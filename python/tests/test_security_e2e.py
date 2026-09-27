"""Real injected-probe admission must authenticate both sides before application traffic."""

from __future__ import annotations

import asyncio
import json
import socket
import secrets
import subprocess
from collections.abc import Iterator
from dataclasses import dataclass, field
from pathlib import Path

import pytest

from qtpilot.connection import ProbeConnection
from qtpilot.message_logger import MessageLogger
from tests.test_complicated_app_e2e import BUILD, _app_env, _free_port, _stop_group, _wait_for_port

pytestmark = [pytest.mark.real_probe, pytest.mark.skipif(not BUILD.available, reason="Native application not built")]


@dataclass(frozen=True)
class SecureApplication:
    url: str
    token: str = field(repr=False)
    token_file: Path
    certificate: Path
    process_log: Path
    announcement: bytes | None


@pytest.fixture
def secure_app(tmp_path: Path, monkeypatch: pytest.MonkeyPatch, request: pytest.FixtureRequest) -> Iterator[SecureApplication]:
    token = secrets.token_urlsafe(32)
    token_file = tmp_path / "token"
    token_file.write_text(token + "\n")
    token_file.chmod(0o600)
    certificate, private_key = tmp_path / "cert.pem", tmp_path / "key.pem"
    subprocess.run([
        "openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
        "-subj", "/CN=synthetic-fixture", "-addext", "subjectAltName=IP:127.0.0.1",
        "-keyout", str(private_key), "-out", str(certificate),
    ], check=True, capture_output=True)
    profile = getattr(request, "param", "remote")
    monkeypatch.setenv("QTPILOT_PROFILE", profile)
    monkeypatch.setenv("QTPILOT_AUTH_TOKEN_FILE", str(token_file))
    monkeypatch.setenv("QTPILOT_TLS_CA_FILE", str(certificate))
    environment = _app_env(str(BUILD.qt_prefix) if BUILD.qt_prefix else None)
    environment.update(QTPILOT_TLS_CERT_FILE=str(certificate), QTPILOT_TLS_KEY_FILE=str(private_key))
    environment["QTPILOT_BIND_ADDRESS"] = "any" if profile == "trusted-network" else "loopback"
    port = _free_port()
    process_log = tmp_path / "process.log"
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as discovery, process_log.open("w") as output:
        discovery.bind(("0.0.0.0", 0))
        discovery.settimeout(0.3)
        environment["QTPILOT_DISCOVERY_PORT"] = str(discovery.getsockname()[1])
        process = subprocess.Popen(
            [str(BUILD.launcher), "--port", str(port), str(BUILD.application)],
            env=environment, stdout=output, stderr=output, start_new_session=True,
        )
        try:
            _wait_for_port(port, process, timeout=10)
            try:
                announcement = discovery.recv(8192)
            except TimeoutError:
                announcement = None
            yield SecureApplication(f"wss://127.0.0.1:{port}", token, token_file, certificate, process_log, announcement)
        finally:
            _stop_group(process)
            assert token not in process_log.read_text(), "Probe output disclosed the credential"


def test_verified_tls_and_token_admit_real_controller(secure_app: SecureApplication, tmp_path: Path) -> None:
    async def check() -> None:
        connection = ProbeConnection(secure_app.url)
        recording = MessageLogger()
        path = tmp_path / "recording.jsonl"
        recording.start(path=str(path), level=2)
        recording.attach(connection)
        try:
            await connection.connect()
            response = await connection.call("qt.ping")
            assert response["result"]["pong"] is True
        finally:
            recording.detach(connection)
            recording.stop()
            await connection.disconnect()
        assert secure_app.token not in path.read_text(), "Recording disclosed the credential"
    asyncio.run(check())


def test_wrong_token_is_denied_and_does_not_reserve_the_session(secure_app: SecureApplication, tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    wrong_file = tmp_path / "wrong-token"
    wrong_file.write_text(secrets.token_urlsafe(32))
    monkeypatch.setenv("QTPILOT_AUTH_TOKEN_FILE", str(wrong_file))

    async def check() -> None:
        rejected = ProbeConnection(secure_app.url)
        with pytest.raises(ConnectionError, match="admission"):
            await rejected.connect()
        assert not rejected.is_connected
        monkeypatch.setenv("QTPILOT_AUTH_TOKEN_FILE", str(secure_app.token_file))
        accepted = ProbeConnection(secure_app.url)
        try:
            await accepted.connect()
            assert (await accepted.call("qt.ping"))["result"]["pong"] is True
        finally:
            await accepted.disconnect()
    asyncio.run(check())


@pytest.mark.parametrize("bad_identity", ["untrusted-ca", "wrong-hostname"])
def test_tls_server_identity_is_verified(secure_app: SecureApplication, monkeypatch: pytest.MonkeyPatch, bad_identity: str) -> None:
    url = secure_app.url
    if bad_identity == "untrusted-ca":
        monkeypatch.delenv("QTPILOT_TLS_CA_FILE")
    else:
        url = url.replace("127.0.0.1", "localhost")

    async def check() -> None:
        connection = ProbeConnection(url)
        with pytest.raises(ConnectionError, match="SSLCertVerificationError"):
            await connection.connect()
        assert not connection.is_connected
    asyncio.run(check())


def test_remote_profile_refuses_plaintext_downgrade(secure_app: SecureApplication) -> None:
    async def check() -> None:
        connection = ProbeConnection(secure_app.url.replace("wss:", "ws:"))
        with pytest.raises(ValueError, match="TLS"):
            await connection.connect()
        assert not connection.is_connected
    asyncio.run(check())


def test_remote_profile_never_announces(secure_app: SecureApplication) -> None:
    assert secure_app.announcement is None, "Remote profile disclosed discovery metadata"


@pytest.mark.parametrize("secure_app", ["trusted-network"], indirect=True)
def test_trusted_network_announces_secure_transport(secure_app: SecureApplication) -> None:
    assert secure_app.announcement is not None, "Trusted-network profile lost LAN discovery"
    packet = json.loads(secure_app.announcement)
    assert packet.get("tls") is True, "Announcement would offer a plaintext endpoint"
    assert packet.get("authRequired") is True, "Announcement omitted authenticated admission"
    assert secure_app.token.encode() not in secure_app.announcement
