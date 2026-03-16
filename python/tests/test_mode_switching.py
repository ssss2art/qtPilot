"""Unit tests for dynamic mode switching."""

from __future__ import annotations

import pytest

from fastmcp import FastMCP

from qtpilot.server import ServerState, _MODE_PREFIXES, _register_mode_tools_if_absent


def _tool_names(mcp: FastMCP) -> set[str]:
    return set(mcp._tool_manager._tools.keys())


@pytest.fixture
def state():
    """Create a ServerState with all three mode tool sets registered."""
    mcp = FastMCP("test")
    for mode_key in _MODE_PREFIXES:
        _register_mode_tools_if_absent(mcp, mode_key)
    return ServerState(mcp, mode="all")


class TestSetMode:
    def test_switch_from_all_to_native(self, state):
        result = state.set_mode("native")
        assert result["changed"] is True
        assert result["mode"] == "native"
        assert result["previous_mode"] == "all"
        names = _tool_names(state.mcp)
        assert any(n.startswith("qt_") for n in names)
        assert not any(n.startswith("cu_") for n in names)
        assert not any(n.startswith("chr_") for n in names)

    def test_switch_from_all_to_cu(self, state):
        state.set_mode("cu")
        names = _tool_names(state.mcp)
        assert any(n.startswith("cu_") for n in names)
        assert not any(n.startswith("qt_") for n in names)
        assert not any(n.startswith("chr_") for n in names)

    def test_switch_from_all_to_chrome(self, state):
        state.set_mode("chrome")
        names = _tool_names(state.mcp)
        assert any(n.startswith("chr_") for n in names)
        assert not any(n.startswith("qt_") for n in names)
        assert not any(n.startswith("cu_") for n in names)

    def test_switch_from_native_to_all(self, state):
        state.set_mode("native")
        result = state.set_mode("all")
        assert result["changed"] is True
        names = _tool_names(state.mcp)
        assert any(n.startswith("qt_") for n in names)
        assert any(n.startswith("cu_") for n in names)
        assert any(n.startswith("chr_") for n in names)

    def test_switch_from_native_to_cu(self, state):
        state.set_mode("native")
        state.set_mode("cu")
        names = _tool_names(state.mcp)
        assert any(n.startswith("cu_") for n in names)
        assert not any(n.startswith("qt_") for n in names)

    def test_same_mode_no_change(self, state):
        result = state.set_mode("all")
        assert result["changed"] is False
        assert result["mode"] == "all"

    def test_invalid_mode_returns_error(self, state):
        result = state.set_mode("invalid")
        assert "error" in result

    def test_no_duplicate_tools_on_roundtrip(self, state):
        """Switching away and back should not duplicate tools."""
        initial = _tool_names(state.mcp)
        state.set_mode("native")
        state.set_mode("all")
        after = _tool_names(state.mcp)
        assert initial == after


class TestCreateServerModes:
    def test_default_mode_is_native(self):
        from qtpilot.server import create_server, get_state

        mcp = create_server()
        state = get_state()
        assert state.mode == "native"
        names = _tool_names(mcp)
        assert any(n.startswith("qt_") for n in names)
        assert not any(n.startswith("cu_") for n in names)
        assert not any(n.startswith("chr_") for n in names)

    def test_all_mode_registers_everything(self):
        from qtpilot.server import create_server

        mcp = create_server(mode="all")
        names = _tool_names(mcp)
        assert any(n.startswith("qt_") for n in names)
        assert any(n.startswith("cu_") for n in names)
        assert any(n.startswith("chr_") for n in names)

    def test_discovery_tools_always_registered(self):
        from qtpilot.server import create_server

        mcp = create_server(mode="native")
        names = _tool_names(mcp)
        assert "qtpilot_list_probes" in names
        assert "qtpilot_connect_probe" in names
        assert "qtpilot_set_mode" in names
        assert "qtpilot_get_mode" in names

    def test_recording_tools_always_registered(self):
        from qtpilot.server import create_server

        mcp = create_server(mode="cu")
        names = _tool_names(mcp)
        assert "qtpilot_start_recording" in names
        assert "qtpilot_stop_recording" in names
