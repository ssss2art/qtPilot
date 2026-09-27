"""MCP status reports observed loss and explicitly unavailable counters."""

from __future__ import annotations

import json
from unittest.mock import AsyncMock, patch

import pytest
from fastmcp import Client

from qtpilot import _mcp_compat
from qtpilot.connection import ProbeConnection
from qtpilot.evidence import BufferEvidence
from qtpilot.server import create_server, get_state


@pytest.mark.asyncio
async def test_resource_and_tool_report_the_same_known_and_unknown_loss() -> None:
    server = create_server(discovery_enabled=False)
    connection = ProbeConnection("ws://127.0.0.1:9222")
    connection._connected = True
    counters = (BufferEvidence("probe", None), BufferEvidence("controller", "session-1", 2, 10, 1))
    with patch.object(connection, "loss_evidence", AsyncMock(return_value=counters)):
        async with Client(server) as client:
            get_state().probe = connection
            resource = json.loads((await client.read_resource("qtpilot://status"))[0].text)
            tool = await _mcp_compat.find_tool(server, "qtpilot_status")
            assert tool is not None
            status = await tool.fn()
            assert resource["evidence"] == status["evidence"]
            loss = resource["evidence"]["transport"]
            assert loss[0]["known"] is False and loss[0]["dropped"] is None
            assert loss[1]["known"] is True and loss[1]["dropped"] == 2


@pytest.mark.asyncio
async def test_disconnected_status_does_not_claim_zero_transport_loss() -> None:
    async with Client(create_server(discovery_enabled=False)) as client:
        resource = json.loads((await client.read_resource("qtpilot://status"))[0].text)
    assert all(not counter["known"] for counter in resource["evidence"]["transport"])
