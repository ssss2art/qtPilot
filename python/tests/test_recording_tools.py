"""Unit tests for recording tool registration."""

from __future__ import annotations

import pytest

from fastmcp import FastMCP

from qtpilot.tools.recording_tools import register_recording_tools


def _tool_names(mcp: FastMCP) -> set[str]:
    """Extract registered tool names from a FastMCP instance."""
    return set(mcp._tool_manager._tools.keys())


class TestRecordingTools:
    def test_recording_tools_registered(self, mock_mcp):
        """Recording mode registers exactly 3 tools."""
        register_recording_tools(mock_mcp)
        assert len(_tool_names(mock_mcp)) == 3

    def test_recording_tool_names(self, mock_mcp):
        """All recording tool names are present."""
        register_recording_tools(mock_mcp)
        names = _tool_names(mock_mcp)
        expected = {
            "qtpilot_start_recording",
            "qtpilot_stop_recording",
            "qtpilot_recording_status",
        }
        missing = expected - names
        assert not missing, f"Missing recording tools: {missing}"
