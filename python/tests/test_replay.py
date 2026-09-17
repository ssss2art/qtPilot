"""Tests for deterministic replay of recorded qtPilot sessions."""

from __future__ import annotations

import json

import pytest

from qtpilot.replay import (
    Divergence,
    Scenario,
    diff_steps,
    load_scenario,
    normalise,
    parse_entries,
)


def entry(**kwargs) -> dict:
    """A log entry with the volatile fields a real log always carries."""
    base = {"ts": "2026-03-06T14:23:01.234Z"}
    base.update(kwargs)
    return base


def req(rid: int, method: str, params: dict | None = None) -> dict:
    return entry(dir="req", id=rid, method=method, params=params or {})


def res(rid: int, method: str, result: dict) -> dict:
    return entry(dir="res", id=rid, method=method, dur_ms=12.3, result=result)


def ntf(method: str, params: dict) -> dict:
    return entry(dir="ntf", method=method, params=params)


# --- normalise ---------------------------------------------------------------


def test_normalise_strips_the_fields_that_differ_every_run():
    # Timestamps, durations and request ids say nothing about behaviour, and comparing them
    # would make every replay fail for reasons the user cannot act on.
    cleaned = normalise(res(7, "qt.properties.get", {"value": 42}))

    assert "ts" not in cleaned
    assert "dur_ms" not in cleaned
    assert "id" not in cleaned
    assert cleaned["result"] == {"value": 42}


def test_normalise_leaves_meaningful_values_alone():
    cleaned = normalise(res(1, "qt.ui.geometry", {"x": 10, "y": 20, "width": 0}))

    assert cleaned["result"] == {"x": 10, "y": 20, "width": 0}


def test_normalise_strips_timing_at_every_depth():
    nested = res(1, "qt.objects.tree", {"children": [{"name": "a", "dur_ms": 5}]})

    assert normalise(nested)["result"]["children"][0] == {"name": "a"}


def test_normalise_keeps_object_ids_inside_results():
    # "id" beside "method" is the request id and means nothing; "id" inside a result names an
    # object, and is the single most meaningful thing the result carries. Dropping it everywhere
    # would leave a diff unable to tell one widget from another.
    nested = res(1, "qt.objects.tree", {"children": [{"id": "mainWindow.okButton"}]})

    assert normalise(nested)["result"]["children"][0] == {"id": "mainWindow.okButton"}


def test_normalise_strips_probe_timestamps_from_result_metadata():
    # Real results carry meta.timestamp, an epoch millisecond count. It is as volatile as the
    # entry's own ts and would fail every replay if compared.
    nested = res(1, "qt.properties.get", {"meta": {"timestamp": 1773618825525}, "value": "hi"})

    assert normalise(nested)["result"]["meta"] == {}
    assert normalise(nested)["result"]["value"] == "hi"


def test_normalise_masks_generated_object_handles():
    # An unregistered object is reported as QObject~<counter>. The counter reflects the order
    # objects happened to be created in, so it is not stable across runs and cannot be asserted
    # on. Registering a name (qt.names.register) is how a recording keeps a stable identity;
    # this only stops the unstable form failing every replay.
    nested = res(1, "qt.objects.inspect", {"meta": {"objectId": "QObject~22"}})

    assert normalise(nested)["result"]["meta"]["objectId"] == "QObject~*"


def test_normalise_leaves_registered_names_intact():
    nested = res(1, "qt.objects.inspect", {"meta": {"objectId": "mainWindow.okButton"}})

    assert normalise(nested)["result"]["meta"]["objectId"] == "mainWindow.okButton"


# --- parsing -----------------------------------------------------------------


def test_parse_splits_into_steps_at_each_mutating_call():
    entries = [
        req(1, "qt.objects.tree"),
        res(1, "qt.objects.tree", {"root": "app"}),
        req(2, "qt.ui.click", {"objectId": "btn"}),
        res(2, "qt.ui.click", {"ok": True}),
        req(3, "qt.properties.get", {"objectId": "label", "name": "text"}),
        res(3, "qt.properties.get", {"value": "clicked"}),
        req(4, "qt.ui.click", {"objectId": "btn2"}),
        res(4, "qt.ui.click", {"ok": True}),
    ]

    scenario = parse_entries(entries)

    # Step 0 is the baseline: what was observed before anything was driven.
    assert scenario.steps[0].action is None
    assert len(scenario.steps) == 3
    assert scenario.steps[1].action.method == "qt.ui.click"
    assert scenario.steps[1].action.params == {"objectId": "btn"}
    assert scenario.steps[2].action.params == {"objectId": "btn2"}


def test_observations_attach_to_the_step_that_produced_them():
    entries = [
        req(1, "qt.ui.click", {"objectId": "btn"}),
        res(1, "qt.ui.click", {"ok": True}),
        req(2, "qt.properties.get", {"objectId": "label", "name": "text"}),
        res(2, "qt.properties.get", {"value": "clicked"}),
    ]

    scenario = parse_entries(entries)

    assert len(scenario.steps) == 2
    observed = scenario.steps[1].observations
    assert len(observed) == 1
    assert observed[0].method == "qt.properties.get"
    assert observed[0].result == {"value": "clicked"}


def test_a_mutating_calls_own_result_is_not_an_observation():
    # Re-driving the click produces its own result; asserting on it would be asserting that
    # the driver worked, not that the app behaved.
    entries = [
        req(1, "qt.ui.click", {"objectId": "btn"}),
        res(1, "qt.ui.click", {"ok": True}),
    ]

    assert parse_entries(entries).steps[1].observations == []


def test_notifications_are_collected_per_step():
    entries = [
        req(1, "qt.ui.click", {"objectId": "btn"}),
        res(1, "qt.ui.click", {"ok": True}),
        ntf("qtpilot.signalEmitted", {"objectId": "btn", "signal": "clicked"}),
    ]

    scenario = parse_entries(entries)

    assert scenario.steps[1].notifications == [
        ("qtpilot.signalEmitted", {"objectId": "btn", "signal": "clicked"})
    ]


def test_screenshots_are_never_observations():
    # Image bytes belong in a visual golden, not in a JSON diff, and the logger has already
    # replaced them with a placeholder anyway.
    entries = [
        req(1, "qt.ui.click", {"objectId": "btn"}),
        res(1, "qt.ui.click", {"ok": True}),
        req(2, "qt.ui.screenshot", {}),
        res(2, "qt.ui.screenshot", {"data": "<image:99999b>"}),
    ]

    assert parse_entries(entries).steps[1].observations == []


def test_mcp_level_entries_are_ignored():
    # A level-1 log has only mcp_in/mcp_out, which name tools rather than wire calls. Replay
    # drives the wire, so a log without req/res yields nothing to replay -- and must say so
    # rather than silently producing an empty scenario that always passes.
    entries = [
        entry(dir="mcp_in", tool="qt_ui_click", args={"objectId": "btn"}),
        entry(dir="mcp_out", tool="qt_ui_click", dur_ms=5.0, ok=True),
    ]

    scenario = parse_entries(entries)

    assert scenario.steps[0].action is None
    assert len(scenario.steps) == 1
    assert not scenario.is_replayable


def test_a_scenario_with_a_mutating_call_is_replayable():
    entries = [req(1, "qt.ui.click", {"objectId": "btn"}), res(1, "qt.ui.click", {"ok": True})]

    assert parse_entries(entries).is_replayable


def test_load_scenario_reads_jsonl_and_skips_blank_lines(tmp_path):
    path = tmp_path / "session.jsonl"
    path.write_text(
        json.dumps(req(1, "qt.ui.click", {"objectId": "btn"}))
        + "\n\n"
        + json.dumps(res(1, "qt.ui.click", {"ok": True}))
        + "\n"
    )

    scenario = load_scenario(path)

    assert isinstance(scenario, Scenario)
    assert len(scenario.steps) == 2
    assert scenario.source == str(path)


def test_load_scenario_rejects_a_malformed_line(tmp_path):
    path = tmp_path / "broken.jsonl"
    path.write_text(json.dumps(req(1, "qt.ui.click")) + "\nnot json\n")

    with pytest.raises(ValueError, match="line 2"):
        load_scenario(path)


# --- diffing -----------------------------------------------------------------


def test_identical_runs_produce_no_divergence():
    scenario = parse_entries([
        req(1, "qt.ui.click", {"objectId": "btn"}),
        res(1, "qt.ui.click", {"ok": True}),
        req(2, "qt.properties.get", {"objectId": "label", "name": "text"}),
        res(2, "qt.properties.get", {"value": "clicked"}),
    ])

    assert diff_steps(scenario.steps, scenario.steps) == []


def test_a_changed_observation_is_reported_with_its_method_and_step():
    recorded = parse_entries([
        req(1, "qt.ui.click", {"objectId": "btn"}),
        res(1, "qt.ui.click", {"ok": True}),
        req(2, "qt.properties.get", {"objectId": "label", "name": "text"}),
        res(2, "qt.properties.get", {"value": "clicked"}),
    ])
    actual = parse_entries([
        req(1, "qt.ui.click", {"objectId": "btn"}),
        res(1, "qt.ui.click", {"ok": True}),
        req(2, "qt.properties.get", {"objectId": "label", "name": "text"}),
        res(2, "qt.properties.get", {"value": "nothing happened"}),
    ])

    divergences = diff_steps(recorded.steps, actual.steps)

    assert len(divergences) == 1
    assert isinstance(divergences[0], Divergence)
    assert divergences[0].step == 1
    assert divergences[0].method == "qt.properties.get"
    assert divergences[0].expected == {"value": "clicked"}
    assert divergences[0].actual == {"value": "nothing happened"}


def test_timing_differences_alone_are_not_a_divergence():
    recorded = parse_entries([
        req(1, "qt.ui.click", {"objectId": "btn"}),
        res(1, "qt.ui.click", {"ok": True}),
        req(2, "qt.properties.get", {"objectId": "l", "name": "text"}),
        entry(dir="res", id=2, method="qt.properties.get", dur_ms=3.1, result={"value": "x"}),
    ])
    actual = parse_entries([
        req(9, "qt.ui.click", {"objectId": "btn"}),
        res(9, "qt.ui.click", {"ok": True}),
        req(10, "qt.properties.get", {"objectId": "l", "name": "text"}),
        entry(dir="res", id=10, method="qt.properties.get", dur_ms=904.7, result={"value": "x"}),
    ])

    assert diff_steps(recorded.steps, actual.steps) == []


def test_truncated_values_are_wildcards():
    # The logger caps long strings, so what was recorded is not what the app returned. Comparing
    # the placeholder against a full value would fail every run for a reason nobody can fix.
    recorded = parse_entries([
        req(1, "qt.ui.click", {"objectId": "b"}),
        res(1, "qt.ui.click", {"ok": True}),
        req(2, "qt.objects.tree", {}),
        res(2, "qt.objects.tree", {"dump": "abc...<truncated 90000c>"}),
    ])
    actual = parse_entries([
        req(1, "qt.ui.click", {"objectId": "b"}),
        res(1, "qt.ui.click", {"ok": True}),
        req(2, "qt.objects.tree", {}),
        res(2, "qt.objects.tree", {"dump": "a completely different string"}),
    ])

    assert diff_steps(recorded.steps, actual.steps) == []


def test_notification_order_within_a_step_does_not_matter():
    # Signal delivery order across independent objects is not something the app promises, so
    # asserting on it would make the suite flaky rather than strict.
    recorded = parse_entries([
        req(1, "qt.ui.click", {"objectId": "b"}),
        res(1, "qt.ui.click", {"ok": True}),
        ntf("qtpilot.signalEmitted", {"objectId": "a", "signal": "changed"}),
        ntf("qtpilot.signalEmitted", {"objectId": "b", "signal": "clicked"}),
    ])
    actual = parse_entries([
        req(1, "qt.ui.click", {"objectId": "b"}),
        res(1, "qt.ui.click", {"ok": True}),
        ntf("qtpilot.signalEmitted", {"objectId": "b", "signal": "clicked"}),
        ntf("qtpilot.signalEmitted", {"objectId": "a", "signal": "changed"}),
    ])

    assert diff_steps(recorded.steps, actual.steps) == []


def test_a_missing_notification_is_a_divergence():
    recorded = parse_entries([
        req(1, "qt.ui.click", {"objectId": "b"}),
        res(1, "qt.ui.click", {"ok": True}),
        ntf("qtpilot.signalEmitted", {"objectId": "b", "signal": "clicked"}),
    ])
    actual = parse_entries([
        req(1, "qt.ui.click", {"objectId": "b"}),
        res(1, "qt.ui.click", {"ok": True}),
    ])

    divergences = diff_steps(recorded.steps, actual.steps)

    assert len(divergences) == 1
    assert divergences[0].kind == "notification"


def test_an_error_where_a_result_was_recorded_is_a_divergence():
    recorded = parse_entries([
        req(1, "qt.ui.click", {"objectId": "b"}),
        res(1, "qt.ui.click", {"ok": True}),
        req(2, "qt.properties.get", {"objectId": "l", "name": "text"}),
        res(2, "qt.properties.get", {"value": "x"}),
    ])
    actual = parse_entries([
        req(1, "qt.ui.click", {"objectId": "b"}),
        res(1, "qt.ui.click", {"ok": True}),
        req(2, "qt.properties.get", {"objectId": "l", "name": "text"}),
        entry(dir="err", id=2, method="qt.properties.get", dur_ms=1.0, error="no such object"),
    ])

    divergences = diff_steps(recorded.steps, actual.steps)

    assert len(divergences) == 1
    assert divergences[0].kind == "error"
    assert "no such object" in str(divergences[0].actual)


def test_a_shorter_run_reports_the_step_that_never_happened():
    recorded = parse_entries([
        req(1, "qt.ui.click", {"objectId": "b"}),
        res(1, "qt.ui.click", {"ok": True}),
        req(2, "qt.ui.click", {"objectId": "c"}),
        res(2, "qt.ui.click", {"ok": True}),
    ])
    actual = parse_entries([
        req(1, "qt.ui.click", {"objectId": "b"}),
        res(1, "qt.ui.click", {"ok": True}),
    ])

    divergences = diff_steps(recorded.steps, actual.steps)

    assert len(divergences) == 1
    assert divergences[0].kind == "missing_step"
    assert divergences[0].step == 2


# --- calls replay cannot reproduce ----------------------------------------


def test_unsupported_calls_are_counted_rather_than_dropped():
    """cu.* and chr.* were silently discarded, so a scenario could mean far less
    than it appeared to with nothing saying so."""
    scenario = parse_entries([
        {"dir": "req", "id": 1, "method": "cu.click", "params": {"x": 1, "y": 2}},
        {"dir": "res", "id": 1, "method": "cu.click", "result": {"ok": True}},
        {"dir": "req", "id": 2, "method": "cu.type", "params": {"text": "hi"}},
        {"dir": "res", "id": 2, "method": "cu.type", "result": {"ok": True}},
        {"dir": "req", "id": 3, "method": "cu.click", "params": {"x": 3, "y": 4}},
        {"dir": "res", "id": 3, "method": "cu.click", "result": {"ok": True}},
    ])

    assert scenario.unsupported == {"cu.click": 2, "cu.type": 1}
    assert not scenario.is_replayable


def test_a_mixed_session_reports_what_it_will_not_drive():
    """The dangerous case: replayable, so it runs, but silently drives only part
    of the recorded input and blames the app for the difference."""
    scenario = parse_entries([
        {"dir": "req", "id": 1, "method": "qt.ui.click", "params": {"objectId": "b"}},
        {"dir": "res", "id": 1, "method": "qt.ui.click", "result": {"ok": True}},
        {"dir": "req", "id": 2, "method": "cu.type", "params": {"text": "hi"}},
        {"dir": "res", "id": 2, "method": "cu.type", "result": {"ok": True}},
    ])

    assert scenario.is_replayable
    assert scenario.unsupported == {"cu.type": 1}


def test_a_fully_supported_session_reports_nothing_unsupported():
    scenario = parse_entries([
        {"dir": "req", "id": 1, "method": "qt.ui.click", "params": {"objectId": "b"}},
        {"dir": "res", "id": 1, "method": "qt.ui.click", "result": {"ok": True}},
    ])

    assert scenario.unsupported == {}


def test_setup_calls_are_captured_for_re_issue():
    """Subscriptions are session setup: re-issued so what follows behaves the
    same, never asserted on."""
    scenario = parse_entries([
        {"dir": "req", "id": 1, "method": "qt.signals.subscribe",
         "params": {"objectId": "e", "signal": "textChanged"}},
        {"dir": "res", "id": 1, "method": "qt.signals.subscribe",
         "result": {"subscriptionId": "sub_1"}},
    ])

    setups = [a.method for step in scenario.steps for a in step.setups]
    assert setups == ["qt.signals.subscribe"]
    assert scenario.unsupported == {}


def test_notifications_belong_to_the_step_whose_action_caused_them():
    """In the log a signal arrives between an action's req and its res, but the
    step is only created on the res -- so they used to be filed one step early."""
    scenario = parse_entries([
        {"dir": "req", "id": 1, "method": "qt.ui.click", "params": {"objectId": "b"}},
        {"dir": "ntf", "method": "qtpilot.signalEmitted", "params": {"signal": "clicked"}},
        {"dir": "res", "id": 1, "method": "qt.ui.click", "result": {"ok": True}},
    ])

    assert scenario.steps[0].notifications == []
    assert [m for m, _ in scenario.steps[1].notifications] == ["qtpilot.signalEmitted"]


def test_subscription_ids_are_stripped_as_volatile():
    """sub_1, sub_2 ... come from a per-run counter, so leaving them in meant
    notifications could never compare equal between runs."""
    scenario = parse_entries([
        {"dir": "ntf", "method": "qtpilot.signalEmitted",
         "params": {"signal": "clicked", "subscriptionId": "sub_1"}},
    ])

    _, params = scenario.steps[0].notifications[0]
    assert "subscriptionId" not in params
    assert params["signal"] == "clicked"
