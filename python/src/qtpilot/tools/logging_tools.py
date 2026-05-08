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
        buffer accessible via qtpilot_log_status(tail=N).

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
        The ring buffer is preserved -- use qtpilot_log_status(tail=N) to
        read entries after stopping.

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
    async def qtpilot_log_status(tail: int = 0, ctx: Context = None) -> dict:
        """Get current message logging status, and optionally tail recent entries.

        Returns logging state, file path, verbosity level, entry count, buffer
        size, and session duration. When `tail > 0`, also returns the most
        recent `tail` entries from the in-memory ring buffer.

        Args:
            tail: When >0, include that many most-recent entries under the
                  `entries` key. When 0 (default), no entries are returned --
                  cheap status only.

        Example: qtpilot_log_status(tail=10)
        """
        from qtpilot.server import get_message_logger

        if tail < 0:
            raise ValueError("tail must be >= 0")

        logger = get_message_logger()
        raw = logger.status()

        # Rename `entries` count -> `entry_count` so the `entries` key is
        # available for the list form when requested.
        status = {k: v for k, v in raw.items() if k != "entries"}
        if "entries" in raw:
            status["entry_count"] = raw["entries"]

        if tail > 0:
            status["entries"] = logger.tail(count=tail)["entries"]

        return status

