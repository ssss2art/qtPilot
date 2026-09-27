"""Configured authentication cannot degrade into ordinary unauthenticated access."""

from __future__ import annotations

import json
import logging
import secrets
from pathlib import Path
from unittest.mock import AsyncMock, patch

import pytest

from qtpilot.connection import ProbeConnection
from tests.conftest import MockWebSocket


@pytest.fixture
def credential(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> str:
    token = secrets.token_urlsafe(32)
    path = tmp_path / "token"
    path.write_text(token + "\n")
    monkeypatch.setenv("QTPILOT_AUTH_TOKEN_FILE", str(path))
    return token


@pytest.mark.asyncio
@pytest.mark.parametrize("url", ["ws://192.0.2.1:9222", "ws://localhost:9222", "ws://fixture.invalid:9222"])
async def test_credentials_never_cross_unverified_plaintext_networks(credential: str, url: str) -> None:
    connection = ProbeConnection(url)
    with patch("qtpilot.connection.connect", AsyncMock(return_value=MockWebSocket())) as transport:
        try:
            with pytest.raises(ValueError, match="TLS|loopback"):
                await connection.connect()
            transport.assert_not_called()
        finally:
            await connection.disconnect()


@pytest.mark.asyncio
@pytest.mark.parametrize("profile", ["remote", "trusted-network", "locla"])
async def test_profiles_fail_closed_before_connecting(monkeypatch: pytest.MonkeyPatch, profile: str) -> None:
    monkeypatch.setenv("QTPILOT_PROFILE", profile)
    connection = ProbeConnection("ws://127.0.0.1:9222")
    with patch("qtpilot.connection.connect", AsyncMock(return_value=MockWebSocket())) as transport:
        try:
            with pytest.raises(ValueError, match="profile|authentication|TLS"):
                await connection.connect()
            transport.assert_not_called()
        finally:
            await connection.disconnect()


@pytest.mark.asyncio
async def test_authenticated_connect_sends_header_and_waits_for_admission(credential: str) -> None:
    socket = MockWebSocket()
    await socket.inject_notification({"jsonrpc": "2.0", "method": "qtpilot.authenticated", "params": {"protocolVersion": 1}})
    connection = ProbeConnection("ws://127.0.0.1:9222")
    with patch("qtpilot.connection.connect", AsyncMock(return_value=socket)) as transport:
        try:
            await connection.connect()
            assert transport.call_args.kwargs.get("additional_headers") == {"Authorization": f"Bearer {credential}"}
            assert connection.is_connected
            assert socket.sent_messages == [], "Admission must not dispatch an application method"
        finally:
            await connection.disconnect()


@pytest.mark.asyncio
async def test_legacy_peer_cannot_satisfy_authenticated_mode(credential: str) -> None:
    socket = MockWebSocket()
    await socket.inject_notification({"jsonrpc": "2.0", "result": "pong", "id": 1})
    connection = ProbeConnection("ws://127.0.0.1:9222")
    with patch("qtpilot.connection.connect", AsyncMock(return_value=socket)):
        try:
            with pytest.raises(ConnectionError, match="admission"):
                await connection.connect()
            assert not connection.is_connected
            assert socket._closed
        finally:
            await connection.disconnect()


@pytest.mark.asyncio
async def test_missing_credential_file_never_falls_back(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv("QTPILOT_AUTH_TOKEN_FILE", str(tmp_path / "absent"))
    connection = ProbeConnection("ws://127.0.0.1:9222")
    with patch("qtpilot.connection.connect", AsyncMock(return_value=MockWebSocket())) as transport:
        try:
            with pytest.raises(ValueError, match="credential"):
                await connection.connect()
            transport.assert_not_called()
        finally:
            await connection.disconnect()


@pytest.mark.parametrize("url", ["ws://name:secret@localhost:9222", "ws://localhost:9222?token=secret", "ws://localhost:9222#secret"])
def test_secret_bearing_urls_are_rejected_without_echo(url: str) -> None:
    with pytest.raises(ValueError) as failure:
        ProbeConnection(url)
    assert "secret" not in str(failure.value)


@pytest.mark.asyncio
async def test_transport_debug_log_redacts_authorization(credential: str, caplog: pytest.LogCaptureFixture) -> None:
    socket = MockWebSocket()
    await socket.inject_notification({"jsonrpc": "2.0", "method": "qtpilot.authenticated", "params": {"protocolVersion": 1}})
    connection = ProbeConnection("ws://127.0.0.1:9222")
    with patch("qtpilot.connection.connect", AsyncMock(return_value=socket)) as transport:
        try:
            await connection.connect()
            wire_logger = transport.call_args.kwargs.get("logger", logging.getLogger("websockets.client"))
            with caplog.at_level(logging.DEBUG):
                wire_logger.debug("> %s: %s", "Authorization", f"Bearer {credential}")
            assert credential not in caplog.text
            assert "[redacted]" in caplog.text
        finally:
            await connection.disconnect()


@pytest.mark.asyncio
async def test_probe_connection_refuses_redirects() -> None:
    from qtpilot.connection import connect
    from websockets.datastructures import Headers
    from websockets.exceptions import InvalidStatus
    from websockets.http11 import Response

    connector = connect("wss://localhost:9222")
    error = InvalidStatus(Response(302, "Found", Headers({"Location": "wss://other.invalid"})))
    assert isinstance(connector.process_redirect(error), Exception), "Probe endpoint silently redirected"
