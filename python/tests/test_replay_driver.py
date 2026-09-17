"""Tests for driving a recorded scenario against a probe."""

from __future__ import annotations

import asyncio

import pytest

from qtpilot.connection import ProbeError
from qtpilot.replay import ReplayResult, parse_entries, run_scenario


def entry(**kwargs) -> dict:
    base = {"ts": "2026-03-06T14:23:01.234Z"}
    base.update(kwargs)
    return base


def req(rid: int, method: str, params: dict | None = None) -> dict:
    return entry(dir="req", id=rid, method=method, params=params or {})


def res(rid: int, method: str, result) -> dict:
    return entry(dir="res", id=rid, method=method, dur_ms=1.0, result=result)


def ntf(method: str, params: dict) -> dict:
    return entry(dir="ntf", method=method, params=params)


class FakeProbe:
    """Records what a replay drives, and answers from a scripted table.

    Deliberately not a mock of ProbeConnection's whole surface: the driver is meant to need
    nothing but call() and the notification handler hooks, and a fake this small is how that
    stays true.
    """

    def __init__(self, results: dict | None = None, errors: dict | None = None) -> None:
        self.calls: list[tuple[str, dict]] = []
        self.results = results or {}
        self.errors = errors or {}
        self.handlers: list = []
        self.emit_on: dict[str, list[tuple[str, dict]]] = {}

    async def call(self, method: str, params: dict | None = None, timeout=None):
        self.calls.append((method, params or {}))
        if method in self.errors:
            raise ProbeError(self.errors[method])
        for signal_method, signal_params in self.emit_on.get(method, []):
            for handler in list(self.handlers):
                handler(signal_method, signal_params)
        return self.results.get(method, {"ok": True})

    def add_notification_handler(self, handler) -> None:
        self.handlers.append(handler)

    def remove_notification_handler(self, handler) -> None:
        if handler in self.handlers:
            self.handlers.remove(handler)


def scenario_with_one_click():
    return parse_entries([
        req(1, "qt.ui.click", {"objectId": "btn"}),
        res(1, "qt.ui.click", {"ok": True}),
        req(2, "qt.properties.get", {"objectId": "label", "name": "text"}),
        res(2, "qt.properties.get", {"value": "clicked"}),
    ])


@pytest.mark.asyncio
async def test_actions_are_driven_in_order():
    scenario = parse_entries([
        req(1, "qt.ui.click", {"objectId": "first"}),
        res(1, "qt.ui.click", {"ok": True}),
        req(2, "qt.ui.sendKeys", {"objectId": "field", "keys": "hi"}),
        res(2, "qt.ui.sendKeys", {"ok": True}),
    ])
    probe = FakeProbe()

    await run_scenario(scenario, probe, settle=0)

    assert probe.calls == [
        ("qt.ui.click", {"objectId": "first"}),
        ("qt.ui.sendKeys", {"objectId": "field", "keys": "hi"}),
    ]


@pytest.mark.asyncio
async def test_observations_are_reissued_with_the_recorded_parameters():
    probe = FakeProbe(results={"qt.properties.get": {"value": "clicked"}})

    await run_scenario(scenario_with_one_click(), probe, settle=0)

    assert ("qt.properties.get", {"objectId": "label", "name": "text"}) in probe.calls


@pytest.mark.asyncio
async def test_a_faithful_replay_reports_no_divergence():
    probe = FakeProbe(results={"qt.properties.get": {"value": "clicked"}})

    result = await run_scenario(scenario_with_one_click(), probe, settle=0)

    assert isinstance(result, ReplayResult)
    assert result.divergences == []
    assert result.passed


@pytest.mark.asyncio
async def test_a_changed_observation_fails_the_replay():
    probe = FakeProbe(results={"qt.properties.get": {"value": "nothing happened"}})

    result = await run_scenario(scenario_with_one_click(), probe, settle=0)

    assert not result.passed
    assert len(result.divergences) == 1
    assert result.divergences[0].method == "qt.properties.get"


@pytest.mark.asyncio
async def test_an_error_on_an_observation_is_a_divergence_not_a_crash():
    # The application changing so much that an inspected object no longer exists is a result the
    # report should carry, not an exception that loses every other step.
    probe = FakeProbe(errors={"qt.properties.get": "no such object"})

    result = await run_scenario(scenario_with_one_click(), probe, settle=0)

    assert not result.passed
    assert result.divergences[0].kind == "error"
    assert "no such object" in str(result.divergences[0].actual)


@pytest.mark.asyncio
async def test_an_error_on_an_action_aborts_the_run():
    # Every later step assumes the ones before it happened. Carrying on after a click that never
    # landed would report a cascade of differences that are all the same failure.
    scenario = parse_entries([
        req(1, "qt.ui.click", {"objectId": "gone"}),
        res(1, "qt.ui.click", {"ok": True}),
        req(2, "qt.ui.click", {"objectId": "second"}),
        res(2, "qt.ui.click", {"ok": True}),
    ])
    probe = FakeProbe(errors={"qt.ui.click": "no such object"})

    result = await run_scenario(scenario, probe, settle=0)

    assert not result.passed
    assert result.aborted_at == 1
    assert "no such object" in result.abort_reason
    assert probe.calls == [("qt.ui.click", {"objectId": "gone"})]


@pytest.mark.asyncio
async def test_notifications_are_captured_against_the_step_that_caused_them():
    scenario = parse_entries([
        req(1, "qt.ui.click", {"objectId": "btn"}),
        res(1, "qt.ui.click", {"ok": True}),
        ntf("qtpilot.signalEmitted", {"objectId": "btn", "signal": "clicked"}),
    ])
    probe = FakeProbe()
    probe.emit_on["qt.ui.click"] = [("qtpilot.signalEmitted", {"objectId": "btn", "signal": "clicked"})]

    result = await run_scenario(scenario, probe, settle=0)

    assert result.passed
    assert result.steps[1].notifications == [
        ("qtpilot.signalEmitted", {"objectId": "btn", "signal": "clicked"})
    ]


@pytest.mark.asyncio
async def test_a_signal_that_stops_being_emitted_is_a_divergence():
    scenario = parse_entries([
        req(1, "qt.ui.click", {"objectId": "btn"}),
        res(1, "qt.ui.click", {"ok": True}),
        ntf("qtpilot.signalEmitted", {"objectId": "btn", "signal": "clicked"}),
    ])
    probe = FakeProbe()  # emits nothing

    result = await run_scenario(scenario, probe, settle=0)

    assert not result.passed
    assert result.divergences[0].kind == "notification"


@pytest.mark.asyncio
async def test_the_notification_handler_is_removed_afterwards():
    probe = FakeProbe()

    await run_scenario(scenario_with_one_click(), probe, settle=0)

    assert probe.handlers == []


@pytest.mark.asyncio
async def test_the_notification_handler_is_removed_even_when_a_step_raises():
    # Leaving a handler attached would keep feeding a dead run's collector for the rest of the
    # session, so the cleanup has to survive the abort path too.
    probe = FakeProbe(errors={"qt.ui.click": "boom"})

    await run_scenario(scenario_with_one_click(), probe, settle=0)

    assert probe.handlers == []


@pytest.mark.asyncio
async def test_a_scenario_with_nothing_to_drive_is_refused():
    # A level-1 log parses to a baseline and nothing else. Running it would report success
    # without having tested anything, which is worse than saying so.
    scenario = parse_entries([entry(dir="mcp_in", tool="qt_ui_click", args={})])

    with pytest.raises(ValueError, match="nothing to replay"):
        await run_scenario(scenario, FakeProbe(), settle=0)


@pytest.mark.asyncio
async def test_the_baseline_step_is_observed_before_anything_is_driven():
    scenario = parse_entries([
        req(1, "qt.objects.tree", {}),
        res(1, "qt.objects.tree", {"root": "app"}),
        req(2, "qt.ui.click", {"objectId": "btn"}),
        res(2, "qt.ui.click", {"ok": True}),
    ])
    probe = FakeProbe(results={"qt.objects.tree": {"root": "app"}})

    result = await run_scenario(scenario, probe, settle=0)

    assert probe.calls[0] == ("qt.objects.tree", {})
    assert result.passed


@pytest.mark.asyncio
async def test_settle_gives_notifications_time_to_arrive():
    # Signals are delivered asynchronously, so a replay that asserted the instant a call returned
    # would miss them and report a divergence that is really a race.
    scenario = parse_entries([
        req(1, "qt.ui.click", {"objectId": "btn"}),
        res(1, "qt.ui.click", {"ok": True}),
        ntf("qtpilot.signalEmitted", {"objectId": "btn", "signal": "clicked"}),
    ])

    class LateProbe(FakeProbe):
        async def call(self, method, params=None, timeout=None):
            result = await super().call(method, params, timeout)
            if method == "qt.ui.click":
                async def later():
                    await asyncio.sleep(0.01)
                    for handler in list(self.handlers):
                        handler("qtpilot.signalEmitted", {"objectId": "btn", "signal": "clicked"})
                asyncio.get_running_loop().create_task(later())
            return result

    assert (await run_scenario(scenario, LateProbe(), settle=0.05)).passed
    assert not (await run_scenario(scenario, LateProbe(), settle=0)).passed


# --- the request deadline --------------------------------------------------


class TimeoutRecordingProbe:
    """Records exactly how the driver passed (or did not pass) a timeout.

    ProbeConnection.call distinguishes three things: an explicit float, an
    explicit None ("wait forever"), and the argument being absent ("use the
    default deadline"). A fake with ``timeout=None`` in its signature cannot tell
    the last two apart, which is how the driver came to disable the only timeout
    the transport has.
    """

    def __init__(self) -> None:
        self.timeout_args: list = []
        self.handlers: list = []

    async def call(self, method, params=None, *args, **kwargs):
        if args:
            self.timeout_args.append(("positional", args[0]))
        elif "timeout" in kwargs:
            self.timeout_args.append(("keyword", kwargs["timeout"]))
        else:
            self.timeout_args.append(("absent", None))
        return {}

    def add_notification_handler(self, handler):
        self.handlers.append(handler)

    def remove_notification_handler(self, handler):
        self.handlers.remove(handler)


CLICK = [
    {"dir": "req", "id": 1, "method": "qt.ui.click", "params": {"objectId": "b"}},
    {"dir": "res", "id": 1, "method": "qt.ui.click", "result": {"ok": True}},
    {"dir": "req", "id": 2, "method": "qt.properties.get", "params": {"objectId": "l"}},
    {"dir": "res", "id": 2, "method": "qt.properties.get", "result": {"value": "x"}},
]


@pytest.mark.asyncio
async def test_no_timeout_means_inherit_the_default_not_wait_forever():
    """The driver must not hand ProbeConnection an explicit None.

    ProbeConnection.call's default is a sentinel; None selects its unbounded
    branch. Passing None meant a replay against an application that wedged hung
    until CI's global kill rather than failing -- reopening the hazard the
    request-timeout work closed.
    """
    probe = TimeoutRecordingProbe()
    await run_scenario(parse_entries(CLICK), probe, settle=0)

    assert probe.timeout_args, "the driver made no calls"
    assert all(
        kind == "absent" for kind, _ in probe.timeout_args
    ), f"driver supplied a timeout it was never given: {probe.timeout_args}"


@pytest.mark.asyncio
async def test_an_explicit_timeout_is_passed_through():
    probe = TimeoutRecordingProbe()
    await run_scenario(parse_entries(CLICK), probe, settle=0, timeout=2.5)

    assert probe.timeout_args
    assert all(value == 2.5 for _, value in probe.timeout_args), probe.timeout_args
