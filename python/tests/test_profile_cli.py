"""CLI profile choices must survive startup without widening network exposure."""

from __future__ import annotations

import os
from collections.abc import Iterator
from pathlib import Path
from unittest.mock import AsyncMock, MagicMock, patch

import pytest
from fastmcp import Client

from qtpilot.cli import cmd_serve, create_parser
from qtpilot.server import create_server, get_discovery
from tests.test_replay_cli import CLICK_SESSION, args_for, cmd_replay, write_log


@pytest.fixture(autouse=True)
def isolated_security_environment() -> Iterator[None]:
    with patch.dict(os.environ):
        for key in ("QTPILOT_PROFILE", "QTPILOT_AUTH_TOKEN_FILE", "QTPILOT_TLS_CA_FILE",
                    "QTPILOT_TLS_CERT_FILE", "QTPILOT_TLS_KEY_FILE"):
            os.environ.pop(key, None)
        yield


@pytest.mark.parametrize("command", [["serve"], ["demo"], ["replay", "session.jsonl"]])
def test_all_drivers_accept_explicit_profile_credentials(command: list[str]) -> None:
    args = create_parser().parse_args(command + [
        "--profile", "remote", "--auth-token-file", "token-file", "--tls-ca-file", "ca-file",
    ])
    assert (args.profile, args.auth_token_file, args.tls_ca_file) == ("remote", "token-file", "ca-file")


@pytest.mark.parametrize("profile,tls,expected", [
    ("", False, "ws://localhost:9333"),
    ("local", False, "ws://127.0.0.1:9333"),
    ("remote", True, "wss://127.0.0.1:9333"),
])
def test_auto_launch_uses_profile_transport(profile: str, tls: bool, expected: str,
                                           monkeypatch: pytest.MonkeyPatch) -> None:
    if profile:
        monkeypatch.setenv("QTPILOT_PROFILE", profile)
    if tls:
        monkeypatch.setenv("QTPILOT_TLS_CERT_FILE", "cert-file")
        monkeypatch.setenv("QTPILOT_TLS_KEY_FILE", "key-file")
    args = create_parser().parse_args(["serve", "--target", "synthetic-fixture", "--port", "9333"])
    with patch("qtpilot.server.create_server", return_value=MagicMock()) as server:
        assert cmd_serve(args) == 0
    assert server.call_args.kwargs["ws_url"] == expected, "Auto-launch weakened the selected profile"


def test_explicit_cli_settings_override_the_same_environment_key(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv("QTPILOT_PROFILE", "remote")
    args = create_parser().parse_args(["serve", "--profile", "local", "--auth-token-file", "cli-token"])
    with patch("qtpilot.server.create_server", return_value=MagicMock()):
        assert cmd_serve(args) == 0
    assert os.environ["QTPILOT_PROFILE"] == "local"
    assert os.environ["QTPILOT_AUTH_TOKEN_FILE"] == "cli-token"


@pytest.mark.asyncio
@pytest.mark.parametrize("profile,discover", [("", True), ("local", False), ("remote", False), ("trusted-network", True)])
async def test_mcp_listener_honors_profile_discovery(profile: str, discover: bool,
                                                  monkeypatch: pytest.MonkeyPatch) -> None:
    if profile:
        monkeypatch.setenv("QTPILOT_PROFILE", profile)
    with patch("qtpilot.server.DiscoveryListener.start", new_callable=AsyncMock) as start:
        async with Client(create_server()) as client:
            await client.list_tools()
            assert (get_discovery() is not None) is discover, "Profile discovery boundary was ignored"
        assert start.await_count == int(discover)


def test_invalid_profile_fails_before_mcp_startup(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv("QTPILOT_PROFILE", "locla")
    with pytest.raises(ValueError, match="profile"):
        create_server()


def test_replay_reports_unsafe_endpoint_as_usage_without_secret_echo(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    args = args_for(write_log(tmp_path, CLICK_SESSION), ws_url="ws://name:secret@localhost:9222")
    assert cmd_replay(args) == 2
    assert "secret" not in capsys.readouterr().err


@pytest.mark.asyncio
async def test_mcp_startup_never_logs_rejected_url_secrets(caplog: pytest.LogCaptureFixture) -> None:
    async with Client(create_server(ws_url="ws://name:secret@localhost:9222", discovery_enabled=False)):
        pass
    assert "secret" not in caplog.text
