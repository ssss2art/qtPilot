"""Stopping a foreground launcher must stop the app it launched.

A foreground ``qtPilot-launcher`` forks the target and waits on it. A caller that
stops the launcher -- a test fixture, the MCP server, a terminal job -- only knows
the launcher's pid. If the launcher does not pass the signal on, the app is
orphaned and keeps its probe port, and the next run finds a stranger on the host.
Dozens of such orphans had piled up on one developer machine from the e2e
fixtures alone.

Skipped when the binaries have not been built, like the other e2e tests.
"""

from __future__ import annotations

import asyncio
import contextlib
import os
import signal
import socket
import subprocess
import sys
import time
from pathlib import Path

import pytest
from qtpilot.connection import ProbeConnection

REPO_ROOT = Path(__file__).resolve().parents[2]
BUILD_DIR = REPO_ROOT / "build"
LAUNCHER = BUILD_DIR / "bin" / "qtPilot-launcher"
TEST_APP = BUILD_DIR / "bin" / "qtPilot-test-app"
CMAKE_CACHE = BUILD_DIR / "CMakeCache.txt"

pytestmark = [
    pytest.mark.skipif(
        not (LAUNCHER.exists() and TEST_APP.exists()),
        reason="qtPilot-launcher / qtPilot-test-app not built; run cmake --build build",
    ),
    pytest.mark.skipif(sys.platform == "win32", reason="POSIX signals"),
]


def _free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def _app_env() -> dict[str, str]:
    env = dict(os.environ)
    env["QT_QPA_PLATFORM"] = "offscreen"
    env["QTPILOT_BIND_ADDRESS"] = "loopback"
    if CMAKE_CACHE.exists():
        for line in CMAKE_CACHE.read_text().splitlines():
            if line.startswith("QTPILOT_QT_DIR:PATH="):
                qt_dir = line.split("=", 1)[1].strip()
                if sys.platform == "darwin":
                    env["DYLD_FRAMEWORK_PATH"] = f"{qt_dir}/lib"
                else:
                    env["LD_LIBRARY_PATH"] = f"{qt_dir}/lib:" + env.get("LD_LIBRARY_PATH", "")
                env["QT_PLUGIN_PATH"] = f"{qt_dir}/plugins"
    return env


def _wait_for_port(port: int, proc: subprocess.Popen[bytes], timeout: float = 25.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            raise RuntimeError(f"launcher exited early with code {proc.returncode}")
        with contextlib.closing(socket.socket()) as s:
            s.settimeout(0.3)
            if s.connect_ex(("127.0.0.1", port)) == 0:
                return
        time.sleep(0.2)
    raise TimeoutError(f"probe never listened on {port}")


def _app_pid(port: int) -> int:
    async def ping() -> int:
        conn = ProbeConnection(f"ws://127.0.0.1:{port}")
        await conn.connect()
        try:
            result = await conn.call("qt.ping")
        finally:
            await conn.disconnect()
        return int(result["result"]["pid"])

    return asyncio.run(ping())


def _alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    return True


@pytest.mark.parametrize("sig", [signal.SIGTERM, signal.SIGINT, signal.SIGHUP], ids=lambda s: s.name)
def test_signalling_the_launcher_stops_the_app(sig: signal.Signals) -> None:
    port = _free_port()
    # Its own session, so the signal reaches the launcher alone, the way a caller
    # holding only the launcher's pid delivers it -- not a terminal's Ctrl-C,
    # which the whole foreground group receives.
    launcher = subprocess.Popen(
        [str(LAUNCHER), "--quiet", "--port", str(port), str(TEST_APP)],
        env=_app_env(),
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        start_new_session=True,
    )
    app_pid = 0
    try:
        _wait_for_port(port, launcher)
        app_pid = _app_pid(port)
        assert app_pid != launcher.pid

        launcher.send_signal(sig)
        launcher.wait(timeout=10)

        deadline = time.monotonic() + 10
        while _alive(app_pid) and time.monotonic() < deadline:
            time.sleep(0.1)
        assert not _alive(app_pid), f"app {app_pid} outlived its launcher after {sig.name}"
    finally:
        # Never leak the app this test is about, whatever the outcome.
        with contextlib.suppress(ProcessLookupError, PermissionError):
            os.killpg(launcher.pid, signal.SIGKILL)
        if app_pid:
            with contextlib.suppress(ProcessLookupError):
                os.kill(app_pid, signal.SIGKILL)
        with contextlib.suppress(subprocess.TimeoutExpired):
            launcher.wait(timeout=5)
