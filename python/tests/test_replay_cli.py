"""Tests for the `qtpilot replay` subcommand."""

from __future__ import annotations

import argparse
import json
import pathlib

import pytest

from qtpilot.cli import cmd_replay, create_parser
from qtpilot.replay import Divergence, ReplayResult, parse_entries

EXIT_OK = 0
EXIT_DIVERGED = 1
EXIT_USAGE = 2
EXIT_ABORTED = 3


def write_log(tmp_path, entries: list[dict]) -> str:
    path = tmp_path / "session.jsonl"
    path.write_text("\n".join(json.dumps(e) for e in entries) + "\n")
    return str(path)


CLICK_SESSION = [
    {"ts": "t", "dir": "req", "id": 1, "method": "qt.ui.click", "params": {"objectId": "btn"}},
    {"ts": "t", "dir": "res", "id": 1, "method": "qt.ui.click", "dur_ms": 1.0, "result": {"ok": True}},
    {"ts": "t", "dir": "req", "id": 2, "method": "qt.properties.get", "params": {"objectId": "l", "name": "text"}},
    {"ts": "t", "dir": "res", "id": 2, "method": "qt.properties.get", "dur_ms": 1.0, "result": {"value": "clicked"}},
]

MCP_ONLY_SESSION = [
    {"ts": "t", "dir": "mcp_in", "tool": "qt_ui_click", "args": {}},
]


def args_for(path: str, **overrides) -> argparse.Namespace:
    base = {
        "path": path,
        "ws_url": "ws://localhost:9222",
        "settle": 0.0,
        "inspect": False,
        "json": False,
        "watch": None,
        "record": False,
        "output": None,
    }
    base.update(overrides)
    return argparse.Namespace(**base)


class FakeProbe:
    is_connected = True

    def __init__(self, value="clicked"):
        self.handlers = []
        self.value = value

    async def connect(self):
        pass

    async def handshake(self):
        return {}

    async def disconnect(self):
        pass

    async def call(self, method, params=None, timeout=None):
        return {"value": self.value} if method == "qt.properties.get" else {"ok": True}

    def add_notification_handler(self, h):
        self.handlers.append(h)

    def remove_notification_handler(self, h):
        self.handlers.remove(h)


@pytest.fixture
def probe_factory(monkeypatch):
    """Replaces ProbeConnection so the CLI can be driven without a running application."""
    made: list[FakeProbe] = []

    def install(value="clicked"):
        def factory(ws_url):
            probe = FakeProbe(value)
            made.append(probe)
            return probe

        monkeypatch.setattr("qtpilot.cli.ProbeConnection", factory, raising=False)
        import qtpilot.connection as connection

        monkeypatch.setattr(connection, "ProbeConnection", factory)
        return made

    return install


# --- parser ------------------------------------------------------------------


def test_replay_is_a_subcommand():
    args = create_parser().parse_args(["replay", "session.jsonl"])

    assert args.path == "session.jsonl"
    assert args.func is cmd_replay


def test_replay_defaults_are_usable_without_flags():
    args = create_parser().parse_args(["replay", "session.jsonl"])

    assert args.inspect is False
    assert args.json is False
    assert args.settle > 0, "a default replay should allow signals time to arrive"


def test_replay_accepts_the_flags_ci_needs():
    args = create_parser().parse_args(
        ["replay", "s.jsonl", "--ws-url", "ws://host:1234", "--settle", "0.5", "--json"]
    )

    assert args.ws_url == "ws://host:1234"
    assert args.settle == 0.5
    assert args.json is True


# --- inspect mode ------------------------------------------------------------


def test_inspect_reports_a_replayable_log_and_succeeds(tmp_path, capsys):
    code = cmd_replay(args_for(write_log(tmp_path, CLICK_SESSION), inspect=True))

    assert code == EXIT_OK
    assert "qt.ui.click" in capsys.readouterr().out


def test_inspect_fails_on_a_log_with_nothing_to_drive(tmp_path, capsys):
    # Exit non-zero: a scenario that cannot assert anything is a broken fixture, and CI has to
    # hear about it rather than record a pass.
    code = cmd_replay(args_for(write_log(tmp_path, MCP_ONLY_SESSION), inspect=True))

    assert code == EXIT_USAGE
    assert "nothing to replay" in capsys.readouterr().err.lower()


def test_inspect_needs_no_probe(tmp_path):
    # No probe is patched in; reaching for one would raise rather than return a code.
    assert cmd_replay(args_for(write_log(tmp_path, CLICK_SESSION), inspect=True)) == EXIT_OK


def test_a_missing_file_is_a_usage_error(tmp_path, capsys):
    code = cmd_replay(args_for(str(tmp_path / "nope.jsonl"), inspect=True))

    assert code == EXIT_USAGE
    assert capsys.readouterr().err


def test_a_malformed_file_names_the_line(tmp_path, capsys):
    path = tmp_path / "broken.jsonl"
    path.write_text('{"dir":"req"}\nnot json\n')

    code = cmd_replay(args_for(str(path), inspect=True))

    assert code == EXIT_USAGE
    assert "line 2" in capsys.readouterr().err


# --- run mode ----------------------------------------------------------------


def test_a_faithful_replay_exits_zero(tmp_path, probe_factory, capsys):
    probe_factory("clicked")

    code = cmd_replay(args_for(write_log(tmp_path, CLICK_SESSION)))

    assert code == EXIT_OK
    assert "no divergence" in capsys.readouterr().out


def test_a_divergent_replay_exits_non_zero(tmp_path, probe_factory, capsys):
    # The whole contract for ctest: a behaviour change has to fail the process.
    probe_factory("REGRESSED")

    code = cmd_replay(args_for(write_log(tmp_path, CLICK_SESSION)))

    assert code == EXIT_DIVERGED
    out = capsys.readouterr().out
    assert "qt.properties.get" in out
    assert "REGRESSED" in out


def test_the_probe_is_disconnected_afterwards(tmp_path, probe_factory):
    made = probe_factory("clicked")

    cmd_replay(args_for(write_log(tmp_path, CLICK_SESSION)))

    assert made and made[0].handlers == []


def test_json_output_is_machine_readable(tmp_path, probe_factory, capsys):
    probe_factory("REGRESSED")

    code = cmd_replay(args_for(write_log(tmp_path, CLICK_SESSION), json=True))

    payload = json.loads(capsys.readouterr().out)
    assert code == EXIT_DIVERGED
    assert payload["passed"] is False
    assert payload["divergences"][0]["method"] == "qt.properties.get"


def test_running_a_log_with_nothing_to_drive_fails_before_connecting(tmp_path, capsys):
    # No probe patched in: if this tried to connect it would raise, not return a code.
    code = cmd_replay(args_for(write_log(tmp_path, MCP_ONLY_SESSION)))

    assert code == EXIT_USAGE
    assert "nothing to replay" in capsys.readouterr().err.lower()


def test_a_probe_that_is_not_there_is_a_usage_error_not_a_divergence(tmp_path, monkeypatch, capsys):
    # An application that is not running has not "behaved differently". Reporting that as a
    # divergence would send someone hunting a regression that does not exist, and it is the
    # difference between a broken CI runner and a real failure.
    import qtpilot.connection as connection

    class Unreachable:
        def __init__(self, ws_url):
            pass

        async def connect(self):
            raise OSError("Connect call failed ('127.0.0.1', 9222)")

        async def disconnect(self):
            pass

    monkeypatch.setattr(connection, "ProbeConnection", Unreachable)

    code = cmd_replay(args_for(write_log(tmp_path, CLICK_SESSION)))

    assert code == EXIT_USAGE
    assert "connect" in capsys.readouterr().err.lower()


def test_record_without_a_watch_list_is_refused(tmp_path, capsys):
    # Re-recording exactly what the log already observes produces the same file and teaches
    # nobody anything; the useful form is --record with a watch list.
    code = cmd_replay(args_for(write_log(tmp_path, CLICK_SESSION), record=True))

    assert code == EXIT_USAGE
    assert "--watch" in capsys.readouterr().err


def test_record_with_a_watch_list_writes_a_baseline(tmp_path, probe_factory, capsys):
    probe_factory("clicked")
    watch = tmp_path / "watch.json"
    watch.write_text(json.dumps({
        "watch": [{"method": "qt.properties.get", "params": {"objectId": "l", "name": "text"}}]
    }))
    out = tmp_path / "golden.jsonl"

    code = cmd_replay(
        args_for(write_log(tmp_path, CLICK_SESSION), record=True, watch=str(watch), output=str(out))
    )

    assert code == EXIT_OK
    assert out.exists()
    assert "recorded" in capsys.readouterr().out

    from qtpilot.replay import load_scenario

    reloaded = load_scenario(out)
    assert reloaded.is_replayable
    # One recorded observation plus one watched one on the click step, and the watched one on
    # the baseline step.
    assert sum(len(s.observations) for s in reloaded.steps) == 3


def test_a_watch_list_naming_a_mutating_method_is_refused(tmp_path, capsys):
    watch = tmp_path / "watch.json"
    watch.write_text(json.dumps({"watch": [{"method": "qt.ui.click", "params": {}}]}))

    code = cmd_replay(
        args_for(write_log(tmp_path, CLICK_SESSION), record=True, watch=str(watch))
    )

    assert code == EXIT_USAGE
    assert "qt.ui.click" in capsys.readouterr().err


def test_replay_quietens_the_transport_loggers(tmp_path, probe_factory):
    # main() turns DEBUG on for everything, which buries the report under a line per websocket
    # frame. This command's output is meant to be read by CI.
    import logging

    logging.getLogger("websockets.client").setLevel(logging.DEBUG)
    probe_factory("clicked")

    cmd_replay(args_for(write_log(tmp_path, CLICK_SESSION)))

    assert logging.getLogger("websockets.client").level == logging.WARNING


# --- guards on the destructive --record path ------------------------------


def test_record_requires_an_explicit_output(tmp_path, capsys):
    """--output used to default to the input log, so --record wrote its baseline
    over the recording it had just read. The destination must be deliberate."""
    log = write_log(tmp_path, CLICK_SESSION)
    watch = tmp_path / "watch.json"
    watch.write_text(json.dumps({"watch": [{"method": "qt.properties.get", "params": {}}]}))

    code = cmd_replay(args_for(log, record=True, watch=str(watch)))

    assert code == EXIT_USAGE
    assert "--output" in capsys.readouterr().err
    # The input survived.
    assert pathlib.Path(log).read_text().strip(), "the input log was modified by a refused --record"


def test_record_refuses_to_write_a_partial_baseline_after_an_abort(
    tmp_path, probe_factory, capsys, monkeypatch
):
    """An aborted run stopped partway, so its steps are a truncation rather than
    a baseline. Writing it out replaced a long recording with the few steps that
    ran, printed a success line and exited 0."""
    probe_factory("clicked")
    watch = tmp_path / "watch.json"
    watch.write_text(json.dumps({"watch": [{"method": "qt.properties.get", "params": {}}]}))
    out = tmp_path / "golden.jsonl"

    async def aborted(scenario, probe, **kwargs):
        return ReplayResult(
            scenario=scenario,
            steps=scenario.steps[:1],
            divergences=[],
            aborted_at=1,
            abort_reason="qt.ui.click: Object not found",
        )

    monkeypatch.setattr("qtpilot.replay.run_scenario", aborted)

    code = cmd_replay(
        args_for(write_log(tmp_path, CLICK_SESSION), record=True, watch=str(watch), output=str(out))
    )

    assert code == EXIT_ABORTED
    assert not out.exists(), "a partial baseline was written after an abort"
    assert "aborted" in capsys.readouterr().err


def test_an_aborted_comparison_run_is_not_reported_as_a_divergence(
    tmp_path, probe_factory, capsys, monkeypatch
):
    """"the application changed" and "the replay fell over" need different
    responses, so they need different exit codes."""
    probe_factory("clicked")

    async def aborted(scenario, probe, **kwargs):
        return ReplayResult(
            scenario=scenario,
            steps=scenario.steps[:1],
            divergences=[],
            aborted_at=1,
            abort_reason="qt.ui.click: Object not found",
        )

    monkeypatch.setattr("qtpilot.replay.run_scenario", aborted)

    code = cmd_replay(args_for(write_log(tmp_path, CLICK_SESSION)))

    assert code == EXIT_ABORTED
    assert code != EXIT_DIVERGED
