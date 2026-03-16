"""Logging tools -- start/stop message logging and tail recent entries."""

from __future__ import annotations

from fastmcp import Context, FastMCP


def register_logging_tools(mcp: FastMCP) -> None:
    """Register message logging tools on the MCP server."""

    @mcp.tool
    async def qtpilot_log_start(
        path: str | None = None,
        level: int = 2,
        buffer_size: int = 200,
        ctx: Context = None,
    ) -> dict:
        """Start logging MCP and JSON-RPC messages to a file.

        Captures tool calls, probe wire traffic, and notifications at
        configurable verbosity levels. Also maintains an in-memory ring
        buffer accessible via qtpilot_log_tail.

        Args:
            path: Output file path. Default: qtPilot-log-YYYYMMDD-HHMMSS.jsonl in cwd.
            level: Verbosity level:
                1 = minimal (MCP tool calls only)
                2 = normal (+ JSON-RPC wire traffic) [default]
                3 = verbose (+ probe notifications)
            buffer_size: Ring buffer capacity (default 200 entries).

        Example: qtpilot_log_start(level=3)
        """
        from qtpilot.server import get_message_logger, get_probe

        logger = get_message_logger()
        result = logger.start(path=path, level=level, buffer_size=buffer_size)

        # Attach to current probe if connected
        probe = get_probe()
        if probe is not None and probe.is_connected:
            logger.attach(probe)

        return result

    @mcp.tool
    async def qtpilot_log_stop(ctx: Context = None) -> dict:
        """Stop message logging and return summary.

        Returns the file path, total entries logged, and session duration.
        The ring buffer is preserved -- use qtpilot_log_tail to read entries
        after stopping.

        Example: qtpilot_log_stop()
        """
        from qtpilot.server import get_message_logger, get_probe

        logger = get_message_logger()

        # Detach from probe before stopping
        probe = get_probe()
        if probe is not None and logger._attached_probe is probe:
            logger.detach(probe)

        return logger.stop()

    @mcp.tool
    async def qtpilot_log_status(ctx: Context = None) -> dict:
        """Get current message logging status.

        Returns whether logging is active, file path, verbosity level,
        entry count, buffer size, and session duration.

        Example: qtpilot_log_status()
        """
        from qtpilot.server import get_message_logger

        return get_message_logger().status()

    @mcp.tool
    async def qtpilot_log_tail(count: int = 50, ctx: Context = None) -> dict:
        """Return recent log entries from the in-memory ring buffer.

        Works even after qtpilot_log_stop -- the buffer persists until the
        next qtpilot_log_start.

        Args:
            count: Number of recent entries to return (default 50).

        Example: qtpilot_log_tail(count=10)
        """
        from qtpilot.server import get_message_logger

        return get_message_logger().tail(count=count)
