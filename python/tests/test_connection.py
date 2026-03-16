"""Unit tests for ProbeConnection JSON-RPC message format and response handling."""

from __future__ import annotations

import asyncio
import json

import pytest
import pytest_asyncio

from qtpilot.connection import ProbeConnection, ProbeError


pytestmark = pytest.mark.asyncio


async def test_call_sends_jsonrpc_format(mock_probe):
    """Verify call() sends a correctly formatted JSON-RPC 2.0 request."""
    probe, mock_ws = mock_probe

    # Pre-configure response for id=1
    mock_ws.responses[1] = {"jsonrpc": "2.0", "result": {"pong": True}, "id": 1}

    result = await probe.call("qt.ping")

    assert len(mock_ws.sent_messages) == 1
    sent = json.loads(mock_ws.sent_messages[0])
    assert sent["jsonrpc"] == "2.0"
    assert sent["method"] == "qt.ping"
    assert sent["params"] == {}
    assert sent["id"] == 1


async def test_call_increments_id(mock_probe):
    """Verify sequential calls produce incrementing IDs."""
    probe, mock_ws = mock_probe

    mock_ws.responses[1] = {"jsonrpc": "2.0", "result": {}, "id": 1}
    mock_ws.responses[2] = {"jsonrpc": "2.0", "result": {}, "id": 2}

    await probe.call("qt.ping")
    await probe.call("qt.version")

    assert len(mock_ws.sent_messages) == 2
    first = json.loads(mock_ws.sent_messages[0])
    second = json.loads(mock_ws.sent_messages[1])
    assert first["id"] == 1
    assert second["id"] == 2


async def test_call_returns_result(mock_probe):
    """Verify call() returns the result dict from the response."""
    probe, mock_ws = mock_probe

    mock_ws.responses[1] = {"jsonrpc": "2.0", "result": {"ok": True}, "id": 1}

    result = await probe.call("qt.ping")
    assert result == {"ok": True}


async def test_call_raises_probe_error(mock_probe):
    """Verify call() raises ProbeError when probe returns an error response."""
    probe, mock_ws = mock_probe

    mock_ws.responses[1] = {
        "jsonrpc": "2.0",
        "error": {"code": -32601, "message": "Method not found"},
        "id": 1,
    }

    with pytest.raises(ProbeError) as exc_info:
        await probe.call("qt.nonexistent")

    assert exc_info.value.code == -32601
    assert "Method not found" in exc_info.value.message


async def test_is_connected_property():
    """Verify is_connected reflects connection state."""
    probe = ProbeConnection("ws://localhost:9222")
    assert probe.is_connected is False


async def test_call_when_disconnected_raises():
    """Verify calling when not connected raises ProbeError."""
    probe = ProbeConnection("ws://localhost:9222")

    with pytest.raises(ProbeError, match="Not connected"):
        await probe.call("qt.ping")
