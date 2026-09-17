"""The scenario format: versioning, round-tripping, and malformed input.

A scenario IS a message log, so the log's shape became a compatibility surface
the moment replay could read one. These tests cover the three things that makes
load-bearing: that a file states which shape it is, that a baseline written today
reads back identically, and that a hostile or truncated file produces a diagnosis
rather than a crash or -- worse -- a scenario that quietly means something else.
"""

from __future__ import annotations

import json
import random

import pytest

from qtpilot.replay import (
    FORMAT_DIR,
    Action,
    SCENARIO_FORMAT,
    Observation,
    ReplayResult,
    Scenario,
    Step,
    load_scenario,
    parse_entries,
)

CLICK = [
    {"dir": "req", "id": 1, "method": "qt.ui.click", "params": {"objectId": "b"}},
    {"dir": "res", "id": 1, "method": "qt.ui.click", "result": {"ok": True}},
    {"dir": "req", "id": 2, "method": "qt.properties.get", "params": {"objectId": "l"}},
    {"dir": "res", "id": 2, "method": "qt.properties.get", "result": {"value": "x"}},
]


def write(tmp_path, entries, name="s.jsonl"):
    path = tmp_path / name
    path.write_text("\n".join(json.dumps(e) for e in entries) + "\n")
    return path


# --- versioning ------------------------------------------------------------


def test_a_log_without_a_header_is_accepted_as_unversioned():
    """MessageLogger writes no header and is a pre-existing contract this work
    does not change, so an absent header must mean "unversioned", not "invalid"."""
    scenario = parse_entries(CLICK)

    assert scenario.format_version == 0
    assert scenario.is_replayable


def test_a_declared_current_format_is_accepted():
    scenario = parse_entries([{"dir": FORMAT_DIR, "format": SCENARIO_FORMAT}, *CLICK])

    assert scenario.format_version == SCENARIO_FORMAT
    assert scenario.is_replayable


def test_a_future_format_is_refused_rather_than_guessed_at():
    """The point of the header. Diffing a file whose semantics have moved gives
    wrong answers confidently; refusing gives a fixable message."""
    with pytest.raises(ValueError) as excinfo:
        parse_entries([{"dir": FORMAT_DIR, "format": SCENARIO_FORMAT + 1}, *CLICK])

    message = str(excinfo.value)
    assert str(SCENARIO_FORMAT + 1) in message
    assert str(SCENARIO_FORMAT) in message


def test_a_nonsense_format_value_is_refused():
    for bad in ("1", 1.5, None, [], {}, True):
        # bool is an int subclass in Python, so True would sneak through a naive
        # isinstance check and be read as format 1.
        if bad is True:
            with pytest.raises(ValueError):
                parse_entries([{"dir": FORMAT_DIR, "format": bad}, *CLICK])
            continue
        with pytest.raises(ValueError):
            parse_entries([{"dir": FORMAT_DIR, "format": bad}, *CLICK])


def test_an_older_format_is_still_readable():
    """Forward-compat only. A file from an older build must keep working."""
    scenario = parse_entries([{"dir": FORMAT_DIR, "format": 0}, *CLICK])

    assert scenario.format_version == 0
    assert scenario.is_replayable


def test_a_header_anywhere_in_the_file_is_honoured():
    """Logs are appended to, so a header need not be the first line."""
    scenario = parse_entries([*CLICK, {"dir": FORMAT_DIR, "format": SCENARIO_FORMAT}])

    assert scenario.format_version == SCENARIO_FORMAT


# --- round-tripping --------------------------------------------------------


def test_a_written_baseline_reads_back_with_the_same_actions(tmp_path):
    scenario = parse_entries(CLICK)
    result = ReplayResult(scenario=scenario, steps=scenario.steps, divergences=[])
    out = tmp_path / "golden.jsonl"

    result.write_log(out)
    back = load_scenario(out)

    assert back.format_version == SCENARIO_FORMAT
    assert [s.action.method for s in back.steps if s.action] == [
        s.action.method for s in scenario.steps if s.action
    ]
    assert [s.action.params for s in back.steps if s.action] == [
        s.action.params for s in scenario.steps if s.action
    ]


def test_a_baseline_survives_two_round_trips(tmp_path):
    """Re-recording a golden must be idempotent, or a scenario drifts every time
    someone refreshes it."""
    scenario = parse_entries(CLICK)
    first = tmp_path / "a.jsonl"
    ReplayResult(scenario=scenario, steps=scenario.steps, divergences=[]).write_log(first)

    once = load_scenario(first)
    second = tmp_path / "b.jsonl"
    ReplayResult(scenario=once, steps=once.steps, divergences=[]).write_log(second)
    twice = load_scenario(second)

    def shape(s: Scenario):
        return [
            (
                st.action.method if st.action else None,
                st.action.params if st.action else None,
                [(o.method, o.params, o.result) for o in st.observations],
            )
            for st in s.steps
        ]

    assert shape(once) == shape(twice)


def test_observation_results_survive_the_round_trip(tmp_path):
    # Needs an action: with no action there is no step 1 to read back into, because
    # observations attach to whichever step is current.
    scenario = Scenario(
        steps=[
            Step(index=0),
            Step(
                index=1,
                action=Action(method="qt.ui.click", params={"objectId": "b"}),
                observations=[
                    Observation(
                        method="qt.properties.get",
                        params={"objectId": "l"},
                        result={"value": "hello", "nested": {"n": 1, "list": [1, 2, 3]}},
                    )
                ],
            ),
        ]
    )
    out = tmp_path / "g.jsonl"
    ReplayResult(scenario=scenario, steps=scenario.steps, divergences=[]).write_log(out)

    back = load_scenario(out)
    got = back.steps[1].observations[0]

    assert got.result == {"value": "hello", "nested": {"n": 1, "list": [1, 2, 3]}}


def test_a_written_baseline_preserves_setup_calls(tmp_path):
    scenario = Scenario(
        steps=[
            Step(
                index=0,
                setups=[
                    Action(
                        method="qt.signals.subscribe",
                        params={"objectId": "main", "signal": "widthChanged"},
                    )
                ],
            ),
            Step(index=1, action=Action(method="qt.ui.click", params={"objectId": "button"})),
        ]
    )
    out = tmp_path / "setup.jsonl"

    ReplayResult(scenario=scenario, steps=scenario.steps, divergences=[]).write_log(out)

    assert load_scenario(out).steps[0].setups == scenario.steps[0].setups


def test_a_written_baseline_preserves_observation_errors(tmp_path):
    scenario = Scenario(
        steps=[
            Step(index=0),
            Step(
                index=1,
                action=Action(method="qt.ui.click", params={"objectId": "button"}),
                observations=[
                    Observation(
                        method="qt.properties.get",
                        params={"objectId": "missing", "name": "visible"},
                        result=None,
                        error="Object not found",
                    )
                ],
            ),
        ]
    )
    out = tmp_path / "errors.jsonl"

    ReplayResult(scenario=scenario, steps=scenario.steps, divergences=[]).write_log(out)

    assert load_scenario(out).steps[1].observations[0].error == "Object not found"


# --- malformed and hostile input ------------------------------------------


def test_a_non_json_line_is_reported_with_its_line_number(tmp_path):
    path = tmp_path / "broken.jsonl"
    path.write_text('{"dir":"req","id":1,"method":"qt.ui.click","params":{}}\nnot json at all\n')

    with pytest.raises(ValueError) as excinfo:
        load_scenario(path)

    assert "line 2" in str(excinfo.value)


def test_an_empty_file_parses_to_a_baseline_only_scenario(tmp_path):
    path = tmp_path / "empty.jsonl"
    path.write_text("")

    scenario = load_scenario(path)

    assert len(scenario.steps) == 1
    assert not scenario.is_replayable


def test_blank_lines_are_ignored(tmp_path):
    path = tmp_path / "gappy.jsonl"
    path.write_text(
        "\n\n" + "\n".join(json.dumps(e) for e in CLICK) + "\n\n   \n"
    )

    assert load_scenario(path).is_replayable


def test_a_non_object_jsonl_line_is_reported_as_malformed(tmp_path):
    path = tmp_path / "non-object.jsonl"
    path.write_text("[]\n")

    with pytest.raises(ValueError, match="line 1.*object"):
        load_scenario(path)


def test_an_orphaned_response_is_rejected_instead_of_becoming_an_action():
    with pytest.raises(ValueError, match="matching request"):
        parse_entries([{"dir": "res", "id": 1, "method": "qt.ui.click", "result": {"ok": True}}])


def test_a_request_response_method_mismatch_is_rejected():
    with pytest.raises(ValueError, match="does not match"):
        parse_entries(
            [
                {"dir": "req", "id": 1, "method": "qt.properties.get", "params": {}},
                {"dir": "res", "id": 1, "method": "qt.ui.click", "result": {"ok": True}},
            ]
        )


@pytest.mark.parametrize(
    "entries",
    [
        pytest.param([{"dir": "req", "id": 1, "method": "qt.ui.click", "params": {}}], id="req-without-res"),
        pytest.param([{"dir": "req", "id": 1, "method": "qt.ui.click"}, {"dir": "res", "id": 1, "method": "qt.ui.click", "result": {}}], id="req-without-params"),
        pytest.param([{"dir": "ntf"}], id="notification-without-method"),
        pytest.param([{"dir": "wat", "id": 1}], id="unknown-direction"),
        pytest.param([{"id": 1, "method": "qt.ui.click"}], id="no-direction"),
        pytest.param([{}], id="empty-entry"),
        pytest.param([{"dir": "req", "method": "qt.ui.click", "params": {}}], id="req-without-id"),
        pytest.param([{"dir": "req", "id": None, "method": "qt.ui.click", "params": None}], id="null-id-and-params"),
    ],
)
def test_structurally_broken_entries_do_not_crash_the_parser(entries):
    """A log is produced by a long-running process that can be killed mid-write,
    appended to across sessions, and edited by hand. The parser meeting something
    it did not expect must produce a scenario -- possibly an empty one -- not a
    traceback from inside a comprehension.
    """
    scenario = parse_entries(entries)

    assert scenario.steps, "the baseline step disappeared"


def test_duplicate_request_ids_do_not_mispair_params():
    """MessageLogger opens its file in append mode and request ids restart per
    session, so one file can hold two sessions both numbering from 1."""
    entries = [
        {"dir": "req", "id": 1, "method": "qt.ui.click", "params": {"objectId": "first"}},
        {"dir": "res", "id": 1, "method": "qt.ui.click", "result": {"ok": True}},
        {"dir": "req", "id": 1, "method": "qt.ui.click", "params": {"objectId": "second"}},
        {"dir": "res", "id": 1, "method": "qt.ui.click", "result": {"ok": True}},
    ]

    scenario = parse_entries(entries)
    targets = [s.action.params.get("objectId") for s in scenario.steps if s.action]

    assert targets == ["first", "second"], f"params were mispaired across sessions: {targets}"


def test_an_abandoned_request_does_not_poison_a_later_one():
    """A request whose response never landed (process killed) leaves an entry in
    the pending map. A later session reusing that id must not inherit its params."""
    entries = [
        {"dir": "req", "id": 7, "method": "qt.ui.click", "params": {"objectId": "abandoned"}},
        # no res -- the process died here
        {"dir": "req", "id": 7, "method": "qt.ui.click", "params": {"objectId": "wanted"}},
        {"dir": "res", "id": 7, "method": "qt.ui.click", "result": {"ok": True}},
    ]

    scenario = parse_entries(entries)
    targets = [s.action.params.get("objectId") for s in scenario.steps if s.action]

    assert targets == ["wanted"], f"an abandoned request supplied the params: {targets}"


def test_deeply_nested_values_do_not_blow_the_stack():
    """qt.objects.tree returns a tree as deep as the widget hierarchy, and
    normalise() recurses over it."""
    deep: dict = {"value": "leaf"}
    for _ in range(200):
        deep = {"children": [deep]}

    scenario = parse_entries([
        {"dir": "req", "id": 1, "method": "qt.objects.tree", "params": {}},
        {"dir": "res", "id": 1, "method": "qt.objects.tree", "result": deep},
    ])

    assert scenario.steps[0].observations


def test_unicode_and_control_characters_survive(tmp_path):
    """Recorded text comes from a real UI: emoji, RTL, combining marks, tabs."""
    awkward = "naïve → 日本語 🎛 ‏ rtl\ttab"
    entries = [
        {"dir": "req", "id": 1, "method": "qt.ui.sendKeys", "params": {"objectId": "e", "text": awkward}},
        {"dir": "res", "id": 1, "method": "qt.ui.sendKeys", "result": {"ok": True}},
    ]
    path = write(tmp_path, entries)

    scenario = load_scenario(path)

    assert scenario.steps[1].action.params["text"] == awkward


# --- fuzz ------------------------------------------------------------------


def _random_value(rng: random.Random, depth: int = 0):
    kinds = ["str", "int", "float", "bool", "none"]
    if depth < 3:
        kinds += ["dict", "list"]
    kind = rng.choice(kinds)
    if kind == "str":
        return rng.choice(["", "x", "a~1", "MainWindow/central/QLabel~2", "…<truncated 900c>", "🎛"])
    if kind == "int":
        return rng.randint(-(2**40), 2**40)
    if kind == "float":
        return rng.choice([0.0, -1.5, 1e308])
    if kind == "bool":
        return rng.choice([True, False])
    if kind == "none":
        return None
    if kind == "dict":
        keys = ["id", "objectId", "value", "ts", "dur_ms", "meta", "children", "subscriptionId"]
        return {rng.choice(keys): _random_value(rng, depth + 1) for _ in range(rng.randint(0, 4))}
    return [_random_value(rng, depth + 1) for _ in range(rng.randint(0, 4))]


def _random_entry(rng: random.Random) -> dict:
    entry: dict = {}
    if rng.random() < 0.95:
        entry["dir"] = rng.choice(["req", "res", "err", "ntf", "meta", "bogus"])
    if rng.random() < 0.9:
        entry["id"] = rng.choice([1, 2, 3, None, "str-id"])
    if rng.random() < 0.9:
        entry["method"] = rng.choice([
            "qt.ui.click", "qt.ui.sendKeys", "qt.properties.get", "qt.objects.tree",
            "qt.signals.subscribe", "cu.click", "chr.click", "qt.brand.new", "",
        ])
    for key in ("params", "result"):
        if rng.random() < 0.7:
            entry[key] = _random_value(rng)
    if entry.get("dir") == "meta":
        entry["format"] = rng.choice([0, 1, 2, 99, "1", None])
    if rng.random() < 0.2:
        entry["error"] = "boom"
    return entry


@pytest.mark.parametrize("seed", range(60))
def test_fuzzed_logs_either_parse_or_raise_valueerror(seed):
    """The parser's contract under garbage: a Scenario, or a ValueError naming the
    problem. Never an AttributeError/TypeError/KeyError from inside the walk, and
    never a hang. Seeded so a failure is reproducible from the test id alone.
    """
    rng = random.Random(seed)
    entries = [_random_entry(rng) for _ in range(rng.randint(0, 25))]

    try:
        scenario = parse_entries(entries)
    except ValueError:
        return  # A diagnosis is a valid outcome.

    assert scenario.steps
    assert isinstance(scenario.unsupported, dict)
    assert isinstance(scenario.format_version, int)
    # Anything classified as an action must be drivable: a method and a dict of params.
    for step in scenario.steps:
        if step.action is not None:
            assert step.action.method
            assert isinstance(step.action.params, dict)
        for observation in step.observations:
            assert observation.method
            assert isinstance(observation.params, dict)
