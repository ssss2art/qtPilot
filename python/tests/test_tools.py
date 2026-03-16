"""Unit tests for tool registration and method mapping."""

from __future__ import annotations

import pytest

from fastmcp import FastMCP

from qtpilot.tools.native import register_native_tools
from qtpilot.tools.cu import register_cu_tools
from qtpilot.tools.chrome import register_chrome_tools


def _tool_names(mcp: FastMCP) -> set[str]:
    """Extract registered tool names from a FastMCP instance."""
    return set(mcp._tool_manager._tools.keys())


class TestNativeTools:
    def test_native_tools_registered(self, mock_mcp):
        """Native mode registers >= 32 tools."""
        register_native_tools(mock_mcp)
        assert len(_tool_names(mock_mcp)) >= 32

    def test_native_tool_names(self, mock_mcp):
        """Key native tool names are present."""
        register_native_tools(mock_mcp)
        names = _tool_names(mock_mcp)
        expected = {
            "qt_ping",
            "qt_objects_find",
            "qt_objects_findByClass",
            "qt_objects_tree",
            "qt_objects_info",
            "qt_objects_inspect",
            "qt_properties_get",
            "qt_properties_set",
            "qt_properties_list",
            "qt_methods_list",
            "qt_methods_invoke",
            "qt_signals_list",
            "qt_signals_subscribe",
            "qt_ui_click",
            "qt_ui_screenshot",
            "qt_ui_sendKeys",
            "qt_models_list",
            "qt_models_info",
            "qt_models_data",
            "qt_qml_inspect",
            "qt_names_register",
            "qt_names_list",
        }
        missing = expected - names
        assert not missing, f"Missing native tools: {missing}"


class TestCuTools:
    def test_cu_tools_registered(self, mock_mcp):
        """Computer Use mode registers exactly 13 tools."""
        register_cu_tools(mock_mcp)
        assert len(_tool_names(mock_mcp)) == 13

    def test_cu_tool_names(self, mock_mcp):
        """Key CU tool names are present."""
        register_cu_tools(mock_mcp)
        names = _tool_names(mock_mcp)
        expected = {
            "cu_screenshot",
            "cu_leftClick",
            "cu_rightClick",
            "cu_doubleClick",
            "cu_type",
            "cu_key",
            "cu_scroll",
            "cu_cursorPosition",
            "cu_mouseMove",
            "cu_mouseDrag",
            "cu_mouseDown",
            "cu_mouseUp",
            "cu_middleClick",
        }
        missing = expected - names
        assert not missing, f"Missing CU tools: {missing}"


class TestChromeTools:
    def test_chrome_tools_registered(self, mock_mcp):
        """Chrome mode registers exactly 8 tools."""
        register_chrome_tools(mock_mcp)
        assert len(_tool_names(mock_mcp)) == 8

    def test_chrome_tool_names(self, mock_mcp):
        """Key Chrome tool names are present."""
        register_chrome_tools(mock_mcp)
        names = _tool_names(mock_mcp)
        expected = {
            "chr_readPage",
            "chr_click",
            "chr_find",
            "chr_formInput",
            "chr_getPageText",
            "chr_navigate",
            "chr_tabsContext",
            "chr_readConsoleMessages",
        }
        missing = expected - names
        assert not missing, f"Missing Chrome tools: {missing}"
