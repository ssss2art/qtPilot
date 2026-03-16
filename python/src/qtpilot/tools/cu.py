"""Computer Use mode tool registration -- 13 cu_* tools mapping to cu.* JSON-RPC methods."""

from __future__ import annotations

from fastmcp import Context, FastMCP


def register_cu_tools(mcp: FastMCP) -> None:
    """Register all computer use mode tools on the MCP server."""

    @mcp.tool
    async def cu_screenshot(ctx: Context) -> dict:
        """Capture a full screenshot of the application window.
        Example: cu_screenshot()
        """
        from qtpilot.server import require_probe

        return await require_probe().call("cu.screenshot")

    @mcp.tool
    async def cu_leftClick(
        x: int, y: int,
        screenAbsolute: bool | None = None,
        delay_ms: int | None = None,
        ctx: Context = None,
    ) -> dict:
        """Left-click at the given coordinates.
        Example: cu_leftClick(x=100, y=200)
        """
        from qtpilot.server import require_probe

        params: dict = {"x": x, "y": y}
        if screenAbsolute is not None:
            params["screenAbsolute"] = screenAbsolute
        if delay_ms is not None:
            params["delay_ms"] = delay_ms
        return await require_probe().call("cu.click", params)

    @mcp.tool
    async def cu_rightClick(
        x: int, y: int,
        screenAbsolute: bool | None = None,
        delay_ms: int | None = None,
        ctx: Context = None,
    ) -> dict:
        """Right-click at the given coordinates.
        Example: cu_rightClick(x=100, y=200)
        """
        from qtpilot.server import require_probe

        params: dict = {"x": x, "y": y}
        if screenAbsolute is not None:
            params["screenAbsolute"] = screenAbsolute
        if delay_ms is not None:
            params["delay_ms"] = delay_ms
        return await require_probe().call("cu.rightClick", params)

    @mcp.tool
    async def cu_middleClick(
        x: int, y: int,
        screenAbsolute: bool | None = None,
        delay_ms: int | None = None,
        ctx: Context = None,
    ) -> dict:
        """Middle-click at the given coordinates.
        Example: cu_middleClick(x=100, y=200)
        """
        from qtpilot.server import require_probe

        params: dict = {"x": x, "y": y}
        if screenAbsolute is not None:
            params["screenAbsolute"] = screenAbsolute
        if delay_ms is not None:
            params["delay_ms"] = delay_ms
        return await require_probe().call("cu.middleClick", params)

    @mcp.tool
    async def cu_doubleClick(
        x: int, y: int,
        screenAbsolute: bool | None = None,
        delay_ms: int | None = None,
        ctx: Context = None,
    ) -> dict:
        """Double-click at the given coordinates.
        Example: cu_doubleClick(x=100, y=200)
        """
        from qtpilot.server import require_probe

        params: dict = {"x": x, "y": y}
        if screenAbsolute is not None:
            params["screenAbsolute"] = screenAbsolute
        if delay_ms is not None:
            params["delay_ms"] = delay_ms
        return await require_probe().call("cu.doubleClick", params)

    @mcp.tool
    async def cu_mouseMove(
        x: int, y: int,
        screenAbsolute: bool | None = None,
        ctx: Context = None,
    ) -> dict:
        """Move the mouse cursor to the given coordinates.
        Example: cu_mouseMove(x=300, y=400)
        """
        from qtpilot.server import require_probe

        params: dict = {"x": x, "y": y}
        if screenAbsolute is not None:
            params["screenAbsolute"] = screenAbsolute
        return await require_probe().call("cu.mouseMove", params)

    @mcp.tool
    async def cu_mouseDrag(
        startX: int, startY: int, endX: int, endY: int,
        screenAbsolute: bool | None = None,
        ctx: Context = None,
    ) -> dict:
        """Drag from start to end coordinates.
        Example: cu_mouseDrag(startX=10, startY=20, endX=200, endY=300)
        """
        from qtpilot.server import require_probe

        params: dict = {"startX": startX, "startY": startY, "endX": endX, "endY": endY}
        if screenAbsolute is not None:
            params["screenAbsolute"] = screenAbsolute
        return await require_probe().call("cu.mouseDrag", params)

    @mcp.tool
    async def cu_mouseDown(
        x: int, y: int,
        button: str | None = None,
        screenAbsolute: bool | None = None,
        ctx: Context = None,
    ) -> dict:
        """Press a mouse button down at the given coordinates.
        Example: cu_mouseDown(x=100, y=200)
        """
        from qtpilot.server import require_probe

        params: dict = {"x": x, "y": y}
        if button is not None:
            params["button"] = button
        if screenAbsolute is not None:
            params["screenAbsolute"] = screenAbsolute
        return await require_probe().call("cu.mouseDown", params)

    @mcp.tool
    async def cu_mouseUp(
        x: int, y: int,
        button: str | None = None,
        screenAbsolute: bool | None = None,
        ctx: Context = None,
    ) -> dict:
        """Release a mouse button at the given coordinates.
        Example: cu_mouseUp(x=100, y=200)
        """
        from qtpilot.server import require_probe

        params: dict = {"x": x, "y": y}
        if button is not None:
            params["button"] = button
        if screenAbsolute is not None:
            params["screenAbsolute"] = screenAbsolute
        return await require_probe().call("cu.mouseUp", params)

    @mcp.tool
    async def cu_type(text: str, ctx: Context = None) -> dict:
        """Type text at the current cursor position.
        Example: cu_type(text="Hello world")
        """
        from qtpilot.server import require_probe

        return await require_probe().call("cu.type", {"text": text})

    @mcp.tool
    async def cu_key(key: str, ctx: Context = None) -> dict:
        """Press a key or key combination.
        Example: cu_key(key="Return")
        """
        from qtpilot.server import require_probe

        return await require_probe().call("cu.key", {"key": key})

    @mcp.tool
    async def cu_scroll(
        x: int, y: int, direction: str,
        amount: int | None = None,
        screenAbsolute: bool | None = None,
        ctx: Context = None,
    ) -> dict:
        """Scroll at the given coordinates in a direction.
        Example: cu_scroll(x=100, y=200, direction="down", amount=3)
        """
        from qtpilot.server import require_probe

        params: dict = {"x": x, "y": y, "direction": direction}
        if amount is not None:
            params["amount"] = amount
        if screenAbsolute is not None:
            params["screenAbsolute"] = screenAbsolute
        return await require_probe().call("cu.scroll", params)

    @mcp.tool
    async def cu_cursorPosition(ctx: Context) -> dict:
        """Get the current cursor position.
        Example: cu_cursorPosition()
        """
        from qtpilot.server import require_probe

        return await require_probe().call("cu.cursorPosition")
