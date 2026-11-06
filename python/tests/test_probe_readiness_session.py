"""Readiness at the MCP/session boundary, using real loopback WebSockets."""
from __future__ import annotations

import asyncio
from collections.abc import Iterator
from dataclasses import dataclass
import json

from fastmcp import Client, FastMCP
import pytest
from websockets.asyncio.server import ServerConnection, serve

from qtpilot import server as sessions
from qtpilot.connection import ProbeRefusedError
from qtpilot.tools.discovery_tools import register_discovery_tools


@dataclass(frozen=True)
class SessionExpectation:
    state: sessions.ServerState

    def to_be_connected_to(self, url: str) -> None:
        assert self.state.probe is not None, "publish the ready probe"
        assert self.state.probe.is_connected, "only report a live probe as ready"
        assert self.state.probe.ws_url == url
        assert self.state.probe.probe_version == "synthetic"

    def to_be_disconnected(self) -> None:
        assert self.state.probe is None, "failed or cancelled admission must not publish a probe"


def expect_session(state: sessions.ServerState) -> SessionExpectation:
    return SessionExpectation(state)


@pytest.fixture
def state(monkeypatch: pytest.MonkeyPatch) -> Iterator[sessions.ServerState]:
    for name in ("QTPILOT_PROFILE", "QTPILOT_AUTH_TOKEN_FILE", "QTPILOT_TLS_CA_FILE"):
        monkeypatch.delenv(name, raising=False)
    mcp = FastMCP("readiness-test")
    register_discovery_tools(mcp)
    current = sessions.ServerState(mcp)
    monkeypatch.setattr(sessions, "_state", current)
    yield current


async def answer(ws: ServerConnection) -> None:
    async for raw in ws:
        request = json.loads(raw)
        await ws.send(json.dumps({"jsonrpc": "2.0", "id": request["id"],
                                  "result": {"version": "synthetic", "protocolVersion": 1}}))


@pytest.mark.asyncio
async def test_default_session_connection_retries_a_starting_probe(state: sessions.ServerState) -> None:
    attempts = 0

    async def starting(ws: ServerConnection) -> None:
        nonlocal attempts
        attempts += 1
        if attempts == 1:
            await ws.close(1011, "starting")
        else:
            await answer(ws)

    async with serve(starting, "127.0.0.1", 0) as probe:
        url = f"ws://127.0.0.1:{probe.sockets[0].getsockname()[1]}"
        try:
            await sessions.connect_to_probe(url)
            expect_session(state).to_be_connected_to(url)
            assert attempts == 2
        finally:
            await sessions.disconnect_probe()


@pytest.mark.asyncio
async def test_mcp_exposes_a_bounded_readiness_wait(state: sessions.ServerState) -> None:
    admitted = asyncio.Event()

    async def silent(ws: ServerConnection) -> None:
        admitted.set()
        await ws.wait_closed()

    async with serve(silent, "127.0.0.1", 0) as probe:
        url = f"ws://127.0.0.1:{probe.sockets[0].getsockname()[1]}"
        async with Client(state.mcp) as client:
            result = await asyncio.wait_for(client.call_tool(
                "qtpilot_connect_probe", {"ws_url": url, "ready_timeout": 0.1},
                raise_on_error=False,
            ), timeout=2)
        assert admitted.is_set(), "the timeout must bound an actual connection attempt"
        assert result.is_error
        assert "not ready" in str(result.content)
        expect_session(state).to_be_disconnected()


@pytest.mark.asyncio
async def test_cancelled_session_does_not_publish_half_ready_probe(state: sessions.ServerState) -> None:
    requested = asyncio.Event()
    closed = asyncio.Event()

    async def silent(ws: ServerConnection) -> None:
        await ws.recv()
        requested.set()
        await ws.wait_closed()
        closed.set()

    async with serve(silent, "127.0.0.1", 0) as probe:
        url = f"ws://127.0.0.1:{probe.sockets[0].getsockname()[1]}"
        task = asyncio.create_task(sessions.connect_to_probe(url))
        try:
            await asyncio.wait_for(requested.wait(), timeout=2)
            task.cancel()
            with pytest.raises(asyncio.CancelledError):
                await task
            expect_session(state).to_be_disconnected()
            await asyncio.wait_for(closed.wait(), timeout=2)
        finally:
            task.cancel()
            await sessions.disconnect_probe()


@pytest.mark.asyncio
async def test_policy_refusal_is_immediate_and_not_published(state: sessions.ServerState) -> None:
    attempts = 0

    async def refuse(ws: ServerConnection) -> None:
        nonlocal attempts
        attempts += 1
        await ws.close(1008, "synthetic refusal")

    async with serve(refuse, "127.0.0.1", 0) as probe:
        url = f"ws://127.0.0.1:{probe.sockets[0].getsockname()[1]}"
        try:
            with pytest.raises(ProbeRefusedError):
                await asyncio.wait_for(sessions.connect_to_probe(url), timeout=2)
            assert attempts == 1
            expect_session(state).to_be_disconnected()
        finally:
            await sessions.disconnect_probe()


@pytest.mark.asyncio
@pytest.mark.parametrize("timeout", [0.0, -1.0, float("inf"), float("nan"), 61.0])
async def test_invalid_wait_preserves_existing_connection(
    state: sessions.ServerState, timeout: float,
) -> None:
    async with serve(answer, "127.0.0.1", 0) as probe:
        url = f"ws://127.0.0.1:{probe.sockets[0].getsockname()[1]}"
        try:
            await sessions.connect_to_probe(url)
            with pytest.raises(ValueError, match="ready_timeout"):
                await sessions.connect_to_probe(url, ready_timeout=timeout)
            expect_session(state).to_be_connected_to(url)
        finally:
            await sessions.disconnect_probe()


@pytest.mark.asyncio
async def test_replacing_a_closed_probe_clears_stale_session_on_failure(
    state: sessions.ServerState,
) -> None:
    async def refuse(ws: ServerConnection) -> None:
        await ws.close(1008, "synthetic refusal")

    async with serve(answer, "127.0.0.1", 0) as first:
        first_url = f"ws://127.0.0.1:{first.sockets[0].getsockname()[1]}"
        previous = await sessions.connect_to_probe(first_url)
        await previous.disconnect()
        async with serve(refuse, "127.0.0.1", 0) as second:
            next_url = f"ws://127.0.0.1:{second.sockets[0].getsockname()[1]}"
            try:
                with pytest.raises(ProbeRefusedError):
                    await sessions.connect_to_probe(next_url)
                expect_session(state).to_be_disconnected()
            finally:
                await sessions.disconnect_probe()


@pytest.mark.asyncio
@pytest.mark.parametrize("replace", [False, True], ids=["disconnect", "replace"])
async def test_pending_admission_is_cancelled_before_session_transition(
    state: sessions.ServerState, replace: bool,
) -> None:
    requested = asyncio.Event()
    closed = asyncio.Event()

    async def stalled(ws: ServerConnection) -> None:
        await ws.recv()
        requested.set()
        await ws.wait_closed()
        closed.set()

    async with serve(stalled, "127.0.0.1", 0) as first, serve(answer, "127.0.0.1", 0) as second:
        first_url = f"ws://127.0.0.1:{first.sockets[0].getsockname()[1]}"
        second_url = f"ws://127.0.0.1:{second.sockets[0].getsockname()[1]}"
        pending = asyncio.create_task(sessions.connect_to_probe(first_url))
        try:
            await asyncio.wait_for(requested.wait(), timeout=2)
            async with Client(state.mcp) as client:
                if replace:
                    await client.call_tool("qtpilot_connect_probe", {"ws_url": second_url})
                    expect_session(state).to_be_connected_to(second_url)
                else:
                    result = await client.call_tool("qtpilot_disconnect_probe", {})
                    assert result.data["ok"], "disconnect must acknowledge pending admission"
                    expect_session(state).to_be_disconnected()
            await asyncio.wait_for(closed.wait(), timeout=1)
            with pytest.raises(asyncio.CancelledError):
                await pending
        finally:
            pending.cancel()
            await asyncio.gather(pending, return_exceptions=True)
            await sessions.disconnect_probe()


@pytest.mark.asyncio
async def test_subsecond_wait_retries_before_its_deadline(state: sessions.ServerState) -> None:
    attempts = 0

    async def starting(ws: ServerConnection) -> None:
        nonlocal attempts
        attempts += 1
        if attempts == 1:
            await ws.close(1011, "starting")
        else:
            await answer(ws)

    async with serve(starting, "127.0.0.1", 0) as probe:
        url = f"ws://127.0.0.1:{probe.sockets[0].getsockname()[1]}"
        try:
            async with Client(state.mcp) as client:
                await client.call_tool("qtpilot_connect_probe", {"ws_url": url, "ready_timeout": 0.5})
            expect_session(state).to_be_connected_to(url)
            assert attempts == 2
        finally:
            await sessions.disconnect_probe()


@pytest.mark.asyncio
@pytest.mark.parametrize("unpublished", [False, True], ids=["ready", "admitted-not-published"])
async def test_cancelled_disconnect_finishes_socket_cleanup(
    state: sessions.ServerState, monkeypatch: pytest.MonkeyPatch, unpublished: bool,
) -> None:
    closing = asyncio.Event()
    release = asyncio.Event()
    closed = asyncio.Event()

    async def observed(ws: ServerConnection) -> None:
        try:
            await answer(ws)
        finally:
            closed.set()

    async with serve(observed, "127.0.0.1", 0) as probe:
        url = f"ws://127.0.0.1:{probe.sockets[0].getsockname()[1]}"
        conn = await sessions.connect_to_probe(url)
        real_disconnect = conn.disconnect

        async def delayed_disconnect() -> None:
            closing.set()
            await release.wait()
            await real_disconnect()

        if unpublished:
            # Exercise the scheduling gap after admission succeeds, before publication.
            async def admitted() -> None:
                return None

            admission = asyncio.create_task(admitted())
            await admission
            state.probe = None
            state.pending_connection = sessions.PendingConnection(conn, admission)
        monkeypatch.setattr(conn, "disconnect", delayed_disconnect)
        task = asyncio.create_task(sessions.disconnect_probe())
        try:
            await asyncio.wait_for(closing.wait(), timeout=2)
            task.cancel()
            release.set()
            with pytest.raises(asyncio.CancelledError):
                await task
            await asyncio.wait_for(closed.wait(), timeout=0.5)
            expect_session(state).to_be_disconnected()
            assert state.pending_connection is None
        finally:
            release.set()
            await asyncio.gather(task, return_exceptions=True)
            await real_disconnect()


@pytest.mark.asyncio
@pytest.mark.parametrize("connected", [False, True], ids=["discovery-only", "with-probe"])
async def test_mcp_lifespan_closes_discovery_even_when_shutdown_is_cancelled(
    monkeypatch: pytest.MonkeyPatch, connected: bool,
) -> None:
    monkeypatch.delenv("QTPILOT_PROFILE", raising=False)
    monkeypatch.delenv("QTPILOT_AUTH_TOKEN_FILE", raising=False)
    async with serve(answer, "127.0.0.1", 0) as probe:
        url = f"ws://127.0.0.1:{probe.sockets[0].getsockname()[1]}"
        mcp = sessions.create_server(ws_url=url if connected else None, discovery_port=0)
        listener = None
        try:
            async with Client(mcp) as client:
                await client.list_tools()
                listener = sessions.get_state().discovery
                assert listener is not None and listener.is_running
            assert not listener.is_running, "MCP cancellation must not skip UDP teardown"
            assert sessions.get_state().discovery is None
        finally:
            if listener is not None:
                await listener.stop()
