"""The qt_ui_touch MCP tool, and replay of the qt.ui.touch calls it sends."""

from __future__ import annotations

import json
from unittest.mock import AsyncMock, MagicMock, patch

import pytest
from fastmcp import Client
from fastmcp.exceptions import ToolError

from qtpilot import _mcp_compat as mcp_compat
from qtpilot.connection import ProbeError
from qtpilot.contract_runner import run_contract
from qtpilot.evidence import BufferEvidence
from qtpilot.replay import MUTATING_METHODS, TOOL_TO_METHOD, normalise, parse_entries, run_scenario
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


@pytest.mark.asyncio
@pytest.mark.parametrize(
    "bad_points",
    [
        [[10, 20]],
        [[0, 10, 20, 30]],
        [{"id": 0, "x": 10}],
        [{"x": 10, "y": 20}],
        [{"id": 0, "x": "bad", "y": 20}],
        [{"id": True, "x": 10, "y": 20}],
        [{"id": 0, "x": 10, "y": 20}, [0, 15, 25]],
    ],
)
async def test_malformed_points_raise_value_error(
    mock_probe: MagicMock, bad_points: list[object]
) -> None:
    with patch("qtpilot.server.require_probe", return_value=mock_probe):
        mcp = create_server(mode="native", discovery_enabled=False)
        touch_fn = (await mcp_compat.find_tool(mcp, "qt_ui_touch")).fn
        with pytest.raises(ValueError):
            await touch_fn(objectId="canvas", points=bad_points, ctx=None)
    mock_probe.call.assert_not_awaited()


@pytest.mark.asyncio
async def test_malformed_points_via_mcp_client_raises_tool_error(mock_probe: MagicMock) -> None:
    with pytest.raises(ToolError):
        await _call_touch(mock_probe, {"objectId": "canvas", "points": [[10, 20]]})
    mock_probe.call.assert_not_awaited()


@pytest.mark.asyncio
async def test_optional_touch_params_passthrough(mock_probe: MagicMock) -> None:
    await _call_touch(
        mock_probe,
        {
            "objectId": "canvas",
            "points": [[0, 10, 20]],
            "viewObjectId": "planView",
            "dryRun": True,
            "cancel": True,
        },
    )
    mock_probe.call.assert_awaited_once_with(
        "qt.ui.touch",
        {
            "objectId": "canvas",
            "points": [{"id": 0, "x": 10, "y": 20}],
            "viewObjectId": "planView",
            "dryRun": True,
            "cancel": True,
        },
    )


def test_parse_entries_and_normalise_roundtrip_for_touch() -> None:
    entries = [
        {
            "dir": "req",
            "id": 1,
            "ts": 1000,
            "method": "qt.ui.touch",
            "params": {"objectId": "MainWindow/canvas~1", "points": [{"id": 0, "x": 10, "y": 20}]},
        },
        {
            "dir": "ntf",
            "ts": 1005,
            "method": "qtpilot.signalEmitted",
            "params": {"objectId": "MainWindow/canvas~1", "signal": "pressed", "subscriptionId": "sub_1"},
        },
        {
            "dir": "res",
            "id": 1,
            "ts": 1010,
            "dur_ms": 10,
            "method": "qt.ui.touch",
            "result": {"ok": True, "timestamp": 999},
        },
        {
            "dir": "req",
            "id": 2,
            "ts": 1020,
            "method": "qt.ui.touch",
            "params": {"objectId": "MainWindow/canvas~1", "points": []},
        },
        {
            "dir": "res",
            "id": 2,
            "ts": 1025,
            "dur_ms": 5,
            "method": "qt.ui.touch",
            "result": {"ok": True},
        },
    ]
    normalised = normalise(entries[0])
    assert "ts" not in normalised
    assert "id" not in normalised
    assert normalised["params"]["points"][0]["id"] == 0

    scenario = parse_entries(entries)
    assert len(scenario.steps) == 3
    assert scenario.steps[1].action is not None
    assert scenario.steps[1].action.method == "qt.ui.touch"
    assert scenario.steps[1].action.params == {
        "objectId": "MainWindow/canvas~1",
        "points": [{"id": 0, "x": 10, "y": 20}],
    }
    assert scenario.steps[2].action is not None
    assert scenario.steps[2].action.params == {"objectId": "MainWindow/canvas~1", "points": []}


def test_contract_normalises_touch_point_shorthand() -> None:
    contract_json = json.dumps({
        "format": 2,
        "fixture": "touch-app",
        "timeout": 5,
        "requirements": [
            {"name": "protocol", "method": "getVersion", "params": {}, "path": ["protocolVersion"], "expected": 1}
        ],
        "preconditions": [
            {
                "name": "shown",
                "method": "qt.properties.get",
                "params": {"objectId": "pad", "name": "visible"},
                "path": ["result", "value"],
                "expected": True,
            }
        ],
        "steps": [
            {
                "name": "press",
                "action": {"method": "qt.ui.touch", "params": {"objectId": "pad", "points": [[0, 5, 15]]}},
                "checkpoint": {"objectId": "pad", "signal": "pressedChanged"},
                "postconditions": [],
            }
        ],
    })
    parsed = parse_contract(contract_json)
    assert parsed.is_ok()
    contract = parsed.unwrap()
    assert contract.steps[0].action.params["points"] == [{"id": 0, "x": 5, "y": 15}]


@pytest.mark.asyncio
async def test_run_scenario_releases_held_touch_on_abort() -> None:
    entries = [
        {
            "dir": "req",
            "id": 1,
            "method": "qt.ui.touch",
            "params": {"objectId": "canvas", "points": [{"id": 0, "x": 10, "y": 20}]},
        },
        {"dir": "res", "id": 1, "method": "qt.ui.touch", "result": {"ok": True}},
        {"dir": "req", "id": 2, "method": "qt.ui.click", "params": {"objectId": "missingBtn"}},
        {"dir": "res", "id": 2, "method": "qt.ui.click", "result": {"ok": True}},
    ]
    scenario = parse_entries(entries)

    calls: list[tuple[str, dict]] = []

    class FakeProbe:
        def add_notification_handler(self, handler: object) -> None:
            pass

        def remove_notification_handler(self, handler: object) -> None:
            pass

        async def call(self, method: str, params: dict, **kwargs: object) -> dict:
            calls.append((method, params))
            if method == "qt.ui.click":
                raise ProbeError(-32000, "target not found")
            return {"ok": True}

    result = await run_scenario(scenario, FakeProbe(), settle=0, sync=False)
    assert result.aborted_at == 2
    assert ("qt.ui.touch", {"objectId": "canvas", "points": []}) in calls


@pytest.mark.asyncio
async def test_run_contract_releases_held_touch_on_abort() -> None:
    contract = parse_contract(
        json.dumps({
            "format": 2,
            "fixture": "touch-app",
            "timeout": 5,
            "requirements": [
                {"name": "protocol", "method": "getVersion", "params": {}, "path": ["protocolVersion"], "expected": 1}
            ],
            "preconditions": [
                {
                    "name": "shown",
                    "method": "qt.properties.get",
                    "params": {"objectId": "pad", "name": "visible"},
                    "path": ["value"],
                    "expected": True,
                }
            ],
            "steps": [
                {
                    "name": "press",
                    "action": {"method": "qt.ui.touch", "params": {"objectId": "pad", "points": [[0, 5, 5]]}},
                    "postconditions": [
                        {
                            "name": "pressed",
                            "method": "qt.properties.get",
                            "params": {"objectId": "pad", "name": "pressed"},
                            "path": ["value"],
                            "expected": True,
                        }
                    ],
                },
                {
                    "name": "drag_fail",
                    "action": {"method": "qt.ui.touch", "params": {"objectId": "pad", "points": [[0, 20, 20]]}},
                    "postconditions": [
                        {
                            "name": "moved",
                            "method": "qt.properties.get",
                            "params": {"objectId": "pad", "name": "x"},
                            "path": ["value"],
                            "expected": 999,
                        }
                    ],
                },
            ],
        })
    ).unwrap()

    calls: list[tuple[str, dict]] = []

    class FakeContractProbe:
        async def loss_evidence(self) -> tuple[BufferEvidence, BufferEvidence]:
            return (
                BufferEvidence("probe", "e1", 0, 100, 0),
                BufferEvidence("controller", "e1", 0, 100, 0),
            )

        def add_notification_handler(self, handler: object) -> None:
            pass

        def remove_notification_handler(self, handler: object) -> None:
            pass

        async def call(self, method: str, params: dict, **kwargs: object) -> dict:
            calls.append((method, params))
            if method == "getVersion":
                return {"protocolVersion": 1}
            if method == "qt.properties.get":
                if params.get("name") == "visible":
                    return {"value": True}
                if params.get("name") == "pressed":
                    return {"value": True}
                return {"value": 20}
            return {"ok": True}

    result = await run_contract(contract, FakeContractProbe())
    assert not result.passed
    assert result.failure_kind == "postcondition"
    assert calls[-1] == ("qt.ui.touch", {"objectId": "pad", "points": []})


@pytest.mark.asyncio
async def test_prompts_mention_touch_and_multiframe_gestures() -> None:
    mcp = create_server(mode="native", discovery_enabled=False)
    async with Client(mcp) as client:
        explore = await client.get_prompt("qt_explore_ui", {})
        replay_prompt = await client.get_prompt("qt_generate_replay_test", {"goal": "pinch zoom"})
        assert "qt_ui_touch" in explore.messages[0].content.text
        replay_text = replay_prompt.messages[0].content.text
        assert "qt.ui.touch" in replay_text
        assert "multi-frame" in replay_text.lower()
