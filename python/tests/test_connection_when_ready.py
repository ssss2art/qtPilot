"""connect_when_ready(): connect and handshake, retrying while the app is still starting.

The defect it replaces: every session driving a tablet wrote its own retry loop round
connect(), because a probe in an app that is still starting stalls the WebSocket opening
handshake ("timed out during opening handshake") rather than refusing it.

These run against a real loopback WebSocket server, so the sockets, reader tasks, pending
calls and security checks are the real ones.
"""

from __future__ import annotations

import asyncio
import base64
import hashlib
import json
import socket
import ssl
import time

import pytest
from websockets.asyncio.server import ServerConnection, serve

from qtpilot.connection import ProbeConnection, ProbeRefusedError


async def _answers(ws: ServerConnection) -> None:
    async for raw in ws:
        request = json.loads(raw)
        await ws.send(json.dumps({"jsonrpc": "2.0", "id": request["id"], "result": {"version": "1", "protocolVersion": 1}}))


async def _silent(ws: ServerConnection) -> None:
    async for _ in ws:
        pass


async def _rejects_get_version(ws: ServerConnection) -> None:
    async for raw in ws:
        request = json.loads(raw)
        await ws.send(json.dumps({"jsonrpc": "2.0", "id": request["id"], "error": {"code": -32601, "message": "Method not found"}}))


def _free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


@pytest.mark.asyncio
async def test_it_waits_for_a_probe_that_comes_up_later():
    port = _free_port()
    conn = ProbeConnection(f"ws://127.0.0.1:{port}")

    async def start_late() -> None:
        await asyncio.sleep(0.5)
        async with serve(_answers, "127.0.0.1", port):
            await asyncio.sleep(5)

    server = asyncio.create_task(start_late())
    try:
        info = await conn.connect_when_ready(ready_timeout=5, interval=0.1)
        assert info["version"] == "1"
        assert conn.is_connected, "a ready connection is kept open"
    finally:
        await conn.disconnect()
        server.cancel()
        with pytest.raises(asyncio.CancelledError):
            await server


@pytest.mark.asyncio
async def test_the_deadline_bounds_an_attempt_that_never_answers():
    # The call timeout (30 s) is longer than the wait asked for: the wait must still end on time.
    async with serve(_silent, "127.0.0.1", 0) as server:
        conn = ProbeConnection(f"ws://127.0.0.1:{server.sockets[0].getsockname()[1]}")
        started = time.monotonic()
        with pytest.raises(TimeoutError, match="not ready"):
            await conn.connect_when_ready(ready_timeout=1, interval=0.1)
        assert time.monotonic() - started < 2
        assert not conn.is_connected


@pytest.mark.asyncio
async def test_a_probe_that_rejects_get_version_is_connected_to_degraded():
    async with serve(_rejects_get_version, "127.0.0.1", 0) as server:
        conn = ProbeConnection(f"ws://127.0.0.1:{server.sockets[0].getsockname()[1]}")
        attempts = 0
        real_connect = conn.connect

        async def counting_connect() -> None:
            nonlocal attempts
            attempts += 1
            await real_connect()

        conn.connect = counting_connect  # type: ignore[method-assign]
        try:
            assert await conn.connect_when_ready(ready_timeout=3, interval=0.1) == {}
            assert attempts == 1, "it answered, so it is up: no reconnecting"
            assert conn.is_connected
        finally:
            await conn.disconnect()


@pytest.mark.asyncio
async def test_a_refusing_security_setting_fails_at_once(monkeypatch: pytest.MonkeyPatch):
    monkeypatch.setenv("QTPILOT_PROFILE", "remote")
    conn = ProbeConnection("ws://127.0.0.1:1")
    started = time.monotonic()
    with pytest.raises(ValueError):
        await conn.connect_when_ready(ready_timeout=30, interval=0.1)
    assert time.monotonic() - started < 1


@pytest.mark.asyncio
async def test_it_gives_up_naming_the_last_failure():
    conn = ProbeConnection(f"ws://127.0.0.1:{_free_port()}")
    with pytest.raises(TimeoutError, match="ConnectionRefusedError|OSError"):
        await conn.connect_when_ready(ready_timeout=0.5, interval=0.05)


@pytest.mark.asyncio
async def test_cancelling_mid_handshake_closes_the_connection():
    async with serve(_silent, "127.0.0.1", 0) as server:
        conn = ProbeConnection(f"ws://127.0.0.1:{server.sockets[0].getsockname()[1]}")
        task = asyncio.create_task(conn.connect_when_ready(ready_timeout=10))
        await asyncio.sleep(0.5)
        task.cancel()
        with pytest.raises(asyncio.CancelledError):
            await task
        assert not conn.is_connected
        assert conn._recv_task is None or conn._recv_task.done()


async def _refuses_at_once(ws: ServerConnection) -> None:
    await ws.close(1008, "Another client is already connected")


async def _refuses_after_the_request(ws: ServerConnection) -> None:
    await ws.recv()
    await asyncio.sleep(0.05)
    await ws.close(1008, "Another client is already connected")


@pytest.mark.asyncio
@pytest.mark.parametrize("handler", [_refuses_at_once, _refuses_after_the_request], ids=["at-once", "after-request"])
async def test_a_refusing_probe_fails_at_once_whenever_the_close_arrives(handler):
    # The probe refuses a second client, a bad token or an untrusted origin by accepting the
    # upgrade and closing with 1008. Raised at once or retried until the timeout depended on
    # whether the close came with the upgrade or a moment after it.
    async with serve(handler, "127.0.0.1", 0) as server:
        conn = ProbeConnection(f"ws://127.0.0.1:{server.sockets[0].getsockname()[1]}")
        started = time.monotonic()
        with pytest.raises(ProbeRefusedError, match="Another client is already connected"):
            await conn.connect_when_ready(ready_timeout=5, interval=0.1)
        assert time.monotonic() - started < 1
        assert not conn.is_connected


@pytest.mark.asyncio
async def test_a_connection_closed_otherwise_is_tried_again():
    closed_once = False

    async def closes_first_then_answers(ws: ServerConnection) -> None:
        nonlocal closed_once
        if not closed_once:
            closed_once = True
            await ws.close(1011, "starting")
            return
        await _answers(ws)

    async with serve(closes_first_then_answers, "127.0.0.1", 0) as server:
        conn = ProbeConnection(f"ws://127.0.0.1:{server.sockets[0].getsockname()[1]}")
        try:
            assert (await conn.connect_when_ready(ready_timeout=5, interval=0.1))["version"] == "1"
        finally:
            await conn.disconnect()


async def _upgrades_then_stops_reading(reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
    # An app whose GUI thread is busy: the upgrade completes, then nothing is read or answered,
    # not even a close frame.
    headers = (await reader.readuntil(b"\r\n\r\n")).decode()
    key = next(line.split(":", 1)[1].strip() for line in headers.split("\r\n") if line.lower().startswith("sec-websocket-key:"))
    accept = base64.b64encode(hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()).decode()
    writer.write(f"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: {accept}\r\n\r\n".encode())
    await writer.drain()
    await asyncio.sleep(30)


@pytest.mark.asyncio
@pytest.mark.parametrize("token", [False, True], ids=["plain", "token"])
async def test_giving_up_on_a_probe_that_stopped_reading_keeps_to_the_deadline(token, monkeypatch, tmp_path):
    if token:
        # With a token the client waits for the probe's admission message, a path of its own.
        token_file = tmp_path / "token"
        token_file.write_text("a" * 40)
        token_file.chmod(0o600)
        monkeypatch.setenv("QTPILOT_AUTH_TOKEN_FILE", str(token_file))
    server = await asyncio.start_server(_upgrades_then_stops_reading, "127.0.0.1", 0)
    try:
        conn = ProbeConnection(f"ws://127.0.0.1:{server.sockets[0].getsockname()[1]}")
        started = time.monotonic()
        with pytest.raises(TimeoutError):
            await conn.connect_when_ready(ready_timeout=1, interval=0.1)
        assert time.monotonic() - started < 2, "a graceful close waited its 5 s close timeout"

        task = asyncio.create_task(conn.connect_when_ready(ready_timeout=10))
        await asyncio.sleep(0.5)
        task.cancel()
        started = time.monotonic()
        with pytest.raises(asyncio.CancelledError):
            await task
        assert time.monotonic() - started < 1
        assert not conn.is_connected
    finally:
        server.close()


async def _rejects_the_token(ws: ServerConnection) -> None:
    await ws.close(1008, "Authentication rejected")


@pytest.mark.asyncio
async def test_a_rejected_token_is_refused_at_once_as_an_admission_failure(monkeypatch, tmp_path):
    # The probe closes a wrong token with 1008 after the upgrade, before its admission message.
    token_file = tmp_path / "token"
    token_file.write_text("a" * 40)
    token_file.chmod(0o600)
    monkeypatch.setenv("QTPILOT_AUTH_TOKEN_FILE", str(token_file))
    async with serve(_rejects_the_token, "127.0.0.1", 0) as server:
        conn = ProbeConnection(f"ws://127.0.0.1:{server.sockets[0].getsockname()[1]}")
        started = time.monotonic()
        with pytest.raises(ProbeRefusedError, match="admission.*Authentication rejected"):
            await conn.connect_when_ready(ready_timeout=5, interval=0.1)
        assert time.monotonic() - started < 1
        with pytest.raises(ConnectionError, match="admission"):
            await conn.connect()


@pytest.mark.asyncio
async def test_a_certificate_the_client_rejects_fails_at_once(monkeypatch: pytest.MonkeyPatch):
    # An untrusted or mismatched certificate will not change while the app starts; retrying it
    # only hid the reason behind a timeout. It is an OSError, which the loop otherwise retries.
    conn = ProbeConnection("ws://127.0.0.1:1")
    attempts = 0

    async def rejected() -> dict:
        nonlocal attempts
        attempts += 1
        raise ssl.SSLCertVerificationError(1, "certificate verify failed: self-signed certificate")

    monkeypatch.setattr(conn, "_attempt_handshake", rejected)
    started = time.monotonic()
    with pytest.raises(ssl.SSLCertVerificationError, match="self-signed"):
        await conn.connect_when_ready(ready_timeout=5, interval=0.05)
    assert attempts == 1
    assert time.monotonic() - started < 1


@pytest.mark.asyncio
async def test_other_tls_failures_are_tried_again(monkeypatch: pytest.MonkeyPatch):
    # A TLS handshake cut short (the app's server not up yet) is a probe not ready, not a refusal.
    conn = ProbeConnection("ws://127.0.0.1:1")
    attempts = 0

    async def cut_short() -> dict:
        nonlocal attempts
        attempts += 1
        if attempts < 3:
            raise ssl.SSLEOFError(8, "EOF occurred in violation of protocol")
        return {"version": "1"}

    monkeypatch.setattr(conn, "_attempt_handshake", cut_short)
    assert (await conn.connect_when_ready(ready_timeout=5, interval=0.01))["version"] == "1"
    assert attempts == 3
