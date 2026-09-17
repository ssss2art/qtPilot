"""Deterministic replay of a recorded qtPilot session.

A message log written by :class:`~qtpilot.message_logger.MessageLogger` is a transcript, not a
test: it records what was driven and what came back, interleaved, with timings and request ids
that differ on every run. This module turns one into something re-runnable.

The split that makes it work is that a session divides cleanly into two kinds of call. A few
methods *change* the application -- clicks, keystrokes, property writes, method invocations --
and everything else only *observes* it. Re-driving the first kind against a fresh application and
comparing the second kind is an assertion about behaviour, and it needs no bespoke test written
by hand for each flow.

Deliberately narrow: this reads and diffs, and knows nothing about connections. Driving a probe
belongs to the caller, so a scenario can be checked against a live application, against another
recording, or against itself in a unit test.
"""

from __future__ import annotations

import asyncio
import json
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable

from qtpilot.connection import ProbeError

# Calls that change the application. These are what a replay re-drives.
MUTATING_METHODS: frozenset[str] = frozenset({
    "qt.ui.click",
    "qt.ui.clickItem",
    "qt.ui.sendKeys",
    "qt.properties.set",
    "qt.methods.invoke",
})

# Calls whose results describe the application, and so are worth asserting on.
#
# An allow-list rather than "everything that is not mutating": qt.ping and qt.version describe the
# harness, the qt.names.* and qt.signals.* families describe the session's own bookkeeping, and
# qt.ui.screenshot returns image bytes that belong in a visual golden. None of them say anything
# about the application under test, and asserting on them would fail runs for reasons a reader
# cannot act on.
OBSERVING_METHODS: frozenset[str] = frozenset({
    "qt.objects.tree",
    "qt.objects.inspect",
    "qt.objects.search",
    "qt.properties.get",
    "qt.models.list",
    "qt.models.data",
    "qt.models.search",
    "qt.ui.geometry",
    "qt.ui.hitTest",
})

# Calls that set up the SESSION rather than drive or describe the application: signal
# subscriptions, event capture, and the symbolic name map. They are re-issued on replay because
# what follows depends on them, but their results are never compared -- a subscription id or a
# "registered 12 names" count says nothing about the application.
#
# Without this, a level-3 recording failed against a correct application 100% of the time: the
# subscription was never re-established, so no notification arrived during replay while the
# recording held one per emission, and every one was reported as missing. docs/REPLAY.md
# recommends level 3 for exactly the case that could not work.
SETUP_METHODS: frozenset[str] = frozenset({
    "qt.signals.subscribe",
    "qt.signals.unsubscribe",
    "qt.signals.setLifecycle",
    "qt.events.start",
    "qt.events.stop",
    "qt.names.register",
    "qt.names.load",
    "qt.names.unregister",
})

# Timing, present at every depth and different in every run. "timestamp" is the probe's own
# epoch-millisecond stamp inside a result's meta block, and is every bit as volatile as the
# entry's ts -- it only shows up once a scenario is run against a real log rather than a fixture.
# "subscriptionId" is the probe's own per-run counter (signal_monitor.cpp mints sub_1, sub_2 ...
# from a monotonic int). It rides on every notification, so leaving it in meant the recorded and
# replayed notifications could never compare equal even once subscriptions were re-established.
# The scenario format this build writes and understands.
#
# A scenario IS a message log -- the same shape MessageLogger produces -- which is deliberate:
# a hand-captured session and a recorded baseline are one kind of file and one tool reads both.
# The cost is that the log's shape became a compatibility surface the moment replay could read
# it, and nothing said which shape a given file was.
#
# A baseline written by --record now carries a header line. A log captured by MessageLogger does
# NOT, because the logger is a pre-existing contract this work does not change -- so an absent
# header means "unversioned", is accepted, and is not an error. What is an error is a header
# claiming a format this build does not know how to read: better to refuse than to diff a file
# whose meaning has moved.
SCENARIO_FORMAT = 1

# The marker line. "dir" keeps it inside the log's existing entry shape, so a reader that
# predates versioning skips it as an unrecognised direction rather than choking on it -- which
# is why this is a new `dir` value and not a new top-level key.
FORMAT_DIR = "meta"

VOLATILE_KEYS: frozenset[str] = frozenset({"ts", "dur_ms", "timestamp", "subscriptionId"})

# The JSON-RPC request id. Stripped only from the top level of an entry: nested "id" keys are
# object identifiers -- the single most meaningful thing a result carries -- and removing those
# would leave a diff unable to tell one widget from another.
REQUEST_ID_KEY = "id"

# What MessageLogger._truncate leaves behind when a value was too large to log.
_TRUNCATED = re.compile(r"\.\.\.<truncated \d+c>$|^<image:\d+b>$")

# Keys whose string values are object identifiers. The probe emits "id" on tree nodes
# (object_id.cpp:579) and "objectId" on search/inspect/property entries
# (native_mode_api.cpp:361). Masking is confined to these because it used to run over every
# string at every depth, which meant an ordinary label or model cell reading "user~1" was
# rewritten too -- silently equal on both sides, so a real difference could pass.
ID_KEYS: frozenset[str] = frozenset({"id", "objectId", "objectIds", "parentId"})

# The collision suffix the registry appends when two objects would otherwise generate the same
# id (object_registry.cpp allocateUniqueIdLocked). The counter is monotonic and depends on the
# order objects were constructed, so it is not stable between runs.
#
# It is appended to a WHOLE id, and a real id is a "/"-joined path whose segments may carry a
# "#N" sibling index -- "MainWindow/centralWidget/formTab/QLabel~2", not a bare class name. The
# previous pattern was anchored to `^Class~N$`, so it never matched anything the probe emits for
# a nested object, and the one case it did match it corrupted. Hence [^~]+ across the whole
# string rather than an identifier at the start.
_GENERATED_HANDLE = re.compile(r"^(?P<base>[^~]+)~\d+$")


def _is_truncated(value: Any) -> bool:
    """Whether a logged value is a placeholder rather than the real thing."""
    return isinstance(value, str) and _TRUNCATED.search(value) is not None


def normalise(
    value: Any, *, top_level: bool = True, mask_handles: bool = True, key: str | None = None
) -> Any:
    """Strip the fields that differ between two runs of the same session.

    :param value: A log entry, or any value nested inside one.
    :param top_level: False for values already inside an entry.
    :param mask_handles: Whether to blank the registry's ``~N`` collision suffix. Must be False
        for anything that will be sent back to the probe -- see the note below.
    :param key: The dict key ``value`` was found under, used to confine masking to identifiers.
    :return: A copy without timing, and without the request id at the outermost level.

    .. note:: Timing is stripped at every depth, because a diff that reported a nested
       ``dur_ms`` would be reporting the clock. The request id is stripped only at the top:
       deeper down, ``id`` names an object rather than a call.

    .. warning:: ``mask_handles`` exists because this function is applied to request params as
       well as results, and request params are re-driven verbatim. Masking them meant replay
       asked the probe for an objectId containing a literal ``~*`` (which cannot resolve) and
       typed ``~*`` into the application in place of recorded text. Masking is a comparison
       concern; it must never reach the drive path.
    """
    if isinstance(value, dict):
        drop = set(VOLATILE_KEYS)
        if top_level:
            drop.add(REQUEST_ID_KEY)
        return {
            k: normalise(v, top_level=False, mask_handles=mask_handles, key=k)
            for k, v in value.items()
            if k not in drop
        }
    if isinstance(value, list):
        return [
            normalise(item, top_level=False, mask_handles=mask_handles, key=key) for item in value
        ]
    if isinstance(value, str) and mask_handles and key in ID_KEYS:
        return _GENERATED_HANDLE.sub(r"\g<base>~*", value)
    return value


def _equivalent(expected: Any, actual: Any) -> bool:
    """Compare two normalised values, treating logged placeholders as wildcards.

    .. note:: A truncated value is not what the application returned, so holding a replay to it
       would fail every run over a difference the logger introduced.
    """
    if _is_truncated(expected) or _is_truncated(actual):
        return True
    if isinstance(expected, dict) and isinstance(actual, dict):
        if expected.keys() != actual.keys():
            return False
        return all(_equivalent(expected[k], actual[k]) for k in expected)
    if isinstance(expected, list) and isinstance(actual, list):
        return len(expected) == len(actual) and all(
            _equivalent(e, a) for e, a in zip(expected, actual)
        )
    return expected == actual


@dataclass(frozen=True)
class Action:
    """A call that changes the application, and so is re-driven on replay."""

    method: str
    params: dict


@dataclass(frozen=True)
class Observation:
    """A call that describes the application, and so is asserted on."""

    method: str
    params: dict
    result: Any
    error: str | None = None


@dataclass
class Step:
    """One driven action and everything observed before the next one.

    .. note:: Step 0 has no action. It holds the baseline -- whatever was inspected before the
       session drove anything -- so a scenario can assert on the state it started from.
    """

    index: int
    action: Action | None = None
    # Session setup recorded during this step (subscriptions, name-map loads). Re-issued on
    # replay so what follows behaves the same, never asserted on -- see SETUP_METHODS.
    setups: list[Action] = field(default_factory=list)
    observations: list[Observation] = field(default_factory=list)
    notifications: list[tuple[str, dict]] = field(default_factory=list)


@dataclass
class Scenario:
    """A parsed session: an ordered sequence of steps."""

    steps: list[Step]
    source: str = "<memory>"
    # Wire calls the parser could not classify, as {method: count}. Previously these fell off
    # the end of parse_entries' if-chain and vanished, so a session recorded in computer_use
    # mode (every cu.* call) parsed to zero actions and reported "record at level 2 or above"
    # about a log that already was level 2 -- and a MIXED session was worse: it replayed, drove
    # none of the cu.* input, and reported the resulting state differences as application
    # divergences.
    unsupported: dict[str, int] = field(default_factory=dict)
    # The format declared by the file's header, or 0 for an unversioned capture.
    format_version: int = 0

    @property
    def is_replayable(self) -> bool:
        """Whether there is anything to drive.

        .. note:: A level-1 log records tool names but no wire traffic, so it parses into a
           baseline and nothing else. Such a scenario would pass unconditionally, which is worse
           than failing -- hence an explicit answer rather than an empty run.
        """
        return any(step.action is not None for step in self.steps)


@dataclass(frozen=True)
class WatchTarget:
    """One observing call issued after every action."""

    method: str
    params: dict


@dataclass
class WatchList:
    """Observations a scenario carries, rather than ones it happened to record.

    .. note:: Exists because a replay can only assert on what the recording looked at, and an
       operator driving an application clicks far more readily than they inspect. A session of
       nothing but clicks replays as a sequence of clicks that cannot fail. A watch list is
       queried after every action, so the assertions are a property of the scenario instead of
       of whoever recorded it.
    """

    targets: list[WatchTarget] = field(default_factory=list)

    @classmethod
    def from_targets(cls, targets: Iterable[tuple[str, dict]]) -> WatchList:
        """Build from ``(method, params)`` pairs."""
        return cls([WatchTarget(method=m, params=p) for m, p in targets])


def load_watch_list(path: str | Path) -> WatchList:
    """Read a watch list.

    :param path: A JSON file of the form ``{"watch": [{"method": ..., "params": {...}}]}``.
    :return: The parsed list.
    :raises ValueError: If the file is malformed, or names a method that is not an observing one.

    .. note:: Only observing methods are allowed. A watch list runs after every action, so
       letting it drive input would silently rewrite the scenario it is supposed to be measuring.
    """
    try:
        raw = json.loads(Path(path).read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise ValueError(f"{path}: not valid JSON: {exc}") from exc

    entries = raw.get("watch") if isinstance(raw, dict) else None
    if not isinstance(entries, list):
        raise ValueError(f'{path}: expected an object with a "watch" list')

    targets: list[WatchTarget] = []
    for index, item in enumerate(entries):
        if not isinstance(item, dict) or "method" not in item:
            raise ValueError(f"{path}: watch[{index}] needs a \"method\"")

        method = item["method"]
        if method not in OBSERVING_METHODS:
            raise ValueError(
                f"{path}: watch[{index}] names {method}, which is not an observing method. "
                f"A watch list runs after every action and must not change the application. "
                f"Allowed: {', '.join(sorted(OBSERVING_METHODS))}"
            )

        targets.append(WatchTarget(method=method, params=item.get("params", {})))

    return WatchList(targets)


@dataclass(frozen=True)
class Divergence:
    """One way a replay differed from what was recorded."""

    step: int
    kind: str  # "observation", "notification", "error", "missing_step"
    method: str
    expected: Any
    actual: Any

    def __str__(self) -> str:
        return f"step {self.step}: {self.kind} in {self.method}: expected {self.expected!r}, got {self.actual!r}"


def parse_entries(entries: Iterable[dict]) -> Scenario:
    """Split a sequence of log entries into steps.

    :param entries: Log entries in the order they were written.
    :return: The parsed scenario. Always has at least the baseline step.
    """
    steps: list[Step] = [Step(index=0)]
    pending: dict[Any, dict] = {}
    # Notifications that arrived while a mutating call was in flight. In the log the order is
    # req(click) ... ntf ... res(click), but the step an action owns is only created on its res,
    # so appending to steps[-1] filed every signal an action caused under the PREVIOUS step. On
    # replay the same signal is collected after the action and attributed to the current one, so
    # recorded and replayed attribution differed by one for every action that emitted anything.
    in_flight: list[tuple[str, dict]] = []
    mutating_in_flight = 0
    unsupported: dict[str, int] = {}
    # 0 means "no header" -- a log captured by MessageLogger, which does not write one.
    fmt = 0

    for raw in entries:
        if not isinstance(raw, dict):
            raise ValueError("scenario entry must be a JSON object")
        direction = raw.get("dir")

        if direction == FORMAT_DIR:
            declared = raw.get("format")
            # `isinstance(True, int)` is True in Python, so a header of {"format": true}
            # would otherwise be read as format 1 -- a corrupt file silently claiming to be
            # the current version is the one outcome the header exists to prevent.
            if isinstance(declared, bool) or not isinstance(declared, int):
                raise ValueError(
                    f"scenario header declares format {declared!r}, which is not a version number"
                )
            if declared > SCENARIO_FORMAT:
                raise ValueError(
                    f"scenario was written in format {declared}, but this build understands "
                    f"up to {SCENARIO_FORMAT}. Upgrade qtpilot, or re-record the scenario."
                )
            fmt = declared
            continue

        if direction == "req":
            pending[raw.get("id")] = raw
            if raw.get("method", "") in MUTATING_METHODS:
                mutating_in_flight += 1
            continue

        if direction == "ntf":
            entry = (raw.get("method", ""), normalise(raw.get("params", {})))
            if mutating_in_flight:
                in_flight.append(entry)
            else:
                steps[-1].notifications.append(entry)
            continue

        if direction not in ("res", "err"):
            continue

        method = raw.get("method", "")
        request_id = raw.get("id")
        if request_id not in pending:
            raise ValueError(f"response id {request_id!r} has no matching request")
        request = pending.pop(request_id)
        request_method = request.get("method", "")
        if method != request_method:
            raise ValueError(
                f"response method {method!r} does not match request method {request_method!r} "
                f"for id {request_id!r}"
            )

        # A recorded call whose params are not an object cannot be reconstructed: replay would
        # hand probe.call() a float or a list where the wire format requires a JSON object.
        # Counted so it is visible rather than driven or silently dropped. Only reachable
        # through a corrupted or hand-edited log, which is exactly when a parser should refuse
        # to invent something plausible.
        raw_params = request.get("params", {})
        if raw_params is not None and not isinstance(raw_params, dict):
            if method:
                unsupported[method] = unsupported.get(method, 0) + 1
            continue
        # mask_handles=False: these params are re-sent to the probe verbatim on replay.
        params = normalise(raw_params if raw_params is not None else {}, mask_handles=False)

        if method in MUTATING_METHODS:
            # The action's own result is not an observation: re-driving it produces a fresh one,
            # and asserting on it would assert that the driver worked, not that the app behaved.
            mutating_in_flight = max(0, mutating_in_flight - 1)
            step = Step(index=len(steps), action=Action(method=method, params=params))
            # Whatever this action emitted belongs to the step it creates, not the one before it.
            step.notifications.extend(in_flight)
            in_flight.clear()
            steps.append(step)
            continue

        if method in SETUP_METHODS:
            # Only successful setup is worth re-issuing; a subscription that failed during
            # recording produced no notifications to reproduce either.
            if direction == "res":
                steps[-1].setups.append(Action(method=method, params=params))
            continue

        if method in OBSERVING_METHODS:
            steps[-1].observations.append(
                Observation(
                    method=method,
                    params=params,
                    result=normalise(raw.get("result")) if direction == "res" else None,
                    error=raw.get("error") if direction == "err" else None,
                )
            )
            continue

        # Anything left is a call replay does not know how to reproduce -- the cu.* and chr.*
        # families, or a qt.* method added to the probe since this list was written. Counted
        # rather than dropped, so --inspect can say what will not be re-driven instead of a
        # scenario quietly meaning less than it appears to.
        if method:
            unsupported[method] = unsupported.get(method, 0) + 1

    steps[-1].notifications.extend(in_flight)
    return Scenario(steps=steps, unsupported=unsupported, format_version=fmt)


def load_scenario(path: str | Path) -> Scenario:
    """Read a JSON Lines message log.

    :param path: The log file.
    :return: The parsed scenario, tagged with where it came from.
    :raises ValueError: If a line is not valid JSON, naming the line so it can be found.
    """
    entries: list[dict] = []
    text = Path(path).read_text(encoding="utf-8")

    for number, line in enumerate(text.splitlines(), start=1):
        if not line.strip():
            continue
        try:
            entry = json.loads(line)
        except json.JSONDecodeError as exc:
            raise ValueError(f"{path}: line {number} is not valid JSON: {exc}") from exc
        if not isinstance(entry, dict):
            raise ValueError(f"{path}: line {number} must be a JSON object")
        entries.append(entry)

    scenario = parse_entries(entries)
    scenario.source = str(path)
    return scenario


def _diff_observations(expected: Step, actual: Step) -> list[Divergence]:
    """Compare one step's observations against another's."""
    divergences: list[Divergence] = []

    for index, want in enumerate(expected.observations):
        got = actual.observations[index] if index < len(actual.observations) else None

        if got is None:
            divergences.append(
                Divergence(expected.index, "observation", want.method, want.result, None)
            )
            continue

        if got.error is not None and want.error is None:
            divergences.append(
                Divergence(expected.index, "error", want.method, want.result, got.error)
            )
            continue

        if not _equivalent(want.result, got.result):
            divergences.append(
                Divergence(expected.index, "observation", want.method, want.result, got.result)
            )

    return divergences


def _diff_notifications(expected: Step, actual: Step) -> list[Divergence]:
    """Compare notifications as a multiset.

    .. note:: Delivery order between independent objects is not something the application
       promises, so comparing sequences would make a replay flaky rather than strict.
    """
    def key(notification: tuple[str, dict]) -> str:
        return json.dumps(notification, sort_keys=True)

    remaining = [key(n) for n in actual.notifications]
    divergences: list[Divergence] = []

    for notification in expected.notifications:
        wanted = key(notification)
        if wanted in remaining:
            remaining.remove(wanted)
        else:
            divergences.append(
                Divergence(expected.index, "notification", notification[0], notification[1], None)
            )

    return divergences


def diff_steps(expected: list[Step], actual: list[Step]) -> list[Divergence]:
    """Compare a recorded run against a replayed one.

    :param expected: Steps as recorded.
    :param actual: Steps as replayed.
    :return: Every difference found, in step order. Empty means the runs agree.
    """
    divergences: list[Divergence] = []

    for index, want in enumerate(expected):
        if index >= len(actual):
            action = want.action.method if want.action else "<baseline>"
            divergences.append(Divergence(want.index, "missing_step", action, action, None))
            continue

        got = actual[index]
        divergences.extend(_diff_observations(want, got))
        divergences.extend(_diff_notifications(want, got))

    return divergences


@dataclass
class ReplayResult:
    """The outcome of driving a scenario against a running application."""

    scenario: Scenario
    steps: list[Step]
    divergences: list[Divergence]
    aborted_at: int | None = None
    abort_reason: str | None = None

    @property
    def passed(self) -> bool:
        """Whether the application still behaves as recorded."""
        return not self.divergences and self.aborted_at is None

    def as_scenario(self) -> Scenario:
        """Treat this run as the recording to compare future runs against.

        .. note:: What a record run produces. The observations came from the watch list rather
           than from the log it was driven from, so the result -- not the input -- is the golden.
        """
        return Scenario(steps=self.steps, source=self.scenario.source)

    def write_log(self, path: str | Path) -> None:
        """Write this run out as a message log that load_scenario can read back.

        :param path: Destination .jsonl file.

        .. note:: Emitted as req/res pairs rather than as a bespoke format, so a recorded
           baseline and a hand-captured session are the same kind of file and one tool reads
           both.
        """
        # Stamped so a baseline states its own shape. A future reader can then refuse a file
        # it does not understand instead of silently diffing against changed semantics.
        lines: list[str] = [
            json.dumps({"dir": FORMAT_DIR, "format": SCENARIO_FORMAT, "source": self.scenario.source})
        ]
        request_id = 0

        for step in self.steps:
            if step.action is not None:
                request_id += 1
                lines.append(json.dumps({"dir": "req", "id": request_id, "method": step.action.method, "params": step.action.params}))
                lines.append(json.dumps({"dir": "res", "id": request_id, "method": step.action.method, "result": {"ok": True}}))

            for setup in step.setups:
                request_id += 1
                lines.append(json.dumps({"dir": "req", "id": request_id, "method": setup.method, "params": setup.params}))
                lines.append(json.dumps({"dir": "res", "id": request_id, "method": setup.method, "result": {"ok": True}}))

            for observation in step.observations:
                request_id += 1
                lines.append(json.dumps({"dir": "req", "id": request_id, "method": observation.method, "params": observation.params}))
                if observation.error is not None:
                    lines.append(json.dumps({"dir": "err", "id": request_id, "method": observation.method, "error": observation.error}))
                else:
                    lines.append(json.dumps({"dir": "res", "id": request_id, "method": observation.method, "result": observation.result}))

            for method, params in step.notifications:
                lines.append(json.dumps({"dir": "ntf", "method": method, "params": params}))

        Path(path).write_text("\n".join(lines) + "\n", encoding="utf-8")

    def summary(self) -> str:
        """One line fit for a test runner."""
        if self.aborted_at is not None:
            return f"{self.scenario.source}: aborted at step {self.aborted_at}: {self.abort_reason}"
        if self.divergences:
            return f"{self.scenario.source}: {len(self.divergences)} divergence(s)"
        driven = sum(1 for step in self.scenario.steps if step.action)
        return f"{self.scenario.source}: {driven} action(s) replayed, no divergence"


async def run_scenario(
    scenario: Scenario,
    probe: Any,
    *,
    settle: float = 0.1,
    timeout: float | None = None,
    watch: WatchList | None = None,
    record: bool = False,
) -> ReplayResult:
    """Drive a recorded scenario against a probe and compare what comes back.

    :param scenario: The recording to replay.
    :param probe: Anything offering ``call``, ``add_notification_handler`` and
        ``remove_notification_handler`` -- a :class:`~qtpilot.connection.ProbeConnection` in
        practice.
    :param settle: Seconds to wait after each action for signals to arrive. Signals are
        delivered asynchronously, so asserting the instant a call returns reports races as
        divergences.
    :param timeout: Per-call timeout, passed through to the probe.
    :param watch: Observing calls to issue after every action, in addition to whatever the
        recording observed. Recorded observations keep their positions, so adding a watch list
        cannot change what an existing baseline means.
    :param record: Capture a new baseline instead of comparing against the input. Nothing is
        diffed; use :meth:`ReplayResult.as_scenario` or :meth:`ReplayResult.write_log` to keep
        the result.
    :return: The observed run and how it differed from the recording.
    :raises ValueError: If the scenario has nothing to drive.

    .. note:: An error on an *observation* is recorded and the run continues -- an object that
       no longer exists is a finding worth reporting alongside the rest. An error on an *action*
       aborts: every later step assumes the earlier ones happened, so carrying on would report a
       cascade of differences that are all the same failure.
    """
    if not scenario.is_replayable:
        raise ValueError(
            f"{scenario.source}: nothing to replay -- no mutating calls found. "
            "A level-1 log records tool names but no wire traffic; record at level 2 or above."
        )

    # None means "inherit the connection's default deadline", NOT "wait forever".
    # ProbeConnection.call uses a sentinel for its default; passing None explicitly selects its
    # unbounded branch, so a replay against an application that wedges on a modal dialog would
    # hang until the CI runner's global kill rather than failing. Omitting the argument is the
    # only way to say "use the default".
    call_kwargs: dict[str, Any] = {} if timeout is None else {"timeout": timeout}

    collected: list[tuple[str, dict]] = []

    def collect(method: str, params: dict) -> None:
        collected.append((method, normalise(params, top_level=False)))

    probe.add_notification_handler(collect)
    observed: list[Step] = []
    aborted_at: int | None = None
    abort_reason: str | None = None

    try:
        for recorded in scenario.steps:
            step = Step(index=recorded.index, action=recorded.action)
            collected.clear()

            if recorded.action is not None:
                try:
                    await probe.call(recorded.action.method, recorded.action.params, **call_kwargs)
                except ProbeError as exc:
                    aborted_at = recorded.index
                    abort_reason = f"{recorded.action.method}: {exc}"
                    observed.append(step)
                    break

            # Re-establish session state before observing. A failure here is not an
            # application divergence -- it means the replay could not be set up -- so it aborts
            # with a reason that says so rather than being reported as a behaviour change.
            for setup in recorded.setups:
                try:
                    await probe.call(setup.method, setup.params, **call_kwargs)
                except ProbeError as exc:
                    aborted_at = recorded.index
                    abort_reason = f"setup failed -- {setup.method}: {exc}"
                    observed.append(step)
                    break
            if aborted_at is not None:
                break
            step.setups = list(recorded.setups)

            if settle:
                await asyncio.sleep(settle)

            # Recorded observations first, then the watch list. Keeping the recorded ones in
            # their original positions is what lets a watch list be added to an existing
            # scenario without changing what its baseline means.
            wanted: list[tuple[str, dict]] = [(o.method, o.params) for o in recorded.observations]
            if watch is not None:
                wanted.extend((t.method, t.params) for t in watch.targets)

            for method, params in wanted:
                try:
                    result = await probe.call(method, params, **call_kwargs)
                    step.observations.append(
                        Observation(method=method, params=params, result=normalise(result, top_level=False))
                    )
                except ProbeError as exc:
                    step.observations.append(
                        Observation(method=method, params=params, result=None, error=str(exc))
                    )

            step.notifications = list(collected)
            observed.append(step)
    finally:
        # Detached on every path: a handler left behind keeps feeding a dead run's collector for
        # the rest of the session.
        probe.remove_notification_handler(collect)

    # A record run is producing the golden, so there is nothing to compare against yet.
    if record or aborted_at is not None:
        divergences: list[Divergence] = []
    else:
        divergences = diff_steps(scenario.steps, observed)

    return ReplayResult(
        scenario=scenario,
        steps=observed,
        divergences=divergences,
        aborted_at=aborted_at,
        abort_reason=abort_reason,
    )
