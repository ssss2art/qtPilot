"""End-to-end verification of the fully-featured test application.

Tests driving both QWidgets (native controls, models, tables, graphics scene,
dialogs) and embedded QML scenes with custom types through qtPilot.
"""

from __future__ import annotations

import asyncio
import contextlib
import os
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

pytestmark = pytest.mark.skipif(
    not (LAUNCHER.exists() and TEST_APP.exists()),
    reason="qtPilot-launcher / qtPilot-test-app not built; run cmake --build build",
)


def _free_port() -> int:
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
def live_complicated_app():
    port = _free_port()
    proc = subprocess.Popen(
        [str(LAUNCHER), "--port", str(port), str(TEST_APP)],
        env=_app_env(_qt_dir()),
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
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


def test_app_exposes_rich_widget_and_model_tabs(live_complicated_app):
    """Verify test app provides form, table, canvas, and lifecycle tabs."""

    async def go():
        async with _connected(live_complicated_app) as conn:
            # 1. Search for table view and query model data
            tables = await conn.call("qt.objects.search", {"className": "QTableView"})
            assert len(tables["result"]["objects"]) >= 1, "Expected at least one QTableView"
            table_id = tables["result"]["objects"][0]["objectId"]

            # Read table model data
            model_data = await conn.call(
                "qt.models.data", {"objectId": table_id}
            )
            assert "rows" in model_data["result"]
            assert len(model_data["result"]["rows"]) >= 1

            # 2. Search for graphics view items
            canvas = await conn.call("qt.objects.search", {"className": "QGraphicsView"})
            assert len(canvas["result"]["objects"]) >= 1, "Expected QGraphicsView"

            # 3. Check for custom gauge control and property interaction
            custom_gauge = await conn.call("qt.objects.search", {"className": "CustomGaugeWidget"})
            assert len(custom_gauge["result"]["objects"]) >= 1, "Expected CustomGaugeWidget"
            gauge_id = custom_gauge["result"]["objects"][0]["objectId"]

            # Read and set custom gauge property
            gauge_val = await conn.call("qt.properties.get", {"objectId": gauge_id, "name": "value"})
            assert isinstance(gauge_val["result"]["value"], int)
            await conn.call("qt.properties.set", {"objectId": gauge_id, "name": "value", "value": 75})
            gauge_val2 = await conn.call("qt.properties.get", {"objectId": gauge_id, "name": "value"})
            assert gauge_val2["result"]["value"] == 75

            # 4. Check dynamic lifecycle tab: object storm & cleanup
            storm_btn = await conn.call(
                "qt.objects.search", {"objectName": "spawnStormButton"}
            )
            assert len(storm_btn["result"]["objects"]) == 1, "Expected spawnStormButton"
            storm_id = storm_btn["result"]["objects"][0]["objectId"]

            clear_btn = await conn.call(
                "qt.objects.search", {"objectName": "clearStormButton"}
            )
            assert len(clear_btn["result"]["objects"]) == 1, "Expected clearStormButton"
            clear_id = clear_btn["result"]["objects"][0]["objectId"]

            # Spawn 100 objects
            await conn.call("qt.ui.click", {"objectId": storm_id})
            await asyncio.sleep(0.3)

            # Verify objects exist
            node_0 = await conn.call("qt.objects.search", {"objectName": "stormLabel_0"})
            assert len(node_0["result"]["objects"]) == 1, "Expected stormLabel_0 to exist after storm"
            node_99 = await conn.call("qt.objects.search", {"objectName": "stormLabel_99"})
            assert len(node_99["result"]["objects"]) == 1, "Expected stormLabel_99 to exist after storm"

            # Clear objects
            await conn.call("qt.ui.click", {"objectId": clear_id})
            await asyncio.sleep(0.3)

            # Verify destroyed
            cleared_node = await conn.call("qt.objects.search", {"objectName": "stormLabel_0"})
            assert len(cleared_node["result"]["objects"]) == 0, "Expected stormLabel_0 destroyed after clear"

    asyncio.run(go())


def test_app_embedded_qml_scene(live_complicated_app):
    """Verify test app hosts live QML objects and interacts through QQuickWidget."""

    async def go():
        async with _connected(live_complicated_app) as conn:
            # Look for QQuickWidget
            quick_widgets = await conn.call(
                "qt.objects.search", {"className": "QQuickWidget"}
            )
            assert len(quick_widgets["result"]["objects"]) >= 1, "Expected QQuickWidget"

            # Look for QML items inside the quick widget
            qml_btn = await conn.call("qt.objects.search", {"objectName": "qmlButton"})
            assert len(qml_btn["result"]["objects"]) >= 1, "Expected qmlButton in QML scene"

            btn_id = qml_btn["result"]["objects"][0]["objectId"]
            # Click the QML button via probe
            res = await conn.call("qt.ui.click", {"objectId": btn_id})
            assert res["result"]["ok"] is True

            # Verify QML bridge received the click
            bridges = await conn.call("qt.objects.search", {"className": "QmlTestBridge"})
            if len(bridges["result"]["objects"]) >= 1:
                bridge_id = bridges["result"]["objects"][0]["objectId"]
                clicks = await conn.call(
                    "qt.properties.get", {"objectId": bridge_id, "name": "clickCount"}
                )
                assert clicks["result"]["value"] >= 1

    asyncio.run(go())
