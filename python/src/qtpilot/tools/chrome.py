"""Chrome mode tool registration -- 8 chr_* tools mapping to chr.* JSON-RPC methods."""

from __future__ import annotations

from fastmcp import Context, FastMCP


def register_chrome_tools(mcp: FastMCP) -> None:
    """Register all chrome mode tools on the MCP server."""

    @mcp.tool
    async def chr_readPage(
        filter: str | None = None,
        maxDepth: int | None = None,
        ctx: Context = None,
    ) -> dict:
        """Read the accessible page structure with optional filtering.
        Example: chr_readPage(filter="buttons")
        """
        from qtpilot.server import require_probe

        params: dict = {}
        if filter is not None:
            params["filter"] = filter
        if maxDepth is not None:
            params["maxDepth"] = maxDepth
        return await require_probe().call("chr.readPage", params)

    @mcp.tool
    async def chr_click(ref: str, ctx: Context = None) -> dict:
        """Click an element by its accessibility reference.
        Example: chr_click(ref="btn_submit")
        """
        from qtpilot.server import require_probe

        return await require_probe().call("chr.click", {"ref": ref})

    @mcp.tool
    async def chr_formInput(ref: str, value: str | int | float | bool, ctx: Context = None) -> dict:
        """Set a form input value by accessibility reference.
        Example: chr_formInput(ref="input_name", value="Alice")
        """
        from qtpilot.server import require_probe

        return await require_probe().call("chr.formInput", {"ref": ref, "value": value})

    @mcp.tool
    async def chr_getPageText(ctx: Context) -> dict:
        """Get all visible text content from the page.
        Example: chr_getPageText()
        """
        from qtpilot.server import require_probe

        return await require_probe().call("chr.getPageText")

    @mcp.tool
    async def chr_find(query: str, ctx: Context = None) -> dict:
        """Search for elements matching a text query.
        Example: chr_find(query="Submit")
        """
        from qtpilot.server import require_probe

        return await require_probe().call("chr.find", {"query": query})

    @mcp.tool
    async def chr_navigate(ref: str, ctx: Context = None) -> dict:
        """Navigate to or activate an element by reference.
        Example: chr_navigate(ref="tab_settings")
        """
        from qtpilot.server import require_probe

        return await require_probe().call("chr.navigate", {"ref": ref})

    @mcp.tool
    async def chr_tabsContext(ctx: Context) -> dict:
        """Get context about all tabs/windows in the application.
        Example: chr_tabsContext()
        """
        from qtpilot.server import require_probe

        return await require_probe().call("chr.tabsContext")

    @mcp.tool
    async def chr_readConsoleMessages(
        limit: int | None = None,
        pattern: str | None = None,
        clear: bool | None = None,
        ctx: Context = None,
    ) -> dict:
        """Read console/debug messages with optional filtering.
        Example: chr_readConsoleMessages(limit=10, pattern="error")
        """
        from qtpilot.server import require_probe

        params: dict = {}
        if limit is not None:
            params["limit"] = limit
        if pattern is not None:
            params["pattern"] = pattern
        if clear is not None:
            params["clear"] = clear
        return await require_probe().call("chr.readConsoleMessages", params)
