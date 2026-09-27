"""Initial-state evidence is a gate: a failed prerequisite must drive zero actions."""

from __future__ import annotations

import pytest
import json
from fastmcp import FastMCP

from qtpilot.fluent import expect_replay
from qtpilot.replay import Action, Observation, ReplayResult, Scenario, Step, run_scenario
from tests.test_replay_driver import FakeProbe


def ready_scenario() -> Scenario:
    return Scenario(steps=[
        Step(index=0, observations=[Observation("qt.properties.get", {"objectId": "form", "name": "ready"}, {"value": True})]),
        Step(index=1, action=Action("qt.ui.click", {"objectId": "submit"})),
    ])


@pytest.mark.asyncio
@pytest.mark.parametrize("record", [False, True], ids=["acceptance", "new-baseline"])
async def test_wrong_initial_state_never_drives_a_mutation(record: bool) -> None:
    probe = FakeProbe(results={"qt.properties.get": {"value": False}})
    result = await run_scenario(ready_scenario(), probe, settle=0, record=record)
    expect_replay(result).to_fail_precondition().to_have_driven(0)
    assert probe.calls == [("qt.properties.get", {"objectId": "form", "name": "ready"})]
    assert probe.handlers == [], "Failed prerequisite retained a notification collector"


@pytest.mark.asyncio
async def test_failed_initial_read_is_a_precondition_failure() -> None:
    probe = FakeProbe(errors={"qt.properties.get": "target disappeared"})
    result = await run_scenario(ready_scenario(), probe, settle=0)
    expect_replay(result).to_fail_precondition().to_have_driven(0)
    assert result.divergences[0].actual == "target disappeared"


@pytest.mark.asyncio
async def test_verified_initial_state_allows_the_action() -> None:
    probe = FakeProbe(results={"qt.properties.get": {"value": True}})
    result = await run_scenario(ready_scenario(), probe, settle=0)
    expect_replay(result).to_pass().to_have_driven(1)
    assert probe.calls[1] == ("qt.ui.click", {"objectId": "submit"})


def test_precondition_matcher_rejects_a_later_action_failure() -> None:
    scenario = ready_scenario()
    result = ReplayResult(scenario, scenario.steps, [], aborted_at=1, abort_reason="action failed")
    with pytest.raises(AssertionError, match="Expected failed precondition"):
        expect_replay(result).to_fail_precondition()


def test_zero_action_matcher_exposes_an_unexpected_mutation() -> None:
    scenario = ready_scenario()
    result = ReplayResult(scenario, scenario.steps, [])
    with pytest.raises(AssertionError, match="Expected 0 actions driven, got 1"):
        expect_replay(result).to_have_driven(0)


@pytest.mark.asyncio
async def test_cli_report_distinguishes_initial_state_failure(capsys: pytest.CaptureFixture[str]) -> None:
    from qtpilot.cli import _print_report
    result = await run_scenario(ready_scenario(), FakeProbe(results={"qt.properties.get": {"value": False}}), settle=0)
    _print_report(result, True)
    report = json.loads(capsys.readouterr().out)
    assert report["failure_kind"] == "precondition"
    assert report["actions_driven"] == 0


@pytest.mark.asyncio
async def test_mcp_report_distinguishes_initial_state_failure(monkeypatch: pytest.MonkeyPatch) -> None:
    from qtpilot.tools.replay_tools import register_replay_tools
    from tests.test_replay_tools import _call_tool
    probe = FakeProbe(results={"qt.properties.get": {"value": False}})
    probe.is_connected = True
    monkeypatch.setattr("qtpilot.server.get_probe", lambda: probe)
    monkeypatch.setattr("qtpilot.replay.load_scenario", lambda path: ready_scenario())
    server = FastMCP("synthetic-fixture")
    register_replay_tools(server)
    report = await _call_tool(server, "qtpilot_replay_run", exploratory=True, path="synthetic.jsonl", settle=0)
    assert report["failure_kind"] == "precondition"
    assert report["actions_driven"] == 0
