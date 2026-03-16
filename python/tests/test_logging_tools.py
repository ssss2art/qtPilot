"""Unit tests for logging tool registration."""

from __future__ import annotations

import pytest

from fastmcp import FastMCP

from qtpilot.tools.logging_tools import register_logging_tools


def _tool_names(mcp: FastMCP) -> set[str]:
    """Extract registered tool names from a FastMCP instance."""
    return set(mcp._tool_manager._tools.keys())


class TestLoggingTools:
    def test_logging_tools_registered(self, mock_mcp):
        """Logging tools register exactly 4 tools."""
        register_logging_tools(mock_mcp)
        assert len(_tool_names(mock_mcp)) == 4

    def test_logging_tool_names(self, mock_mcp):
        """All logging tool names are present."""
        register_logging_tools(mock_mcp)
        names = _tool_names(mock_mcp)
        expected = {
            "qtpilot_log_start",
            "qtpilot_log_stop",
            "qtpilot_log_status",
            "qtpilot_log_tail",
        }
        missing = expected - names
        assert not missing, f"Missing logging tools: {missing}"
