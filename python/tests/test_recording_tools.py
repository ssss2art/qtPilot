"""Unit tests for recording tool registration."""

from __future__ import annotations

import asyncio

import pytest

from fastmcp import FastMCP

from qtpilot import _mcp_compat as mcp_compat

from qtpilot.tools.recording_tools import register_recording_tools


def _tool_names(mcp: FastMCP) -> set[str]:
    """Extract registered tool names from a FastMCP instance."""
    # Enumeration is async on every FastMCP generation after 2.x; these tests
    # are sync, so drive the compat accessor on a throwaway loop.
    return set(asyncio.run(mcp_compat.list_tool_names(mcp)))


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
            "qtpilot_recording_start",
            "qtpilot_recording_stop",
            "qtpilot_recording_status",
        }
        missing = expected - names
        assert not missing, f"Missing recording tools: {missing}"
