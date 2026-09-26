"""Shared test fixtures for qtPilot unit tests."""

from __future__ import annotations

import asyncio
import json
import os
import warnings
from collections.abc import Generator
from unittest.mock import AsyncMock, MagicMock, patch

# Filter third-party deprecation warnings inside older FastMCP/Authlib packages
try:
    from authlib.deprecate import AuthlibDeprecationWarning

    warnings.filterwarnings("ignore", category=AuthlibDeprecationWarning)
except ImportError:
    pass

try:
    from fastmcp.exceptions import FastMCPDeprecationWarning

    warnings.filterwarnings("ignore", category=FastMCPDeprecationWarning)
except ImportError:
    pass

import pytest
import pytest_asyncio
from pluggy import Result as HookResult

from fastmcp import FastMCP

from qtpilot.connection import ProbeConnection


def pytest_configure(config: pytest.Config) -> None:
    config.addinivalue_line("markers", "real_probe: executes a native application through the real injected probe")


def pytest_collection_finish(session: pytest.Session) -> None:
    if os.environ.get("QTPILOT_REQUIRE_E2E") != "1":
        return
    if not any(item.get_closest_marker("real_probe") for item in session.items):
        raise pytest.UsageError("Required E2E selected no real-probe tests")
    from pathlib import Path
    from tests.probe_app import probe_build

    directory = Path(os.environ.get("QTPILOT_TEST_BUILD_DIR", str(Path(__file__).resolve().parents[2] / "build")))
    build = probe_build(directory)
    if not build.available:
        raise pytest.UsageError(f"Required E2E binaries missing from {directory}")


@pytest.hookimpl(hookwrapper=True)
def pytest_runtest_makereport(
    item: pytest.Item, call: pytest.CallInfo[None],
) -> Generator[None, HookResult[pytest.TestReport], None]:
    outcome = yield
    report = outcome.get_result()
    if os.environ.get("QTPILOT_REQUIRE_E2E") == "1" and item.get_closest_marker("real_probe") and report.skipped:
        report.outcome = "failed"
        report.longrepr = f"Required E2E unexpectedly skipped: {item.nodeid}"


class MockWebSocket:
    """Simulates a WebSocket connection for unit testing.

    Records sent messages and returns pre-configured responses
    keyed by JSON-RPC request id.
    """

    def __init__(self) -> None:
        self.sent_messages: list[str] = []
        self.responses: dict[int, dict] = {}
        self._pending_responses: asyncio.Queue[str] = asyncio.Queue()
        self._closed = False

    async def send(self, msg: str) -> None:
        """Record sent message and queue the matching response."""
        self.sent_messages.append(msg)
        parsed = json.loads(msg)
        req_id = parsed.get("id")
        if req_id is not None and req_id in self.responses:
            resp = self.responses[req_id]
            await self._pending_responses.put(json.dumps(resp))

    async def recv(self) -> str:
        """Return the next queued response."""
        return await self._pending_responses.get()

    def __aiter__(self):
        return self

    async def __anext__(self) -> str:
        try:
            return await asyncio.wait_for(self._pending_responses.get(), timeout=0.5)
        except asyncio.TimeoutError:
            raise StopAsyncIteration

    async def inject_notification(self, notification: dict) -> None:
        """Simulate a probe push notification (no id, has method)."""
        await self._pending_responses.put(json.dumps(notification))

    async def close(self) -> None:
        self._closed = True


@pytest_asyncio.fixture
async def mock_ws():
    """Provide a MockWebSocket instance."""
    return MockWebSocket()


@pytest_asyncio.fixture
async def mock_probe(mock_ws):
    """Create a ProbeConnection with a patched WebSocket.

    The connection is already in the 'connected' state with the
    mock WebSocket wired in.
    """
    probe = ProbeConnection("ws://localhost:9222")

    mock_connect = AsyncMock(return_value=mock_ws)
    with patch("qtpilot.connection.connect", mock_connect):
        await probe.connect()

    yield probe, mock_ws

    if probe.is_connected:
        await probe.disconnect()


@pytest.fixture
def mock_mcp():
    """Create a FastMCP instance for tool registration testing."""
    return FastMCP("test")
