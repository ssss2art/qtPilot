"""FastMCP middleware that intercepts tool calls for message logging."""

from __future__ import annotations

import time

from fastmcp.server.middleware import Middleware


def _summarize_tool_result(result) -> str:
    """Extract a text summary from a ToolResult."""
    parts = []
    for block in getattr(result, "content", []):
        if getattr(block, "type", None) == "text":
            text = getattr(block, "text", "")
            if len(text) > 200:
                text = text[:200] + "..."
            parts.append(text)
        elif getattr(block, "type", None) == "image":
            parts.append("<image>")
        else:
            parts.append(f"<{getattr(block, 'type', 'unknown')}>")
    return " ".join(parts) if parts else str(result)


class LoggingMiddleware(Middleware):
    """Intercepts MCP tool calls to log request/response at the MCP layer."""

    async def on_call_tool(self, context, call_next):
        from qtpilot.server import get_message_logger

        logger = get_message_logger()
        tool_name = context.message.name
        args = context.message.arguments or {}

        # Skip logging our own tools to avoid recursion
        if tool_name.startswith("qtpilot_log_"):
            return await call_next(context)

        if not logger.is_active:
            return await call_next(context)

        logger.log_mcp_in(tool_name, args)

        t0 = time.monotonic()
        try:
            result = await call_next(context)
            duration_ms = (time.monotonic() - t0) * 1000
            summary = _summarize_tool_result(result)
            logger.log_mcp_out(tool_name, summary, duration_ms, is_error=False)
            return result
        except Exception as exc:
            duration_ms = (time.monotonic() - t0) * 1000
            logger.log_mcp_out(tool_name, str(exc), duration_ms, is_error=True)
            raise
