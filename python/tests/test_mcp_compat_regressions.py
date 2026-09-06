"""Regressions for defects the existing suite could not see.

Both bugs here survived a full green test run, for the same reason in each case:
the tests exercised the layer *underneath* the bug. `test_mode_switching.py`
calls `state.set_mode()` directly, so it never touched the MCP tool wrapper that
was raising; and nothing constructed two servers in one process, so per-server
isolation was never asserted.
"""

from __future__ import annotations

import asyncio

import pytest
from fastmcp import Client

from qtpilot import _mcp_compat as mcp_compat
from qtpilot.server import create_server


# ---------------------------------------------------------------------------
# qtpilot_set_mode through the real MCP tool boundary
# ---------------------------------------------------------------------------
# The wrapper called Context.send_tool_list_changed(), which FastMCP 4 removed.
# Every SUCCESSFUL switch raised AttributeError -- after set_mode() had already
# mutated the server, so the client saw a hard error for an operation that had
# actually happened. Calling the tool through a Client is what catches it;
# calling ServerState.set_mode() directly does not.


@pytest.mark.asyncio
async def test_set_mode_tool_succeeds_through_a_client():
    server = create_server(mode="native")
    async with Client(server) as client:
        result = await client.call_tool("qtpilot_set_mode", {"mode": "chrome"})

    assert result.data["ok"] is True
    assert result.data["mode"] == "chrome"
    assert result.data["previous_mode"] == "native"


@pytest.mark.asyncio
async def test_set_mode_tool_actually_narrows_the_tool_surface():
    server = create_server(mode="native")
    async with Client(server) as client:
        before = {t.name for t in await client.list_tools()}
        await client.call_tool("qtpilot_set_mode", {"mode": "chrome"})
        after = {t.name for t in await client.list_tools()}

    assert any(n.startswith("qt_") for n in before)
    assert not any(n.startswith("qt_") for n in after)
    assert any(n.startswith("chr_") for n in after)


@pytest.mark.asyncio
async def test_switching_to_the_current_mode_is_a_no_op():
    server = create_server(mode="native")
    async with Client(server) as client:
        first = {t.name for t in await client.list_tools()}
        result = await client.call_tool("qtpilot_set_mode", {"mode": "native"})
        second = {t.name for t in await client.list_tools()}

    assert result.data["ok"] is True
    assert first == second


class _NoNotificationContext:
    """A Context offering neither notification API."""


class _LegacyContext:
    """FastMCP 2.x/3.x shape."""

    def __init__(self) -> None:
        self.called = False

    async def send_tool_list_changed(self) -> None:
        self.called = True


class _RaisingContext:
    def __init__(self) -> None:
        self.called = False

    async def send_tool_list_changed(self) -> None:
        self.called = True
        raise RuntimeError("client went away mid-notification")


@pytest.mark.asyncio
async def test_notify_uses_the_legacy_api_when_present():
    ctx = _LegacyContext()
    assert await mcp_compat.notify_tool_list_changed(ctx) is True
    assert ctx.called


@pytest.mark.asyncio
async def test_notify_is_a_no_op_when_no_api_exists():
    """Must not raise: the notification is a cache hint, not part of the switch."""
    assert await mcp_compat.notify_tool_list_changed(_NoNotificationContext()) is False


@pytest.mark.asyncio
async def test_notify_swallows_a_failing_send():
    """A dead client must not fail a mode switch that already took effect."""
    ctx = _RaisingContext()
    assert await mcp_compat.notify_tool_list_changed(ctx) is False
    assert ctx.called


# ---------------------------------------------------------------------------
# The "unknown" revision sentinel
# ---------------------------------------------------------------------------
# Revisions are ISO dates compared as strings, which sorts correctly -- except
# the sentinel, where "u" > "2" made an unidentifiable SDK compare as newer than
# every real revision and report itself as stateless.


def test_unknown_revision_is_not_treated_as_stateless(monkeypatch):
    monkeypatch.setattr(mcp_compat, "protocol_revision", lambda: mcp_compat.REVISION_UNKNOWN)
    assert mcp_compat.is_stateless_protocol() is False
    assert mcp_compat.describe()["stateless_protocol"] is False


def test_the_raw_string_comparison_really_is_the_trap():
    """Pins the reason the guard exists, so removing it fails loudly."""
    assert mcp_compat.REVISION_UNKNOWN >= mcp_compat.REVISION_STATELESS


def test_legacy_revision_is_not_stateless(monkeypatch):
    monkeypatch.setattr(mcp_compat, "protocol_revision", lambda: mcp_compat.REVISION_LEGACY)
    assert mcp_compat.is_stateless_protocol() is False


def test_stateless_revision_is_stateless(monkeypatch):
    monkeypatch.setattr(mcp_compat, "protocol_revision", lambda: mcp_compat.REVISION_STATELESS)
    assert mcp_compat.is_stateless_protocol() is True


def test_describe_agrees_with_is_stateless_protocol():
    """describe() re-implemented the comparison and so duplicated its bug."""
    assert mcp_compat.describe()["stateless_protocol"] is mcp_compat.is_stateless_protocol()


# ---------------------------------------------------------------------------
# Per-server isolation
# ---------------------------------------------------------------------------
# The visibility transform closed over the process-global state, which
# create_server() reassigns. Because the transform is evaluated per request, an
# already-built server retargeted to a later server's mode.


@pytest.mark.asyncio
async def test_building_a_second_server_does_not_retarget_the_first():
    async def tool_names(server) -> set[str]:
        async with Client(server) as client:
            return {t.name for t in await client.list_tools()}

    first = create_server(mode="native")
    before = await tool_names(first)

    create_server(mode="chrome")
    after = await tool_names(first)

    assert before == after, "constructing a second server rewrote the first server's tools"
    assert any(n.startswith("qt_") for n in after)


@pytest.mark.asyncio
async def test_two_servers_keep_independent_surfaces():
    async def tool_names(server) -> set[str]:
        async with Client(server) as client:
            return {t.name for t in await client.list_tools()}

    native = create_server(mode="native")
    chrome = create_server(mode="chrome")

    native_names = await tool_names(native)
    chrome_names = await tool_names(chrome)

    assert any(n.startswith("qt_") for n in native_names)
    assert any(n.startswith("chr_") for n in chrome_names)
    assert not any(n.startswith("chr_") for n in native_names)
