"""connect_when_ready(): connect and handshake, retrying while the app is still starting.

The defect it replaces: every session driving a tablet wrote its own retry loop round
connect(), because a probe in an app that is still starting stalls the WebSocket opening
handshake ("timed out during opening handshake") rather than refusing it.

These run against a real loopback WebSocket server, so the sockets, reader tasks, pending
calls and security checks are the real ones.
"""

from __future__ import annotations

import asyncio
import json
import socket
import time

import pytest
from websockets.asyncio.server import ServerConnection, serve

from qtpilot.connection import ProbeConnection


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
