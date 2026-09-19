"""Computer Use mode tool registration -- 13 cu_* tools mapping to cu.* JSON-RPC methods."""

from __future__ import annotations

from fastmcp import Context, FastMCP


def _resolve_coords(
    x: int | None = None,
    y: int | None = None,
    coordinate: list[int] | tuple[int, int] | None = None,
    point: dict | None = None,
) -> tuple[int, int]:
    if coordinate is not None and len(coordinate) >= 2:
        return coordinate[0], coordinate[1]
    if point is not None and "x" in point and "y" in point:
        return point["x"], point["y"]
    if x is not None and y is not None:
        return x, y
    raise ValueError(
        "Coordinates required: specify x and y, coordinate=[x, y], or point={'x': ..., 'y': ...}"
    )


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
        x: int | None = None,
        y: int | None = None,
        coordinate: list[int] | None = None,
        point: dict | None = None,
        screenAbsolute: bool | None = None,
        screen_absolute: bool | None = None,
        delay_ms: int | None = None,
        delayMs: int | None = None,
        modifiers: str | list[str] | None = None,
        ctx: Context = None,
    ) -> dict:
        """Left-click at the given coordinates.
        Example: cu_leftClick(x=100, y=200) or cu_leftClick(coordinate=[100, 200])
        """
        from qtpilot.server import require_probe

        rx, ry = _resolve_coords(x, y, coordinate, point)
        resolved_screen_abs = (
            screenAbsolute if screenAbsolute is not None else screen_absolute
        )
        resolved_delay = delay_ms if delay_ms is not None else delayMs

        params: dict = {"x": rx, "y": ry}
        if resolved_screen_abs is not None:
            params["screenAbsolute"] = resolved_screen_abs
        if resolved_delay is not None:
            params["delay_ms"] = resolved_delay
        if modifiers is not None:
            params["modifiers"] = modifiers
        return await require_probe().call("cu.click", params)

    @mcp.tool
    async def cu_rightClick(
        x: int | None = None,
        y: int | None = None,
        coordinate: list[int] | None = None,
        point: dict | None = None,
        screenAbsolute: bool | None = None,
        screen_absolute: bool | None = None,
        delay_ms: int | None = None,
        delayMs: int | None = None,
        modifiers: str | list[str] | None = None,
        ctx: Context = None,
    ) -> dict:
        """Right-click at the given coordinates.
        Example: cu_rightClick(x=100, y=200) or cu_rightClick(coordinate=[100, 200])
        """
        from qtpilot.server import require_probe

        rx, ry = _resolve_coords(x, y, coordinate, point)
        resolved_screen_abs = (
            screenAbsolute if screenAbsolute is not None else screen_absolute
        )
        resolved_delay = delay_ms if delay_ms is not None else delayMs

        params: dict = {"x": rx, "y": ry}
        if resolved_screen_abs is not None:
            params["screenAbsolute"] = resolved_screen_abs
        if resolved_delay is not None:
            params["delay_ms"] = resolved_delay
        if modifiers is not None:
            params["modifiers"] = modifiers
        return await require_probe().call("cu.rightClick", params)

    @mcp.tool
    async def cu_middleClick(
        x: int | None = None,
        y: int | None = None,
        coordinate: list[int] | None = None,
        point: dict | None = None,
        screenAbsolute: bool | None = None,
        screen_absolute: bool | None = None,
        delay_ms: int | None = None,
        delayMs: int | None = None,
        modifiers: str | list[str] | None = None,
        ctx: Context = None,
    ) -> dict:
        """Middle-click at the given coordinates.
        Example: cu_middleClick(x=100, y=200) or cu_middleClick(coordinate=[100, 200])
        """
        from qtpilot.server import require_probe

        rx, ry = _resolve_coords(x, y, coordinate, point)
        resolved_screen_abs = (
            screenAbsolute if screenAbsolute is not None else screen_absolute
        )
        resolved_delay = delay_ms if delay_ms is not None else delayMs

        params: dict = {"x": rx, "y": ry}
        if resolved_screen_abs is not None:
            params["screenAbsolute"] = resolved_screen_abs
        if resolved_delay is not None:
            params["delay_ms"] = resolved_delay
        if modifiers is not None:
            params["modifiers"] = modifiers
        return await require_probe().call("cu.middleClick", params)

    @mcp.tool
    async def cu_doubleClick(
        x: int | None = None,
        y: int | None = None,
        coordinate: list[int] | None = None,
        point: dict | None = None,
        screenAbsolute: bool | None = None,
        screen_absolute: bool | None = None,
        delay_ms: int | None = None,
        delayMs: int | None = None,
        modifiers: str | list[str] | None = None,
        ctx: Context = None,
    ) -> dict:
        """Double-click at the given coordinates.
        Example: cu_doubleClick(x=100, y=200) or cu_doubleClick(coordinate=[100, 200])
        """
        from qtpilot.server import require_probe

        rx, ry = _resolve_coords(x, y, coordinate, point)
        resolved_screen_abs = (
            screenAbsolute if screenAbsolute is not None else screen_absolute
        )
        resolved_delay = delay_ms if delay_ms is not None else delayMs

        params: dict = {"x": rx, "y": ry}
        if resolved_screen_abs is not None:
            params["screenAbsolute"] = resolved_screen_abs
        if resolved_delay is not None:
            params["delay_ms"] = resolved_delay
        if modifiers is not None:
            params["modifiers"] = modifiers
        return await require_probe().call("cu.doubleClick", params)

    @mcp.tool
    async def cu_mouseMove(
        x: int | None = None,
        y: int | None = None,
        coordinate: list[int] | None = None,
        point: dict | None = None,
        screenAbsolute: bool | None = None,
        screen_absolute: bool | None = None,
        modifiers: str | list[str] | None = None,
        ctx: Context = None,
    ) -> dict:
        """Move the mouse cursor to the given coordinates.
        Example: cu_mouseMove(x=300, y=400) or cu_mouseMove(coordinate=[300, 400])
        """
        from qtpilot.server import require_probe

        rx, ry = _resolve_coords(x, y, coordinate, point)
        resolved_screen_abs = (
            screenAbsolute if screenAbsolute is not None else screen_absolute
        )

        params: dict = {"x": rx, "y": ry}
        if resolved_screen_abs is not None:
            params["screenAbsolute"] = resolved_screen_abs
        if modifiers is not None:
            params["modifiers"] = modifiers
        return await require_probe().call("cu.mouseMove", params)

    @mcp.tool
    async def cu_mouseDrag(
        startX: int | None = None,
        startY: int | None = None,
        endX: int | None = None,
        endY: int | None = None,
        start_coordinate: list[int] | None = None,
        coordinate: list[int] | None = None,
        screenAbsolute: bool | None = None,
        screen_absolute: bool | None = None,
        modifiers: str | list[str] | None = None,
        ctx: Context = None,
    ) -> dict:
        """Drag from start to end coordinates.
        Example: cu_mouseDrag(startX=10, startY=20, endX=200, endY=300)
        or cu_mouseDrag(start_coordinate=[10, 20], coordinate=[200, 300])
        """
        from qtpilot.server import require_probe

        sx, sy = _resolve_coords(startX, startY, start_coordinate)
        ex, ey = _resolve_coords(endX, endY, coordinate)
        resolved_screen_abs = (
            screenAbsolute if screenAbsolute is not None else screen_absolute
        )

        params: dict = {"startX": sx, "startY": sy, "endX": ex, "endY": ey}
        if resolved_screen_abs is not None:
            params["screenAbsolute"] = resolved_screen_abs
        if modifiers is not None:
            params["modifiers"] = modifiers
        return await require_probe().call("cu.mouseDrag", params)

    @mcp.tool
    async def cu_mouseDown(
        x: int | None = None,
        y: int | None = None,
        coordinate: list[int] | None = None,
        point: dict | None = None,
        button: str | None = None,
        screenAbsolute: bool | None = None,
        screen_absolute: bool | None = None,
        modifiers: str | list[str] | None = None,
        ctx: Context = None,
    ) -> dict:
        """Press a mouse button down at the given coordinates.
        Example: cu_mouseDown(x=100, y=200) or cu_mouseDown(coordinate=[100, 200])
        """
        from qtpilot.server import require_probe

        rx, ry = _resolve_coords(x, y, coordinate, point)
        resolved_screen_abs = (
            screenAbsolute if screenAbsolute is not None else screen_absolute
        )

        params: dict = {"x": rx, "y": ry}
        if button is not None:
            params["button"] = button
        if resolved_screen_abs is not None:
            params["screenAbsolute"] = resolved_screen_abs
        if modifiers is not None:
            params["modifiers"] = modifiers
        return await require_probe().call("cu.mouseDown", params)

    @mcp.tool
    async def cu_mouseUp(
        x: int | None = None,
        y: int | None = None,
        coordinate: list[int] | None = None,
        point: dict | None = None,
        button: str | None = None,
        screenAbsolute: bool | None = None,
        screen_absolute: bool | None = None,
        modifiers: str | list[str] | None = None,
        ctx: Context = None,
    ) -> dict:
        """Release a mouse button at the given coordinates.
        Example: cu_mouseUp(x=100, y=200) or cu_mouseUp(coordinate=[100, 200])
        """
        from qtpilot.server import require_probe

        rx, ry = _resolve_coords(x, y, coordinate, point)
        resolved_screen_abs = (
            screenAbsolute if screenAbsolute is not None else screen_absolute
        )

        params: dict = {"x": rx, "y": ry}
        if button is not None:
            params["button"] = button
        if resolved_screen_abs is not None:
            params["screenAbsolute"] = resolved_screen_abs
        if modifiers is not None:
            params["modifiers"] = modifiers
        return await require_probe().call("cu.mouseUp", params)

    @mcp.tool
    async def cu_type(text: str, ctx: Context = None) -> dict:
        """Type text at the current cursor position.
        Example: cu_type(text="Hello world")
        """
        from qtpilot.server import require_probe

        return await require_probe().call("cu.type", {"text": text})

    @mcp.tool
    async def cu_key(
        key: str | None = None, text: str | None = None, ctx: Context = None
    ) -> dict:
        """Press a key or single key combination.

        Use named punctuation when the key is part of the ``+``-separated
        grammar. Examples: ``meta+Plus``, ``ctrl+Minus``, and
        ``QuestionMark``. Accepts both 'key' and 'text'.
        """
        from qtpilot.server import require_probe

        resolved_key = key if key is not None else text
        if resolved_key is None:
            raise ValueError("key or text must be provided")
        return await require_probe().call("cu.key", {"key": resolved_key})

    @mcp.tool
    async def cu_scroll(
        x: int | None = None,
        y: int | None = None,
        direction: str = "down",
        amount: int | None = None,
        coordinate: list[int] | None = None,
        point: dict | None = None,
        screenAbsolute: bool | None = None,
        screen_absolute: bool | None = None,
        modifiers: str | list[str] | None = None,
        ctx: Context = None,
    ) -> dict:
        """Scroll at the given coordinates in a direction.
        Example: cu_scroll(x=100, y=200, direction="down", amount=3)
        """
        from qtpilot.server import require_probe

        rx, ry = _resolve_coords(x, y, coordinate, point)
        resolved_screen_abs = (
            screenAbsolute if screenAbsolute is not None else screen_absolute
        )

        params: dict = {"x": rx, "y": ry, "direction": direction}
        if amount is not None:
            params["amount"] = amount
        if resolved_screen_abs is not None:
            params["screenAbsolute"] = resolved_screen_abs
        if modifiers is not None:
            params["modifiers"] = modifiers
        return await require_probe().call("cu.scroll", params)

    @mcp.tool
    async def cu_cursorPosition(ctx: Context) -> dict:
        """Get the current cursor position.
        Example: cu_cursorPosition()
        """
        from qtpilot.server import require_probe

        return await require_probe().call("cu.cursorPosition")
