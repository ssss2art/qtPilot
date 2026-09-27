"""Profile restrictions apply to the actual injected application's UDP boundary."""

from __future__ import annotations

import json
import socket
import subprocess
from pathlib import Path

import pytest

from tests.test_complicated_app_e2e import BUILD, _app_env, _free_port, _stop_group, _wait_for_port

pytestmark = [pytest.mark.real_probe, pytest.mark.skipif(not BUILD.available, reason="Native application not built")]


@pytest.mark.parametrize("profile,via_cli", [("", False), ("local", False), ("local", True)])
def test_local_is_quiet_while_legacy_loopback_still_announces(profile: str, via_cli: bool, tmp_path: Path) -> None:
    environment = _app_env(str(BUILD.qt_prefix) if BUILD.qt_prefix else None)
    for name in ("QTPILOT_AUTH_TOKEN_FILE", "QTPILOT_TLS_CERT_FILE", "QTPILOT_TLS_KEY_FILE"):
        environment.pop(name, None)
    environment.pop("QTPILOT_PROFILE", None)
    if via_cli or profile:
        environment["QTPILOT_PROFILE"] = "remote" if via_cli else profile
    arguments = ["--profile", profile] if via_cli else []
    port = _free_port()
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as discovery:
        discovery.bind(("127.0.0.1", 0))
        discovery.settimeout(0.5)
        environment["QTPILOT_DISCOVERY_PORT"] = str(discovery.getsockname()[1])
        with (tmp_path / "process.log").open("w") as output:
            process = subprocess.Popen(
                [str(BUILD.launcher), *arguments, "--port", str(port), str(BUILD.application)],
                env=environment, stdout=output, stderr=output, start_new_session=True,
            )
            try:
                _wait_for_port(port, process, timeout=10)
                if profile == "local":
                    with pytest.raises(TimeoutError):
                        discovery.recv(8192)
                else:
                    announcement = json.loads(discovery.recv(8192))
                    assert announcement["wsPort"] == port
                    assert announcement["protocol"] == "qtPilot-discovery"
            finally:
                _stop_group(process)


@pytest.mark.parametrize("inherited", ["local", "remote", "trusted-network"])
def test_empty_cli_profile_cannot_erase_a_restriction(inherited: str) -> None:
    environment = _app_env(str(BUILD.qt_prefix) if BUILD.qt_prefix else None)
    environment["QTPILOT_PROFILE"] = inherited
    result = subprocess.run([str(BUILD.launcher), "--profile", "", "synthetic-target-must-not-launch"],
                            env=environment, capture_output=True, text=True, timeout=5)
    assert result.returncode != 0
    assert "Invalid operating profile" in result.stderr
