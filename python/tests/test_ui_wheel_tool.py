"""The qt_ui_wheel MCP tool, and replay of the qt.ui.wheel calls it sends."""

from __future__ import annotations

from unittest.mock import AsyncMock, MagicMock, patch

import pytest
from fastmcp import Client
from qtpilot.replay import MUTATING_METHODS, TOOL_TO_METHOD
from qtpilot.server import create_server


@pytest.fixture
def mock_probe() -> MagicMock:
    probe = MagicMock()
    probe.is_connected = True
    probe.call = AsyncMock(return_value={"ok": True})
    return probe


async def _call_wheel(probe: MagicMock, args: dict) -> None:
    with patch("qtpilot.server.require_probe", return_value=probe):
        mcp = create_server(mode="native", discovery_enabled=False)
        async with Client(mcp) as client:
            await client.call_tool("qt_ui_wheel", args)


@pytest.mark.asyncio
async def test_minimal_call_leaves_defaults_to_the_probe(mock_probe: MagicMock) -> None:
    await _call_wheel(mock_probe, {"objectId": "canvas"})
    mock_probe.call.assert_awaited_once_with("qt.ui.wheel", {"objectId": "canvas"})


@pytest.mark.asyncio
async def test_every_parameter_reaches_the_probe(mock_probe: MagicMock) -> None:
    await _call_wheel(
        mock_probe,
        {
            "objectId": "canvas",
            "notches": -3,
            "device": "trackpad",
            "route": "direct",
            "position": [40, 60],
            "viewObjectId": "planView",
            "modifiers": "ctrl",
            "angleDelta": [0, -120],
            "pixelDelta": {"x": 0, "y": -24},
            "dryRun": True,
        },
    )
    mock_probe.call.assert_awaited_once_with(
        "qt.ui.wheel",
        {
            "objectId": "canvas",
            "notches": -3,
            "device": "trackpad",
            "route": "direct",
            "position": {"x": 40, "y": 60},
            "viewObjectId": "planView",
            "modifiers": "ctrl",
            "angleDelta": {"x": 0, "y": -120},
            "pixelDelta": {"x": 0, "y": -24},
            "dryRun": True,
        },
    )


def test_replay_re_drives_a_recorded_wheel() -> None:
    assert "qt.ui.wheel" in MUTATING_METHODS
    assert TOOL_TO_METHOD["qt_ui_wheel"] == "qt.ui.wheel"
