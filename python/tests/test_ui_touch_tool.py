"""The qt_ui_touch MCP tool, and replay of the qt.ui.touch calls it sends."""

from __future__ import annotations

from unittest.mock import AsyncMock, MagicMock, patch

import pytest
from fastmcp import Client
from qtpilot.replay import MUTATING_METHODS, TOOL_TO_METHOD
from qtpilot.replay_contract import parse_contract
from qtpilot.server import create_server


@pytest.fixture
def mock_probe() -> MagicMock:
    probe = MagicMock()
    probe.is_connected = True
    probe.call = AsyncMock(return_value={"ok": True})
    return probe


async def _call_touch(probe: MagicMock, args: dict) -> None:
    with patch("qtpilot.server.require_probe", return_value=probe):
        mcp = create_server(mode="native", discovery_enabled=False)
        async with Client(mcp) as client:
            await client.call_tool("qt_ui_touch", args)


@pytest.mark.asyncio
async def test_fingers_reach_the_probe_as_given(mock_probe: MagicMock) -> None:
    fingers = [{"id": 0, "x": 10.5, "y": 20}, {"id": 1, "x": 40, "y": 20}]
    await _call_touch(mock_probe, {"objectId": "canvas", "points": fingers})
    mock_probe.call.assert_awaited_once_with(
        "qt.ui.touch", {"objectId": "canvas", "points": fingers}
    )


@pytest.mark.asyncio
async def test_id_x_y_lists_are_shorthand_for_fingers(mock_probe: MagicMock) -> None:
    await _call_touch(mock_probe, {"objectId": "canvas", "points": [[0, 10, 20], [1, 40, 20]]})
    mock_probe.call.assert_awaited_once_with(
        "qt.ui.touch",
        {
            "objectId": "canvas",
            "points": [{"id": 0, "x": 10, "y": 20}, {"id": 1, "x": 40, "y": 20}],
        },
    )


@pytest.mark.asyncio
async def test_no_points_lifts_every_finger(mock_probe: MagicMock) -> None:
    await _call_touch(mock_probe, {"objectId": "canvas"})
    mock_probe.call.assert_awaited_once_with("qt.ui.touch", {"objectId": "canvas", "points": []})


def test_replay_re_drives_a_recorded_touch() -> None:
    assert "qt.ui.touch" in MUTATING_METHODS
    assert TOOL_TO_METHOD["qt_ui_touch"] == "qt.ui.touch"


def test_a_contract_step_can_touch() -> None:
    contract = """{
      "format": 2, "fixture": "touch-app", "timeout": 5,
      "requirements": [{"name": "protocol", "method": "getVersion", "params": {},
                        "path": ["protocolVersion"], "expected": 1}],
      "preconditions": [{"name": "shown", "method": "qt.properties.get",
                         "params": {"objectId": "pad", "name": "visible"},
                         "path": ["result", "value"], "expected": true}],
      "steps": [{"name": "press",
                 "action": {"method": "qt.ui.touch",
                            "params": {"objectId": "pad", "points": [{"id": 0, "x": 5, "y": 5}]}},
                 "checkpoint": {"objectId": "pad", "signal": "pressedChanged"},
                 "postconditions": []}]
    }"""
    assert parse_contract(contract).is_ok()
