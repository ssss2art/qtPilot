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
        """Logging tools register exactly 3 tools."""
        register_logging_tools(mock_mcp)
        assert len(_tool_names(mock_mcp)) == 3

    def test_logging_tool_names(self, mock_mcp):
        """All logging tool names are present."""
        register_logging_tools(mock_mcp)
        names = _tool_names(mock_mcp)
        expected = {
            "qtpilot_log_start",
            "qtpilot_log_stop",
            "qtpilot_log_status",
        }
        missing = expected - names
        assert not missing, f"Missing logging tools: {missing}"


class TestLogStatusTail:
    """Behaviour of qtpilot_log_status(tail=N)."""

    @pytest.mark.asyncio
    async def test_log_status_default_no_entries_key(self):
        """Default (tail=0) returns status without the `entries` list key."""
        from qtpilot.server import create_server

        mcp = create_server(mode="native")
        status_fn = mcp._tool_manager._tools["qtpilot_log_status"].fn
        result = await status_fn(ctx=None)
        assert "entries" not in result
        # Count form is still available as entry_count when logging is active;
        # when inactive, neither key is present.

    @pytest.mark.asyncio
    async def test_log_status_with_tail_returns_list(self):
        """tail>0 returns the most-recent entries under `entries` as a list."""
        from qtpilot.server import create_server, get_message_logger

        mcp = create_server(mode="native")
        logger = get_message_logger()
        logger.start(buffer_size=50)
        try:
            logger.log_mcp_in("qt_ping", {})
            logger.log_mcp_in("qt_version", {})

            status_fn = mcp._tool_manager._tools["qtpilot_log_status"].fn
            result = await status_fn(tail=5, ctx=None)
            assert "entries" in result
            assert isinstance(result["entries"], list)
            assert len(result["entries"]) <= 5
            assert result["entry_count"] == 2
        finally:
            logger.stop()

    @pytest.mark.asyncio
    async def test_log_status_negative_tail_raises(self):
        from qtpilot.server import create_server

        mcp = create_server(mode="native")
        status_fn = mcp._tool_manager._tools["qtpilot_log_status"].fn
        with pytest.raises(ValueError):
            await status_fn(tail=-1, ctx=None)
