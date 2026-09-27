"""Opt-in qualification: failures are evidence, never silently certified as support."""

from __future__ import annotations

import asyncio
import json
import os
import subprocess
import sys
from collections.abc import Iterator
from pathlib import Path

import pytest

from qtpilot.connection import ProbeConnection
from tests.probe_expectations import expect_probe
from tests.test_complicated_app_e2e import BUILD, _app_env, _free_port, _stop_group, _wait_for_port

pytestmark = [pytest.mark.real_probe, pytest.mark.skipif(
    os.environ.get("QTPILOT_PYSIDE_QUALIFY") != "1", reason="Optional binding qualification not requested")]


@pytest.fixture
def python_fixture(tmp_path: Path) -> Iterator[tuple[str, dict[str, object]]]:
    assert BUILD.available, "Qualification requires built launcher/probe/application"
    interpreter = Path(os.environ.get("QTPILOT_PYSIDE_PYTHON", sys.executable))
    metadata = tmp_path / "runtime.json"
    process_log = tmp_path / "process.log"
    port = _free_port()
    with process_log.open("w") as output:
        proc = subprocess.Popen(
            [str(BUILD.launcher), "--port", str(port), str(interpreter),
             str(Path(__file__).with_name("pyside") / "fixture.py"), str(metadata)],
            env=_app_env(str(BUILD.qt_prefix) if BUILD.qt_prefix else None),
            stdout=output, stderr=output, start_new_session=True,
        )
        try:
            try:
                _wait_for_port(port, proc, timeout=10)
            except (RuntimeError, TimeoutError) as error:
                raise AssertionError(process_log.read_text()) from error
            runtime = json.loads(metadata.read_text())
            print(f"Qualified runtime: {runtime!r}")
            yield f"ws://127.0.0.1:{port}", runtime
        finally:
            _stop_group(proc)
            assert proc.poll() is not None, "Qualification process did not terminate"
            print(process_log.read_text())


def test_python_widget_property_and_runtime_identity(python_fixture: tuple[str, dict[str, object]]) -> None:
    url, runtime = python_fixture
    libraries = runtime["qtCoreLibraries"]
    assert isinstance(libraries, list) and len(libraries) == 1, f"Ambiguous Qt runtime: {runtime!r}"

    async def check() -> None:
        conn = ProbeConnection(url)
        await conn.connect()
        try:
            ping = await conn.call("qt.ping")
            expect_probe(ping).to_identify(application="python-widgets-fixture", qt_version=str(runtime["qt"]))
            assert ping["result"]["pid"] == runtime["pid"], "Connected to another process"
            identifier = expect_probe(await conn.call("qt.objects.search", {"objectName": "fixtureRoot"})).to_find("fixtureRoot")
            expect_probe(await conn.call("qt.properties.get", {"objectId": identifier, "name": "value"})).to_have_value(1)
            await conn.call("qt.properties.set", {"objectId": identifier, "name": "value", "value": 42})
            expect_probe(await conn.call("qt.properties.get", {"objectId": identifier, "name": "value"})).to_have_value(42)
        finally:
            await conn.disconnect()

    asyncio.run(check())


def test_python_typed_slot_has_a_real_effect(python_fixture: tuple[str, dict[str, object]]) -> None:
    url, _ = python_fixture

    async def check() -> None:
        conn = ProbeConnection(url)
        await conn.connect()
        try:
            await conn.call("qt.methods.invoke", {"objectId": "fixtureRoot", "method": "increment", "args": [3]})
            expect_probe(await conn.call("qt.properties.get", {"objectId": "fixtureRoot", "name": "value"})).to_have_value(4)
        finally:
            await conn.disconnect()

    asyncio.run(check())
