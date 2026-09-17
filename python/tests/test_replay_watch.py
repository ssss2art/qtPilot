"""Tests for watch lists: assertions a scenario carries rather than ones it happened to record."""

from __future__ import annotations

import json

import pytest

from qtpilot.replay import (
    WatchList,
    load_watch_list,
    parse_entries,
    run_scenario,
)


def entry(**kwargs) -> dict:
    base = {"ts": "t"}
    base.update(kwargs)
    return base


def req(rid: int, method: str, params: dict | None = None) -> dict:
    return entry(dir="req", id=rid, method=method, params=params or {})


def res(rid: int, method: str, result) -> dict:
    return entry(dir="res", id=rid, method=method, dur_ms=1.0, result=result)


CLICKS_ONLY = [
    req(1, "qt.ui.click", {"objectId": "btn"}),
    res(1, "qt.ui.click", {"ok": True}),
    req(2, "qt.ui.click", {"objectId": "btn"}),
    res(2, "qt.ui.click", {"ok": True}),
]


class FakeProbe:
    def __init__(self, values=None):
        self.calls: list[tuple[str, dict]] = []
        self.values = values or {}
        self.handlers: list = []

    async def call(self, method, params=None, timeout=None):
        self.calls.append((method, params or {}))
        key = (method, json.dumps(params or {}, sort_keys=True))
        if key in self.values:
            return self.values[key]
        return self.values.get(method, {"ok": True})

    def add_notification_handler(self, h):
        self.handlers.append(h)

    def remove_notification_handler(self, h):
        self.handlers.remove(h)


# --- loading -----------------------------------------------------------------


def test_load_watch_list_reads_targets(tmp_path):
    path = tmp_path / "watch.json"
    path.write_text(json.dumps({
        "watch": [
            {"method": "qt.properties.get", "params": {"objectId": "counter", "name": "text"}},
            {"method": "qt.objects.inspect", "params": {"objectId": "list"}},
        ]
    }))

    watch = load_watch_list(path)

    assert isinstance(watch, WatchList)
    assert len(watch.targets) == 2
    assert watch.targets[0].method == "qt.properties.get"


def test_a_watch_list_may_only_name_observing_methods(tmp_path):
    # A watch list runs after every action. Letting it drive input would silently rewrite the
    # scenario it is supposed to be measuring.
    path = tmp_path / "watch.json"
    path.write_text(json.dumps({"watch": [{"method": "qt.ui.click", "params": {"objectId": "b"}}]}))

    with pytest.raises(ValueError, match="qt.ui.click"):
        load_watch_list(path)


def test_a_malformed_watch_list_is_rejected(tmp_path):
    path = tmp_path / "watch.json"
    path.write_text('{"watch": "not a list"}')

    with pytest.raises(ValueError):
        load_watch_list(path)


# --- recording a baseline ----------------------------------------------------


@pytest.mark.asyncio
async def test_a_watch_list_is_queried_after_every_action():
    # The point of the whole feature: a recording of nothing but clicks has nothing to assert,
    # and replays as a sequence of clicks that cannot fail.
    watch = WatchList.from_targets([("qt.properties.get", {"objectId": "counter", "name": "text"})])
    probe = FakeProbe({"qt.properties.get": {"value": "1"}})

    result = await run_scenario(parse_entries(CLICKS_ONLY), probe, settle=0, watch=watch, record=True)

    queried = [c for c in probe.calls if c[0] == "qt.properties.get"]
    assert len(queried) == 3, "once for the baseline and once after each of the two clicks"
    assert result.passed


@pytest.mark.asyncio
async def test_recording_a_baseline_reports_no_divergence_even_when_values_change():
    # In record mode there is nothing to compare against yet -- the run is producing the golden.
    values = iter([{"value": "0"}, {"value": "1"}, {"value": "2"}])

    class Counting(FakeProbe):
        async def call(self, method, params=None, timeout=None):
            self.calls.append((method, params or {}))
            if method == "qt.properties.get":
                return next(values)
            return {"ok": True}

    watch = WatchList.from_targets([("qt.properties.get", {"objectId": "counter", "name": "text"})])
    result = await run_scenario(parse_entries(CLICKS_ONLY), Counting(), settle=0, watch=watch, record=True)

    assert result.passed
    assert [o.result for o in result.steps[0].observations] == [{"value": "0"}]
    assert [o.result for o in result.steps[1].observations] == [{"value": "1"}]


# --- comparing against a baseline --------------------------------------------


@pytest.mark.asyncio
async def test_a_watched_value_that_changes_is_a_divergence():
    watch = WatchList.from_targets([("qt.properties.get", {"objectId": "counter", "name": "text"})])

    values = iter([{"value": "0"}, {"value": "1"}, {"value": "2"}])

    class Counting(FakeProbe):
        async def call(self, method, params=None, timeout=None):
            self.calls.append((method, params or {}))
            return next(values) if method == "qt.properties.get" else {"ok": True}

    baseline = await run_scenario(parse_entries(CLICKS_ONLY), Counting(), settle=0, watch=watch, record=True)

    # A second run where the second click no longer increments.
    stuck = iter([{"value": "0"}, {"value": "1"}, {"value": "1"}])

    class Stuck(FakeProbe):
        async def call(self, method, params=None, timeout=None):
            self.calls.append((method, params or {}))
            return next(stuck) if method == "qt.properties.get" else {"ok": True}

    # No watch list on the replay: the baseline already carries those observations as its own,
    # so passing it again would query everything twice.
    result = await run_scenario(baseline.as_scenario(), Stuck(), settle=0)

    assert not result.passed
    assert len(result.divergences) == 1
    assert result.divergences[0].step == 2
    assert result.divergences[0].expected == {"value": "2"}
    assert result.divergences[0].actual == {"value": "1"}


@pytest.mark.asyncio
async def test_a_faithful_rerun_against_a_watched_baseline_passes():
    watch = WatchList.from_targets([("qt.properties.get", {"objectId": "c", "name": "text"})])
    probe = FakeProbe({"qt.properties.get": {"value": "same"}})

    baseline = await run_scenario(parse_entries(CLICKS_ONLY), probe, settle=0, watch=watch, record=True)
    result = await run_scenario(baseline.as_scenario(), FakeProbe({"qt.properties.get": {"value": "same"}}), settle=0)

    assert result.passed


@pytest.mark.asyncio
async def test_the_recorded_baseline_round_trips_through_a_log(tmp_path):
    # A baseline is only useful if it can be written out and read back as a scenario.
    from qtpilot.replay import load_scenario

    watch = WatchList.from_targets([("qt.properties.get", {"objectId": "c", "name": "text"})])
    baseline = await run_scenario(
        parse_entries(CLICKS_ONLY), FakeProbe({"qt.properties.get": {"value": "v"}}), settle=0, watch=watch, record=True
    )

    path = tmp_path / "golden.jsonl"
    baseline.write_log(path)

    reloaded = load_scenario(path)

    assert reloaded.is_replayable
    assert [s.action.method for s in reloaded.steps if s.action] == ["qt.ui.click", "qt.ui.click"]
    assert sum(len(s.observations) for s in reloaded.steps) == 3


@pytest.mark.asyncio
async def test_watch_observations_come_after_the_recorded_ones():
    # A scenario may both have recorded observations and carry a watch list. The recorded ones
    # keep their positions, so adding a watch list cannot shift what an existing baseline means.
    entries = CLICKS_ONLY + [
        req(3, "qt.objects.inspect", {"objectId": "panel"}),
        res(3, "qt.objects.inspect", {"visible": True}),
    ]
    watch = WatchList.from_targets([("qt.properties.get", {"objectId": "c", "name": "text"})])
    probe = FakeProbe({"qt.properties.get": {"value": "v"}, "qt.objects.inspect": {"visible": True}})

    result = await run_scenario(parse_entries(entries), probe, settle=0, watch=watch, record=True)

    methods = [o.method for o in result.steps[2].observations]
    assert methods == ["qt.objects.inspect", "qt.properties.get"]


@pytest.mark.asyncio
async def test_a_watch_list_is_a_recording_time_input_not_a_replay_time_one():
    # Once recorded, the watched observations are part of the baseline. Passing the list again on
    # replay would query everything twice and compare a doubled run against a single one.
    watch = WatchList.from_targets([("qt.properties.get", {"objectId": "c", "name": "text"})])

    baseline = await run_scenario(
        parse_entries(CLICKS_ONLY), FakeProbe({"qt.properties.get": {"value": "v"}}), settle=0, watch=watch, record=True
    )

    replay_probe = FakeProbe({"qt.properties.get": {"value": "v"}})
    await run_scenario(baseline.as_scenario(), replay_probe, settle=0)

    queried = [c for c in replay_probe.calls if c[0] == "qt.properties.get"]
    assert len(queried) == 3, "the baseline already asks for these; the list must not be re-applied"
