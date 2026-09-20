"""End-to-end replay against a real Qt application.

Every other replay test drives a ``FakeProbe`` that answers on method name and
ignores params. That is why the unit suite is green while replay corrupts the
params it sends: a fake that ignores params cannot notice they were rewritten.

These tests launch ``qtPilot-test-app`` with the probe injected, record a real
session over the wire, and replay it against the same live application. They are
the only tests here that exercise object-id resolution, Qt's deferred click
delivery, and result shapes the probe actually emits.

Skipped automatically when the binaries have not been built, so a plain
``pytest`` run on a machine with no build is unaffected. Build them with:

    cmake -B build -DQTPILOT_QT_DIR=<qt> && cmake --build build
"""

from __future__ import annotations

import asyncio
import contextlib
import json
import os
import socket
import subprocess
import sys
import time
from pathlib import Path

import pytest

from qtpilot.connection import ProbeConnection
from qtpilot.message_logger import MessageLogger
from qtpilot.replay import parse_entries, run_scenario

# --- locating the build ----------------------------------------------------

REPO_ROOT = Path(__file__).resolve().parents[2]
BUILD_DIR = REPO_ROOT / "build"
LAUNCHER = BUILD_DIR / "bin" / "qtPilot-launcher"
TEST_APP = BUILD_DIR / "bin" / "qtPilot-test-app"
CMAKE_CACHE = BUILD_DIR / "CMakeCache.txt"

pytestmark = pytest.mark.skipif(
    not (LAUNCHER.exists() and TEST_APP.exists()),
    reason="qtPilot-launcher / qtPilot-test-app not built; run cmake --build build",
)

# Widget ids in the test app's form tab. Written out in full rather than
# assembled, because the shape of a real objectId -- a slash-joined path, not a
# bare class name -- is itself something these tests are pinning.
FORM = "MainWindow/centralWidget/tabWidget/qt_tabwidget_stackedwidget/formTab/"
NAME_EDIT = FORM + "nameEdit"
EMAIL_EDIT = FORM + "emailEdit"
SUBMIT = FORM + "submitButton"
CLEAR = FORM + "clearButton"
RESULT_TEXT = FORM + "resultGroup/resultText"


def _free_port() -> int:
    """An ephemeral port the app can bind. Never 9222: a developer's own probe,
    or another Qt app already under test, commonly holds it."""
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def _qt_dir() -> str | None:
    if not CMAKE_CACHE.exists():
        return None
    for line in CMAKE_CACHE.read_text().splitlines():
        if line.startswith("QTPILOT_QT_DIR:PATH="):
            return line.split("=", 1)[1].strip()
    return None


def _app_env(qt_dir: str | None) -> dict:
    env = dict(os.environ)
    env["QT_QPA_PLATFORM"] = "offscreen"
    # The bind policy defaults to all interfaces; a test process has no business
    # accepting connections from the network.
    env["QTPILOT_BIND_ADDRESS"] = "loopback"
    if qt_dir:
        if sys.platform == "darwin":
            env["DYLD_FRAMEWORK_PATH"] = f"{qt_dir}/lib"
        else:
            env["LD_LIBRARY_PATH"] = f"{qt_dir}/lib:" + env.get("LD_LIBRARY_PATH", "")
        env["QT_PLUGIN_PATH"] = f"{qt_dir}/plugins"
    return env


def _wait_for_port(port: int, proc: subprocess.Popen, timeout: float = 25.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            raise RuntimeError(f"app exited early with code {proc.returncode}")
        with contextlib.closing(socket.socket()) as s:
            s.settimeout(0.3)
            if s.connect_ex(("127.0.0.1", port)) == 0:
                return
        time.sleep(0.2)
    raise TimeoutError(f"probe never listened on {port}")


@pytest.fixture(scope="module")
def live_app():
    """A running test app with the probe injected. Yields its ws:// URL."""
    port = _free_port()
    proc = subprocess.Popen(
        [str(LAUNCHER), "--port", str(port), str(TEST_APP)],
        env=_app_env(_qt_dir()),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    try:
        _wait_for_port(port, proc)
        yield f"ws://127.0.0.1:{port}"
    finally:
        proc.terminate()
        with contextlib.suppress(subprocess.TimeoutExpired):
            proc.wait(timeout=10)
        if proc.poll() is None:
            proc.kill()
        if proc.stdout:
            proc.stdout.close()


@contextlib.asynccontextmanager
async def _connected(ws_url: str):
    conn = ProbeConnection(ws_url)
    await conn.connect()
    try:
        yield conn
    finally:
        await conn.disconnect()


async def _set_text(conn: ProbeConnection, object_id: str, value: str) -> None:
    await conn.call("qt.properties.set", {"objectId": object_id, "name": "text", "value": value})


async def _plain_text(conn: ProbeConnection, object_id: str) -> str:
    got = await conn.call("qt.properties.get", {"objectId": object_id, "name": "plainText"})
    return got["result"]["value"]


async def _reset(conn: ProbeConnection) -> None:
    """Put the app back in a known state between recordings."""
    await conn.call("qt.ui.click", {"objectId": CLEAR})
    await asyncio.sleep(0.3)
    await _set_text(conn, NAME_EDIT, "")
    await _set_text(conn, EMAIL_EDIT, "")


def _entries(log_path: Path) -> list[dict]:
    """Read a JSONL log back into entries."""
    return [json.loads(line) for line in log_path.read_text().splitlines() if line]


async def _record_form_session(ws_url: str, log_path: Path) -> None:
    """Drive a real session with the logger attached, producing a level-2 log.

    The session is deliberately the smallest thing that is still a real test: a
    mutation whose effect is only observable through a *different* widget, so a
    replay that silently no-ops cannot pass.
    """
    logger = MessageLogger()
    async with _connected(ws_url) as conn:
        await _reset(conn)
        logger.start(path=str(log_path), level=2)
        logger.attach(conn)
        try:
            await conn.call("qt.ui.sendKeys", {"objectId": NAME_EDIT, "text": "Ada"})
            await asyncio.sleep(0.2)
            await conn.call("qt.ui.click", {"objectId": SUBMIT})
            await asyncio.sleep(0.4)
            await conn.call("qt.properties.get", {"objectId": RESULT_TEXT, "name": "plainText"})
        finally:
            logger.detach(conn)
            logger.stop()


# --- the tests -------------------------------------------------------------


def test_the_app_starts_and_answers(live_app):
    """Guards the fixture itself: if this fails, the rest prove nothing."""

    async def go():
        async with _connected(live_app) as conn:
            assert await conn.call("ping") == "pong"
            version = await conn.call("getVersion")
            assert version["version"], "probe reported no version"

    asyncio.run(go())


def test_computer_use_click_then_type_updates_the_clicked_field(live_app):
    """Computer-use input must reach the widget selected by its own click."""

    async def go() -> str:
        async with _connected(live_app) as conn:
            await _reset(conn)
            geometry = await conn.call("qt.ui.geometry", {"objectId": NAME_EDIT})
            rect = geometry["result"]["global"]
            await conn.call(
                "cu.click",
                {
                    "x": rect["x"] + rect["width"] // 2,
                    "y": rect["y"] + rect["height"] // 2,
                    "screenAbsolute": True,
                },
            )
            await conn.call("cu.type", {"text": "Computer Use"})
            value = await conn.call(
                "qt.properties.get", {"objectId": NAME_EDIT, "name": "text"}
            )
            return value["result"]["value"]

    assert asyncio.run(go()) == "Computer Use"


def test_recorded_session_produces_a_replayable_scenario(live_app, tmp_path):
    """A level-2 recording of real traffic must parse into actions.

    Pins that the recorder and the parser agree about the shape of a log. If
    parse_entries silently drops the methods the app was actually driven with,
    the scenario is empty and every later assertion is vacuous.
    """
    log_path = tmp_path / "session.jsonl"
    asyncio.run(_record_form_session(live_app, log_path))

    scenario = parse_entries(_entries(log_path))

    assert scenario.is_replayable, "a level-2 recording of real traffic parsed to zero actions"
    methods = [s.action.method for s in scenario.steps if s.action]
    assert "qt.ui.sendKeys" in methods
    assert "qt.ui.click" in methods
    observed = [o.method for s in scenario.steps for o in s.observations]
    assert "qt.properties.get" in observed, "the recorded observation was dropped"


def test_replaying_an_unchanged_app_reports_no_divergence(live_app, tmp_path):
    """Record, then replay against the same application: nothing changed, so
    nothing should diverge. This is the test the whole feature rests on."""
    log_path = tmp_path / "session.jsonl"
    asyncio.run(_record_form_session(live_app, log_path))

    async def go():
        entries = _entries(log_path)
        scenario = parse_entries(entries)
        async with _connected(live_app) as conn:
            await _reset(conn)
            return await run_scenario(scenario, conn, settle=0.4)

    result = asyncio.run(go())

    assert result.aborted_at is None, f"replay aborted: {result.abort_reason}"
    assert result.divergences == [], "replay of an unchanged app reported divergences: " + "; ".join(
        f"{d.kind} step {d.step}: expected {d.expected!r} got {d.actual!r}"
        for d in result.divergences
    )
    assert result.passed


def test_replay_detects_a_real_behaviour_change(live_app, tmp_path):
    """The other half: a replay that passes on an unchanged app must FAIL on a
    changed one, or it is asserting nothing.

    The perturbation is a field the scenario never drives but whose value the
    submit handler folds into the observed output -- the shape of a genuine
    regression rather than a synthetic one.
    """
    log_path = tmp_path / "session.jsonl"
    asyncio.run(_record_form_session(live_app, log_path))

    async def go():
        entries = _entries(log_path)
        scenario = parse_entries(entries)
        async with _connected(live_app) as conn:
            await _reset(conn)
            # The app now behaves differently: submit will fold this into resultText.
            await _set_text(conn, EMAIL_EDIT, "ada@example.com")
            return await run_scenario(scenario, conn, settle=0.4)

    result = asyncio.run(go())

    assert result.aborted_at is None, f"replay aborted instead of diverging: {result.abort_reason}"
    assert result.divergences, "replay passed against an application that demonstrably changed"
    assert not result.passed


def test_replay_drives_the_application_rather_than_only_reading_it(live_app, tmp_path):
    """A replay must actually re-perform the recorded mutation.

    Without this, a replay that quietly failed to drive anything -- an
    unresolvable objectId, a dropped method -- would still report a clean run,
    which is the failure mode the design notes call worse than failing.
    """
    log_path = tmp_path / "session.jsonl"
    asyncio.run(_record_form_session(live_app, log_path))

    async def go():
        entries = _entries(log_path)
        scenario = parse_entries(entries)
        async with _connected(live_app) as conn:
            await _reset(conn)
            before = await _plain_text(conn, RESULT_TEXT)
            await run_scenario(scenario, conn, settle=0.4)
            after = await _plain_text(conn, RESULT_TEXT)
            return before, after

    before, after = asyncio.run(go())

    assert before == "", f"reset left stale state: {before!r}"
    assert "Ada" in after, (
        f"replay did not re-drive the recorded input: resultText is {after!r}. "
        "The scenario's mutation never reached the application."
    )


# --- cases that exercise the known defects --------------------------------
# The five tests above cover the happy path and pass today. These do not: each
# drives a path the unit suite's FakeProbe cannot reach, and each corresponds to
# a blocker recorded on the replay PR.


def test_replay_does_not_corrupt_recorded_text(live_app, tmp_path):
    """Recorded input must reach the application byte for byte.

    normalise() masks anything matching ``name~<digits>`` to ``name~*`` so two
    runs' generated object handles compare equal -- but it is applied to request
    params, and those same dicts are what get re-driven. Any recorded text of
    that shape is therefore typed into the app as ``...~*``.
    """
    log_path = tmp_path / "typed.jsonl"
    typed = "Ada~7"

    async def record():
        logger = MessageLogger()
        async with _connected(live_app) as conn:
            await _reset(conn)
            logger.start(path=str(log_path), level=2)
            logger.attach(conn)
            try:
                await conn.call("qt.ui.sendKeys", {"objectId": NAME_EDIT, "text": typed})
                await asyncio.sleep(0.2)
            finally:
                logger.detach(conn)
                logger.stop()

    async def replay():
        entries = _entries(log_path)
        scenario = parse_entries(entries)
        async with _connected(live_app) as conn:
            await _reset(conn)
            await run_scenario(scenario, conn, settle=0.3)
            got = await conn.call(
                "qt.properties.get", {"objectId": NAME_EDIT, "name": "text"}
            )
            return got["result"]["value"]

    asyncio.run(record())
    landed = asyncio.run(replay())

    assert landed == typed, (
        f"replay typed {landed!r} instead of the recorded {typed!r} -- "
        "the comparison mask is being applied to the params that get re-driven"
    )


def test_level3_recording_replays_without_spurious_divergence(live_app, tmp_path):
    """A level-3 recording captures notifications. Replaying it must either
    re-establish the subscriptions that produce them or not assert on them.

    docs/REPLAY.md recommends level 3 when a scenario should assert on signal
    emissions, so an unchanged application must still replay clean at that level.
    """
    log_path = tmp_path / "signals.jsonl"

    async def record():
        logger = MessageLogger()
        async with _connected(live_app) as conn:
            await _reset(conn)
            logger.start(path=str(log_path), level=3)
            logger.attach(conn)
            try:
                await conn.call(
                    "qt.signals.subscribe",
                    {"objectId": NAME_EDIT, "signal": "textChanged"},
                )
                await conn.call("qt.ui.sendKeys", {"objectId": NAME_EDIT, "text": "Ada"})
                await asyncio.sleep(0.5)
            finally:
                logger.detach(conn)
                logger.stop()

    async def replay():
        entries = _entries(log_path)
        scenario = parse_entries(entries)
        async with _connected(live_app) as conn:
            await _reset(conn)
            return await run_scenario(scenario, conn, settle=0.5)

    asyncio.run(record())
    result = asyncio.run(replay())

    assert result.aborted_at is None, f"replay aborted: {result.abort_reason}"
    assert result.divergences == [], (
        "a level-3 recording diverged against an unchanged application: "
        + "; ".join(f"{d.kind} step {d.step}" for d in result.divergences)
    )


def test_the_same_scenario_replays_clean_repeatedly(live_app, tmp_path):
    """One clean run can be luck. "Deterministic" means it holds every time.

    Five consecutive replays of one recording against one application, each from
    the same reset state. A single divergence in any of them is a flake, and a
    flake here is worse than a failure -- it teaches people to re-run until green.
    """
    log_path = tmp_path / "session.jsonl"
    asyncio.run(_record_form_session(live_app, log_path))
    scenario = parse_entries(_entries(log_path))

    async def go():
        outcomes = []
        async with _connected(live_app) as conn:
            for _ in range(5):
                await _reset(conn)
                result = await run_scenario(scenario, conn, settle=0.4)
                outcomes.append((len(result.divergences), result.aborted_at))
        return outcomes

    outcomes = asyncio.run(go())

    assert outcomes == [(0, None)] * 5, f"replay was not stable across runs: {outcomes}"


def test_a_log_holding_two_appended_sessions_keeps_them_distinct(live_app, tmp_path):
    """MessageLogger opens its file in append mode and request ids restart per
    session, so one path can hold two sessions both numbering from 1. The parser
    must not pair session two's response with session one's abandoned request --
    that would replay the wrong input and blame the application for the result.
    """
    log_path = tmp_path / "two.jsonl"

    async def record(text: str):
        logger = MessageLogger()
        async with _connected(live_app) as conn:
            await _reset(conn)
            logger.start(path=str(log_path), level=2)
            logger.attach(conn)
            try:
                await conn.call("qt.ui.sendKeys", {"objectId": NAME_EDIT, "text": text})
                await asyncio.sleep(0.2)
            finally:
                logger.detach(conn)
                logger.stop()

    asyncio.run(record("Ada"))
    asyncio.run(record("Grace"))

    scenario = parse_entries(_entries(log_path))
    typed = [s.action.params.get("text") for s in scenario.steps if s.action]

    assert typed == ["Ada", "Grace"], f"appended sessions were spliced wrongly: {typed}"
