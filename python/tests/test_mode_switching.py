"""Unit tests for dynamic mode switching."""

from __future__ import annotations

import pytest

from fastmcp import FastMCP

from qtpilot import _mcp_compat as mcp_compat
from qtpilot.server import (
    _MODE_PREFIXES,
    _MODE_VISIBILITY,
    ServerState,
    _register_mode_tools_if_absent,
)

# A mode switch narrows the tool surface either by filtering (transforms, the
# FastMCP 3+ path) or by unregistering tools (FastMCP 2.x). Every supported
# generation can do one or the other, so narrowing is always expected — the
# assertions below are unconditional on purpose.
_CAN_NARROW = mcp_compat.supports_transforms(
    FastMCP("capability-probe")
) or mcp_compat.supports_tool_removal(FastMCP("capability-probe"))


async def _tool_names(mcp: FastMCP) -> set[str]:
    return set(await mcp_compat.list_tool_names(mcp))


@pytest.fixture
def state():
    """Create a ServerState with all three mode tool sets registered.

    Mirrors what ``create_server`` does, including installing the mode
    visibility transform, so these tests exercise the real narrowing path
    rather than a raw server that would silently fall back to mutation.
    """
    mcp = FastMCP("test")
    state = ServerState(mcp, mode="all")
    _MODE_VISIBILITY[mcp] = mcp_compat.install_mode_visibility(
        mcp, lambda: state.mode, _MODE_PREFIXES
    )
    for mode_key in _MODE_PREFIXES:
        _register_mode_tools_if_absent(mcp, mode_key)
    return state


def test_every_supported_sdk_can_narrow_the_surface():
    """Guards the assumption the rest of this module is built on."""
    assert _CAN_NARROW


class TestSetMode:
    @pytest.mark.asyncio
    async def test_switch_from_all_to_native(self, state):
        result = await state.set_mode("native")
        assert result["changed"] is True
        assert result["mode"] == "native"
        assert result["previous_mode"] == "all"
        names = await _tool_names(state.mcp)
        assert any(n.startswith("qt_") for n in names)

    @pytest.mark.asyncio
    async def test_switch_from_all_to_native_hides_others(self, state):
        await state.set_mode("native")
        names = await _tool_names(state.mcp)
        assert not any(n.startswith("cu_") for n in names)
        assert not any(n.startswith("chr_") for n in names)

    @pytest.mark.asyncio
    async def test_switch_from_all_to_cu(self, state):
        await state.set_mode("cu")
        names = await _tool_names(state.mcp)
        assert any(n.startswith("cu_") for n in names)
        assert not any(n.startswith("qt_") for n in names)
        assert not any(n.startswith("chr_") for n in names)

    @pytest.mark.asyncio
    async def test_switch_from_all_to_chrome(self, state):
        await state.set_mode("chrome")
        names = await _tool_names(state.mcp)
        assert any(n.startswith("chr_") for n in names)
        assert not any(n.startswith("qt_") for n in names)
        assert not any(n.startswith("cu_") for n in names)

    @pytest.mark.asyncio
    async def test_switch_from_native_to_all(self, state):
        await state.set_mode("native")
        result = await state.set_mode("all")
        assert result["changed"] is True
        names = await _tool_names(state.mcp)
        assert any(n.startswith("qt_") for n in names)
        assert any(n.startswith("cu_") for n in names)
        assert any(n.startswith("chr_") for n in names)

    @pytest.mark.asyncio
    async def test_switch_from_native_to_cu(self, state):
        await state.set_mode("native")
        await state.set_mode("cu")
        names = await _tool_names(state.mcp)
        assert any(n.startswith("cu_") for n in names)
        assert not any(n.startswith("qt_") for n in names)

    @pytest.mark.asyncio
    async def test_same_mode_no_change(self, state):
        result = await state.set_mode("all")
        assert result["changed"] is False
        assert result["mode"] == "all"

    @pytest.mark.asyncio
    async def test_invalid_mode_returns_error(self, state):
        result = await state.set_mode("invalid")
        assert "error" in result

    @pytest.mark.asyncio
    async def test_switch_reports_no_removal_caveat(self, state):
        """Narrowing works on every supported SDK, so no caveat is emitted."""
        result = await state.set_mode("native")
        assert state.mode == "native"
        assert result["mode"] == "native"
        assert "tools_removed" not in result
        assert "note" not in result

    @pytest.mark.asyncio
    async def test_no_duplicate_tools_on_roundtrip(self, state):
        """Switching away and back should not duplicate tools."""
        initial = await _tool_names(state.mcp)
        await state.set_mode("native")
        await state.set_mode("all")
        after = await _tool_names(state.mcp)
        assert initial == after


class TestCreateServerModes:
    @pytest.mark.parametrize(
        ("mode", "expected_count"),
        [("native", 37), ("cu", 23), ("chrome", 18), ("all", 58)],
    )
    @pytest.mark.asyncio
    async def test_public_tool_counts(self, mode, expected_count):
        """Keep the documented MCP surface counts in sync with registration."""
        from qtpilot.server import create_server

        mcp = create_server(mode=mode)
        assert len(await _tool_names(mcp)) == expected_count

    @pytest.mark.asyncio
    async def test_default_mode_is_native(self):
        from qtpilot.server import create_server, get_state

        mcp = create_server()
        state = get_state()
        assert state.mode == "native"
        names = await _tool_names(mcp)
        assert any(n.startswith("qt_") for n in names)
        assert not any(n.startswith("cu_") for n in names)
        assert not any(n.startswith("chr_") for n in names)

    @pytest.mark.asyncio
    async def test_all_mode_registers_everything(self):
        from qtpilot.server import create_server

        mcp = create_server(mode="all")
        names = await _tool_names(mcp)
        assert any(n.startswith("qt_") for n in names)
        assert any(n.startswith("cu_") for n in names)
        assert any(n.startswith("chr_") for n in names)

    @pytest.mark.asyncio
    async def test_discovery_tools_always_registered(self):
        from qtpilot.server import create_server

        mcp = create_server(mode="native")
        names = await _tool_names(mcp)
        assert "qtpilot_connect_probe" in names
        assert "qtpilot_set_mode" in names
        assert "qtpilot_status" in names


class TestQtpilotStatus:
    @pytest.mark.asyncio
    async def test_qtpilot_status_structure(self):
        """qtpilot_status returns mode + available_modes + connection + discovery."""
        from qtpilot.server import create_server

        mcp = create_server(mode="native")
        tool = await mcp_compat.find_tool(mcp, "qtpilot_status")
        assert tool is not None
        result = await tool.fn(ctx=None)

        assert "mode" in result
        assert result["mode"] == "native"
        assert "available_modes" in result
        assert set(result["available_modes"]) == {"native", "cu", "chrome", "all"}
        assert "connection" in result
        assert "connected" in result["connection"]
        assert result["connection"]["connected"] is False  # no probe wired in test
        assert "discovery" in result
        assert "active" in result["discovery"]
        assert "probes" in result["discovery"]
        assert isinstance(result["discovery"]["probes"], list)

    @pytest.mark.asyncio
    async def test_recording_tools_always_registered(self):
        from qtpilot.server import create_server

        mcp = create_server(mode="cu")
        names = await _tool_names(mcp)
        assert "qtpilot_recording_start" in names
        assert "qtpilot_recording_stop" in names
