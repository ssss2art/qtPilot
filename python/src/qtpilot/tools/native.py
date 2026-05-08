"""Native mode tool registration -- ~33 qt_* tools mapping to qt.* JSON-RPC methods."""

from __future__ import annotations

from fastmcp import Context, FastMCP


def register_native_tools(mcp: FastMCP) -> None:
    """Register all native mode tools on the MCP server."""

    # -- Utility / discovery ------------------------------------------------

    @mcp.tool
    async def qt_ping(ctx: Context) -> dict:
        """Ping the probe to check connectivity.
        Example: qt_ping()
        """
        from qtpilot.server import require_probe

        return await require_probe().call("qt.ping")

    @mcp.tool
    async def qt_version(ctx: Context) -> dict:
        """Return Qt and probe version information.
        Example: qt_version()
        """
        from qtpilot.server import require_probe

        return await require_probe().call("qt.version")

    # -- Object tree --------------------------------------------------------

    @mcp.tool
    async def qt_objects_tree(root: str | None = None, maxDepth: int | None = None, ctx: Context = None) -> dict:
        """Get the object tree, optionally from a root with limited depth.
        Example: qt_objects_tree(maxDepth=3)
        """
        from qtpilot.server import require_probe

        params: dict = {}
        if root is not None:
            params["root"] = root
        if maxDepth is not None:
            params["maxDepth"] = maxDepth
        return await require_probe().call("qt.objects.tree", params)

    @mcp.tool
    async def qt_objects_inspect(
        objectId: str,
        parts: str | list[str] | None = None,
        ctx: Context = None,
    ) -> dict:
        """Inspect an object with selectable detail sections.

        `parts` controls which sections are returned. Default is `["info"]` for a
        lightweight overview. Use `"all"` or a list like `["info","properties","methods"]`
        for more detail.

        Valid parts: info, properties, methods, signals, qml, geometry, model.
        Parts that don't apply to the object (e.g. `model` on a non-model object) return
        as a null value rather than raising.

        Args:
            objectId: The object to inspect
            parts: Sections to include. String "all" includes everything; string "info"
                   (or omitted) returns just info. A list names specific sections.

        Example: qt_objects_inspect(objectId="MainWindow", parts=["info","geometry"])
        """
        from qtpilot.server import require_probe

        params: dict = {"objectId": objectId}
        if parts is not None:
            params["parts"] = parts
        return await require_probe().call("qt.objects.inspect", params)

    @mcp.tool
    async def qt_objects_search(
        objectName: str | None = None,
        className: str | None = None,
        properties: dict | None = None,
        root: str | None = None,
        limit: int | None = None,
        ctx: Context = None,
    ) -> dict:
        """Discover objects by name, class, and/or property filters.

        At least one of `objectName`, `className`, or `properties` must be provided.
        Returns a uniform envelope with `objects`, `count`, `truncated`.

        To discover which property names are available for filtering, call
        qt_objects_inspect(objectId=X, parts=["properties"]) on a sample instance.

        Args:
            objectName: Exact match on QObject::objectName()
            className: Exact match (subclass-aware) on metaObject()->className()
            properties: Property-value filters; every listed property must equal the given value
            root: Restrict search to this subtree
            limit: Maximum matches returned (default 50)

        Example: qt_objects_search(className="QPushButton", properties={"enabled": True})
        """
        from qtpilot.server import require_probe

        params: dict = {}
        if objectName is not None:
            params["objectName"] = objectName
        if className is not None:
            params["className"] = className
        if properties is not None:
            params["properties"] = properties
        if root is not None:
            params["root"] = root
        if limit is not None:
            params["limit"] = limit
        return await require_probe().call("qt.objects.search", params)

    # -- Properties ---------------------------------------------------------

    @mcp.tool
    async def qt_properties_get(objectId: str, name: str, ctx: Context = None) -> dict:
        """Get a single property value.
        Example: qt_properties_get(objectId="MainWindow", name="windowTitle")
        """
        from qtpilot.server import require_probe

        return await require_probe().call("qt.properties.get", {"objectId": objectId, "name": name})

    @mcp.tool
    async def qt_properties_set(
        objectId: str, name: str, value: str | int | float | bool, ctx: Context = None
    ) -> dict:
        """Set a property value on an object.
        Example: qt_properties_set(objectId="lineEdit", name="text", value="hello")
        """
        from qtpilot.server import require_probe

        return await require_probe().call(
            "qt.properties.set", {"objectId": objectId, "name": name, "value": value}
        )

    # -- Methods ------------------------------------------------------------

    @mcp.tool
    async def qt_methods_invoke(
        objectId: str, method: str, args: list | None = None, ctx: Context = None
    ) -> dict:
        """Invoke a method on an object with optional arguments.
        Example: qt_methods_invoke(objectId="MainWindow", method="close")
        """
        from qtpilot.server import require_probe

        params: dict = {"objectId": objectId, "method": method}
        if args is not None:
            params["args"] = args
        return await require_probe().call("qt.methods.invoke", params)

    # -- Signals ------------------------------------------------------------

    @mcp.tool
    async def qt_signals_subscribe(objectId: str, signal: str, ctx: Context = None) -> dict:
        """Subscribe to a signal on an object.
        Example: qt_signals_subscribe(objectId="button", signal="clicked")
        """
        from qtpilot.server import require_probe

        return await require_probe().call(
            "qt.signals.subscribe", {"objectId": objectId, "signal": signal}
        )

    @mcp.tool
    async def qt_signals_unsubscribe(subscriptionId: str, ctx: Context = None) -> dict:
        """Unsubscribe from a signal by subscription ID.
        Example: qt_signals_unsubscribe(subscriptionId="sub_1")
        """
        from qtpilot.server import require_probe

        return await require_probe().call(
            "qt.signals.unsubscribe", {"subscriptionId": subscriptionId}
        )

    @mcp.tool
    async def qt_signals_setLifecycle(enabled: bool, ctx: Context = None) -> dict:
        """Enable or disable lifecycle signal notifications.
        Example: qt_signals_setLifecycle(enabled=True)
        """
        from qtpilot.server import require_probe

        return await require_probe().call("qt.signals.setLifecycle", {"enabled": enabled})

    # -- Event capture ------------------------------------------------------

    @mcp.tool
    async def qt_events_start(ctx: Context) -> dict:
        """Start global event capture on the Qt application.

        Installs a global event filter that captures user-interaction events
        (mouse clicks, key presses, focus changes) for every widget without
        needing per-widget signal subscriptions.

        Example: qt_events_start()
        """
        from qtpilot.server import require_probe

        return await require_probe().call("qt.events.start")

    @mcp.tool
    async def qt_events_stop(ctx: Context) -> dict:
        """Stop global event capture.

        Removes the global event filter installed by qt_events_start.

        Example: qt_events_stop()
        """
        from qtpilot.server import require_probe

        return await require_probe().call("qt.events.stop")

    # -- UI interaction -----------------------------------------------------

    @mcp.tool
    async def qt_ui_click(
        objectId: str,
        button: str | None = None,
        position: dict | None = None,
        ctx: Context = None,
    ) -> dict:
        """Click on a widget, optionally specifying button and position.
        Example: qt_ui_click(objectId="submitButton")
        """
        from qtpilot.server import require_probe

        params: dict = {"objectId": objectId}
        if button is not None:
            params["button"] = button
        if position is not None:
            params["position"] = position
        return await require_probe().call("qt.ui.click", params)

    @mcp.tool
    async def qt_ui_sendKeys(
        objectId: str,
        text: str | None = None,
        sequence: str | None = None,
        ctx: Context = None,
    ) -> dict:
        """Send key input to a widget (text or key sequence).
        Example: qt_ui_sendKeys(objectId="lineEdit", text="hello")
        """
        from qtpilot.server import require_probe

        params: dict = {"objectId": objectId}
        if text is not None:
            params["text"] = text
        if sequence is not None:
            params["sequence"] = sequence
        return await require_probe().call("qt.ui.sendKeys", params)

    @mcp.tool
    async def qt_ui_screenshot(
        objectId: str,
        fullWindow: bool | None = None,
        region: dict | None = None,
        ctx: Context = None,
    ) -> dict:
        """Capture a screenshot of a widget as base64 PNG.
        Example: qt_ui_screenshot(objectId="MainWindow")
        """
        from qtpilot.server import require_probe

        params: dict = {"objectId": objectId}
        if fullWindow is not None:
            params["fullWindow"] = fullWindow
        if region is not None:
            params["region"] = region
        return await require_probe().call("qt.ui.screenshot", params)

    @mcp.tool
    async def qt_ui_geometry(objectId: str, ctx: Context = None) -> dict:
        """Get the geometry (position, size) of a widget.
        Example: qt_ui_geometry(objectId="MainWindow")
        """
        from qtpilot.server import require_probe

        return await require_probe().call("qt.ui.geometry", {"objectId": objectId})

    @mcp.tool
    async def qt_ui_hitTest(x: int, y: int, ctx: Context = None) -> dict:
        """Find the widget at the given screen coordinates.
        Example: qt_ui_hitTest(x=100, y=200)
        """
        from qtpilot.server import require_probe

        return await require_probe().call("qt.ui.hitTest", {"x": x, "y": y})

    # -- Named objects ------------------------------------------------------

    @mcp.tool
    async def qt_names_register(name: str, path: str, ctx: Context = None) -> dict:
        """Register a friendly name for an object path.
        Example: qt_names_register(name="submit", path="MainWindow.centralWidget.submitBtn")
        """
        from qtpilot.server import require_probe

        return await require_probe().call("qt.names.register", {"name": name, "path": path})

    @mcp.tool
    async def qt_names_unregister(name: str, ctx: Context = None) -> dict:
        """Remove a registered name.
        Example: qt_names_unregister(name="submit")
        """
        from qtpilot.server import require_probe

        return await require_probe().call("qt.names.unregister", {"name": name})

    @mcp.tool
    async def qt_names_list(ctx: Context) -> dict:
        """List all registered friendly names.
        Example: qt_names_list()
        """
        from qtpilot.server import require_probe

        return await require_probe().call("qt.names.list")

    @mcp.tool
    async def qt_names_validate(ctx: Context) -> dict:
        """Validate that all registered names still resolve.
        Example: qt_names_validate()
        """
        from qtpilot.server import require_probe

        return await require_probe().call("qt.names.validate")

    @mcp.tool
    async def qt_names_load(filePath: str, ctx: Context = None) -> dict:
        """Load name registrations from a file.
        Example: qt_names_load(filePath="names.json")
        """
        from qtpilot.server import require_probe

        return await require_probe().call("qt.names.load", {"filePath": filePath})

    # -- Models -------------------------------------------------------------

    @mcp.tool
    async def qt_models_list(ctx: Context) -> dict:
        """List all QAbstractItemModel instances in the application.
        Example: qt_models_list()
        """
        from qtpilot.server import require_probe

        return await require_probe().call("qt.models.list")

    @mcp.tool
    async def qt_models_data(
        objectId: str,
        parent: list[int] | None = None,
        offset: int | None = None,
        limit: int | None = None,
        roles: list[str] | None = None,
        ctx: Context = None,
    ) -> dict:
        """Read rows from a model/view, at any depth via `parent` row-path.

        `parent=[]` (or omitted) returns top-level rows. `parent=[0, 2]` returns
        the children of the third child of the first top-level row. Each returned
        row carries a full `path` field and a `hasChildren` flag so callers can
        recurse. Pagination via `offset`/`limit` applies to children of `parent`.
        Lazy models (canFetchMore) are force-fetched at each level.
        Example: qt_models_data(objectId="treeView", parent=[0], limit=50)
        """
        from qtpilot.server import require_probe

        params: dict = {"objectId": objectId}
        if parent is not None:
            params["parent"] = parent
        if offset is not None:
            params["offset"] = offset
        if limit is not None:
            params["limit"] = limit
        if roles is not None:
            params["roles"] = roles
        return await require_probe().call("qt.models.data", params)

    @mcp.tool
    async def qt_models_search(
        objectId: str,
        value: str,
        column: int = 0,
        role: str = "display",
        match: str = "contains",
        max_hits: int = 10,
        parent: list[int] | None = None,
        ctx: Context = None,
    ) -> dict:
        """Search a model recursively for rows whose cell value matches `value`.

        `match` one of "exact", "contains", "startsWith", "endsWith", "regex".
        Matching is case-insensitive. `max_hits=-1` = unlimited. `parent=[...]`
        restricts the search to that subtree. Lazy models are force-fetched at
        each level (no false negatives).
        Returns: {matches: [{path, cells}], count, truncated}.
        Example: qt_models_search(objectId="treeView", value="Aura", match="contains")
        """
        from qtpilot.server import require_probe

        params: dict = {
            "objectId": objectId,
            "value": value,
            "column": column,
            "role": role,
            "match": match,
            "maxHits": max_hits,
        }
        if parent is not None:
            params["parent"] = parent
        return await require_probe().call("qt.models.search", params)

    @mcp.tool
    async def qt_ui_clickItem(
        objectId: str,
        itemPath: list[str] | None = None,
        path: list[int] | None = None,
        column: int = 0,
        action: str = "click",
        editColumn: int | None = None,
        expand: bool = True,
        scroll: bool = True,
        ctx: Context = None,
    ) -> dict:
        """Select / click / double-click / edit an item in a view.

        Exactly one of `itemPath` (exact display text per level) or `path`
        (int[] row path) must be provided. `column` selects which cell of the
        addressed row is acted on (and, for `itemPath`, which column's text is
        matched). `action` one of "select", "click", "doubleClick", "edit".
        Works on QTreeView, QTableView, QListView, QComboBox. For combo boxes,
        paths must be length 1 and `edit` returns kNotEditable.
        Example: qt_ui_clickItem(objectId="treeView", itemPath=["ETC","fos4 Fresnel"])
        """
        from qtpilot.server import require_probe

        if (itemPath is None) == (path is None):
            raise ValueError("Exactly one of itemPath or path must be provided")
        params: dict = {
            "objectId": objectId,
            "column": column,
            "action": action,
            "expand": expand,
            "scroll": scroll,
        }
        if itemPath is not None:
            params["itemPath"] = itemPath
        if path is not None:
            params["path"] = path
        if editColumn is not None:
            params["editColumn"] = editColumn
        return await require_probe().call("qt.ui.clickItem", params)
