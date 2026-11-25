"""Acceptance is the default; exploratory transcripts need an explicit opt-in."""

from __future__ import annotations

import json
from pathlib import Path
from unittest.mock import AsyncMock

import pytest
from fastmcp import FastMCP

from qtpilot import _mcp_compat
from qtpilot.cli import cmd_replay, create_parser
from qtpilot.replay import Observation, ReplayResult, Scenario, Step
from qtpilot.tools.replay_tools import register_replay_tools
from tests.test_contract_runner import ContractProbe
from tests.test_replay_contract import contract_document
from tests.test_replay_cli import CLICK_SESSION, write_log


@pytest.fixture
def contract_file(tmp_path: Path) -> Path:
    path = tmp_path / "contract.json"
    path.write_text(json.dumps(contract_document()), encoding="utf-8")
    return path


@pytest.fixture
def probe(monkeypatch: pytest.MonkeyPatch) -> ContractProbe:
    connection = ContractProbe()
    connection.connect = AsyncMock()
    connection.handshake = AsyncMock()
    connection.disconnect = AsyncMock()
    monkeypatch.setattr("qtpilot.connection.ProbeConnection", lambda url: connection)
    monkeypatch.setattr("qtpilot.server.get_probe", lambda: connection)
    return connection


def test_cli_defaults_to_strict_contract_acceptance(contract_file: Path, probe: ContractProbe, capsys: pytest.CaptureFixture[str]) -> None:
    args = create_parser().parse_args(["replay", str(contract_file), "--json"])
    assert cmd_replay(args) == 0
    report = json.loads(capsys.readouterr().out)
    assert report["mode"] == "strict" and report["strict_passed"] is True
    assert report["comparison_units"] == "assertions"
    assert report["evidence_counts"] == {"exact": 3, "normalized": 0, "wildcard": 0, "unsupported": 0, "unavailable": 0}


def test_cli_refuses_transcript_acceptance_before_connecting(tmp_path: Path, probe: ContractProbe, capsys: pytest.CaptureFixture[str]) -> None:
    args = create_parser().parse_args(["replay", write_log(tmp_path, CLICK_SESSION), "--json"])
    assert cmd_replay(args) == 2
    probe.connect.assert_not_awaited()
    assert "--exploratory" in capsys.readouterr().err


def test_strict_inspection_is_read_only(contract_file: Path, probe: ContractProbe) -> None:
    assert cmd_replay(create_parser().parse_args(["replay", str(contract_file), "--inspect"])) == 0
    probe.connect.assert_not_awaited()


@pytest.mark.asyncio
async def test_mcp_uses_the_same_strict_contract_report(contract_file: Path, probe: ContractProbe) -> None:
    server = FastMCP("synthetic-contract")
    register_replay_tools(server)
    tool = await _mcp_compat.find_tool(server, "qtpilot_replay_run")
    report = await tool.fn(path=str(contract_file))
    assert report["strict_passed"] and report["actions_driven"] == 1
    assert report["comparisons"][-1]["raw"]["timestamp"] == 9876


@pytest.mark.asyncio
async def test_mcp_requires_explicit_exploration_for_unasserted_inline_actions(probe: ContractProbe) -> None:
    server = FastMCP("synthetic-contract")
    register_replay_tools(server)
    tool = await _mcp_compat.find_tool(server, "qtpilot_replay_run")
    with pytest.raises(ValueError, match="exploratory"):
        await tool.fn(steps=[{"method": "qt.properties.set", "params": {"value": "changed"}}])
    assert "qt.properties.set" not in probe.calls


@pytest.mark.asyncio
async def test_exploratory_reports_never_relabel_normalized_evidence_as_exact(
    probe: ContractProbe, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str],
) -> None:
    from qtpilot.cli import _print_report

    collected = Step(0, observations=[
        Observation("qt.properties.get", {}, "visible"),
        Observation("qt.properties.get", {}, "generated~*"),
        Observation("qt.properties.get", {}, "...<truncated 100c>"),
        Observation("qt.properties.get", {}, None, error="unavailable"),
        Observation("qt.properties.get", {}, {"_type": "Opaque", "value": None}),
    ])
    result = ReplayResult(Scenario([collected]), [collected], [])
    monkeypatch.setattr("qtpilot.replay.run_scenario", AsyncMock(return_value=result))
    server = FastMCP("synthetic-exploration")
    register_replay_tools(server)
    tool = await _mcp_compat.find_tool(server, "qtpilot_replay_run")
    report = await tool.fn(steps=[{"method": "qt.properties.set", "params": {"value": "changed"}}], exploratory=True)
    _print_report(result, True)
    cli_report = json.loads(capsys.readouterr().out)
    for view in (report, cli_report):
        assert view["strict_passed"] is False
        assert view["comparison_units"] == "collected observations and notifications"
        assert view["evidence_counts"] == {"exact": 0, "normalized": 1, "wildcard": 2, "unavailable": 1, "unsupported": 1}
        assert view["raw_available"] is False and view["loss_verified"] is False


@pytest.mark.parametrize("exc", [OSError("transport lost"), None])
def test_cmd_replay_returns_aborted_when_run_contract_raises(
    contract_file: Path,
    probe: ContractProbe,
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
    exc: Exception | None,
) -> None:
    from qtpilot.cli import REPLAY_EXIT_ABORTED
    from qtpilot.connection import ProbeError

    err = exc if exc is not None else ProbeError(-32000, "probe crashed")
    monkeypatch.setattr("qtpilot.contract_runner.run_contract", AsyncMock(side_effect=err))
    args = create_parser().parse_args(["replay", str(contract_file), "--json"])
    assert cmd_replay(args) == REPLAY_EXIT_ABORTED
    assert "aborted" in capsys.readouterr().err.lower()


def test_cmd_replay_returns_diverged_on_postcondition_failure(
    contract_file: Path,
    probe: ContractProbe,
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
) -> None:
    from qtpilot.cli import REPLAY_EXIT_DIVERGED
    from qtpilot.contract_runner import ContractResult

    failed = ContractResult(
        fixture="synthetic",
        actions_driven=1,
        total_actions=1,
        failure_kind="postcondition",
        failed_step="step-1",
        reason="mismatch",
    )
    monkeypatch.setattr("qtpilot.contract_runner.run_contract", AsyncMock(return_value=failed))
    args = create_parser().parse_args(["replay", str(contract_file)])
    assert cmd_replay(args) == REPLAY_EXIT_DIVERGED
    out = capsys.readouterr().out
    assert "Strict contract failed: step-1: postcondition: mismatch" in out


def test_cmd_replay_non_json_output_on_pass(
    contract_file: Path,
    probe: ContractProbe,
    capsys: pytest.CaptureFixture[str],
) -> None:
    args = create_parser().parse_args(["replay", str(contract_file)])
    assert cmd_replay(args) == 0
    out = capsys.readouterr().out
    assert "Strict contract passed:" in out
    assert "loss verified" in out


@pytest.mark.parametrize(
    "extra_flags",
    [
        ["--record", "--output", "out.jsonl"],
        ["--watch", "watch.json"],
        ["--settle", "0.2"],
    ],
)
def test_cmd_replay_rejects_exploratory_flags_in_strict_mode(
    contract_file: Path,
    probe: ContractProbe,
    capsys: pytest.CaptureFixture[str],
    extra_flags: list[str],
) -> None:
    from qtpilot.cli import REPLAY_EXIT_USAGE

    args = create_parser().parse_args(["replay", str(contract_file), *extra_flags])
    assert cmd_replay(args) == REPLAY_EXIT_USAGE
    probe.connect.assert_not_awaited()
    assert "--exploratory" in capsys.readouterr().err
