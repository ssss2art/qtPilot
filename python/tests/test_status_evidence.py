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
from qtpilot.signal_wait import SignalWaiter, signal_waiter_for


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
            assert resource["evidence"]["signals"] == {"active": False, "known": False}
            waiter = signal_waiter_for(connection)
            waiter.track("owned")
            for _ in range(257):
                waiter.handle_notification("qtpilot.signalEmitted", {"subscriptionId": "owned"})
            status = await tool.fn()
            signals = status["evidence"]["signals"]
            assert signals["active"] and signals["known"]
            assert signals["capacity_per_subscription"] == 256
            assert signals["dropped"] == 1
            assert signals["subscriptions"]["owned"]["queued"] == 256


@pytest.mark.asyncio
async def test_disconnected_status_does_not_claim_zero_transport_loss() -> None:
    async with Client(create_server(discovery_enabled=False)) as client:
        resource = json.loads((await client.read_resource("qtpilot://status"))[0].text)
    assert all(not counter["known"] for counter in resource["evidence"]["transport"])
    assert resource["evidence"]["signals"] == {"active": False, "known": False}


def test_signal_status_retains_overflow_and_eviction_totals_after_forgetting() -> None:
    waiter = SignalWaiter(max_buffered=1, max_untracked=1)
    for sub in ("one", "one", "two"):
        waiter.handle_notification("qtpilot.signalEmitted", {"subscriptionId": sub})
    waiter.forget("two")
    report = waiter.status()
    assert report["scope"] == "waiter-lifetime"
    assert report["dropped"] == 1 and report["evicted"] == 1
    assert report["subscriptions"] == {}
