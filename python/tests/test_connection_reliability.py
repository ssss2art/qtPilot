"""Transport reliability tests for ProbeConnection and qtpilot_status."""

from __future__ import annotations

import asyncio
import json
from unittest.mock import AsyncMock, patch

import pytest
from websockets.asyncio.server import ServerConnection, serve

from qtpilot import _mcp_compat
from qtpilot.connection import ProbeConnection
from qtpilot.evidence import BufferEvidence
from qtpilot.server import create_server, get_state


@pytest.mark.asyncio
async def test_qtpilot_status_reports_probe_protocol_version() -> None:
    server = create_server(discovery_enabled=False)
    connection = ProbeConnection("ws://127.0.0.1:9222")
    connection._connected = True
    connection._probe_version = "0.3.1"
    connection._probe_protocol_version = 1
    counters = (BufferEvidence("probe", None), BufferEvidence("controller", "session-1"))

    with patch.object(connection, "loss_evidence", AsyncMock(return_value=counters)):
        get_state().probe = connection
        tool = await _mcp_compat.find_tool(server, "qtpilot_status")
        assert tool is not None
        status = await tool.fn()

    assert status["connection"]["connected"] is True
    assert status["connection"]["probe_version"] == "0.3.1"
    assert status["connection"]["protocol_version"] == 1


@pytest.mark.asyncio
async def test_recv_loop_ignores_non_dict_json_frames_before_valid_reply() -> None:
    async def handler(ws: ServerConnection) -> None:
        raw = await ws.recv()
        request = json.loads(raw)
        await ws.send("null")
        await ws.send("[]")
        await ws.send(json.dumps({"jsonrpc": "2.0", "id": request["id"], "result": {"pong": True}}))

    async with serve(handler, "127.0.0.1", 0) as server:
        port = server.sockets[0].getsockname()[1]
        conn = ProbeConnection(f"ws://127.0.0.1:{port}")
        await conn.connect()
        try:
            reply = await asyncio.wait_for(conn.call("ping"), timeout=2.0)
            assert reply == {"pong": True}
            assert conn.is_connected
        finally:
            await conn.disconnect()


@pytest.mark.asyncio
async def test_call_cleans_up_pending_when_ws_send_fails() -> None:
    class FailingSendSocket:
        async def send(self, payload: str) -> None:
            raise OSError("broken pipe")

        async def close(self) -> None:
            pass

    conn = ProbeConnection("ws://127.0.0.1:9222")
    conn._ws = FailingSendSocket()
    conn._connected = True

    with pytest.raises(OSError, match="broken pipe"):
        await conn.call("ping", timeout=1.0)

    assert conn._pending == {}


@pytest.mark.asyncio
async def test_clean_close_fails_pending_futures() -> None:
    request_received = asyncio.Event()

    async def handler(ws: ServerConnection) -> None:
        await ws.recv()
        request_received.set()
        await ws.close(code=1000, reason="shutting down")

    async with serve(handler, "127.0.0.1", 0) as server:
        port = server.sockets[0].getsockname()[1]
        conn = ProbeConnection(f"ws://127.0.0.1:{port}")
        await conn.connect()
        try:
            call_task = asyncio.create_task(conn.call("ping", timeout=2.0))
            await asyncio.wait_for(request_received.wait(), timeout=2.0)
            with pytest.raises(ConnectionError, match="closed"):
                await asyncio.wait_for(call_task, timeout=2.0)
            assert conn._pending == {}
        finally:
            await conn.disconnect()
