"""Unit tests for LoggingMiddleware."""

from __future__ import annotations

import time
from unittest.mock import AsyncMock, MagicMock, patch

import pytest

from qtpilot.logging_middleware import LoggingMiddleware

pytestmark = pytest.mark.asyncio


def _make_context(tool_name: str, args: dict | None = None):
    """Create a mock MiddlewareContext."""
    ctx = MagicMock()
    ctx.message.name = tool_name
    ctx.message.arguments = args or {}
    return ctx


def _make_result(text: str = "ok"):
    """Create a mock ToolResult."""
    content_block = MagicMock()
    content_block.type = "text"
    content_block.text = text
    result = MagicMock()
    result.content = [content_block]
    return result


class TestLoggingMiddleware:
    async def test_logs_mcp_in_and_out(self):
        """Middleware logs mcp_in before and mcp_out after a tool call."""
        mock_logger = MagicMock()
        mock_logger.is_active = True

        middleware = LoggingMiddleware()

        ctx = _make_context("qt_ping", {})
        call_next = AsyncMock(return_value=_make_result("pong"))

        with patch("qtpilot.server.get_message_logger", create=True, return_value=mock_logger):
            await middleware.on_call_tool(ctx, call_next)

        mock_logger.log_mcp_in.assert_called_once_with("qt_ping", {})
        mock_logger.log_mcp_out.assert_called_once()
        args = mock_logger.log_mcp_out.call_args
        assert args[0][0] == "qt_ping"  # tool_name
        assert args[1].get("is_error", False) is False or args[0][3] is False

    async def test_skips_log_tools(self):
        """Middleware does not log qtpilot_log_* tools."""
        mock_logger = MagicMock()
        mock_logger.is_active = True

        middleware = LoggingMiddleware()

        ctx = _make_context("qtpilot_log_start", {"level": 2})
        call_next = AsyncMock(return_value=_make_result())

        with patch("qtpilot.server.get_message_logger", create=True, return_value=mock_logger):
            await middleware.on_call_tool(ctx, call_next)

        mock_logger.log_mcp_in.assert_not_called()
        mock_logger.log_mcp_out.assert_not_called()

    async def test_logs_error_on_exception(self):
        """Middleware logs is_error=True when tool raises."""
        mock_logger = MagicMock()
        mock_logger.is_active = True

        middleware = LoggingMiddleware()

        ctx = _make_context("qt_ping", {})
        call_next = AsyncMock(side_effect=RuntimeError("probe down"))

        with patch("qtpilot.server.get_message_logger", create=True, return_value=mock_logger):
            with pytest.raises(RuntimeError, match="probe down"):
                await middleware.on_call_tool(ctx, call_next)

        mock_logger.log_mcp_in.assert_called_once()
        mock_logger.log_mcp_out.assert_called_once()
        out_args = mock_logger.log_mcp_out.call_args
        # Check is_error=True was passed
        assert out_args[1].get("is_error") is True or out_args[0][3] is True

    async def test_inactive_logger_skips_logging(self):
        """Middleware does nothing when logger is inactive."""
        mock_logger = MagicMock()
        mock_logger.is_active = False

        middleware = LoggingMiddleware()

        ctx = _make_context("qt_ping", {})
        call_next = AsyncMock(return_value=_make_result())

        with patch("qtpilot.server.get_message_logger", create=True, return_value=mock_logger):
            await middleware.on_call_tool(ctx, call_next)

        mock_logger.log_mcp_in.assert_not_called()
        mock_logger.log_mcp_out.assert_not_called()

    async def test_duration_is_positive(self):
        """Middleware records a positive duration_ms."""
        mock_logger = MagicMock()
        mock_logger.is_active = True

        middleware = LoggingMiddleware()

        ctx = _make_context("qt_ping", {})
        call_next = AsyncMock(return_value=_make_result())

        with patch("qtpilot.server.get_message_logger", create=True, return_value=mock_logger):
            await middleware.on_call_tool(ctx, call_next)

        out_args = mock_logger.log_mcp_out.call_args
        duration_ms = out_args[0][2]  # 3rd positional arg
        assert duration_ms >= 0
