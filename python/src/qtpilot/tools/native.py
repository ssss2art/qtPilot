"""Native mode tool registration -- ~33 qt_* tools mapping to qt.* JSON-RPC methods."""

from __future__ import annotations

from fastmcp import Context, FastMCP
import mcp.types as types


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
    async def qt_objects_tree(
        root: str | None = None,
        maxDepth: int | None = 3,
        format: str = "compact",
        visible_only: bool = False,
        ctx: Context = None,
    ) -> str | dict:
        """Get the object tree, optionally from a root with limited depth (defaults to maxDepth=3).

        By default, returns an indented, token-efficient text outline ("compact")
        that cuts token overhead by 80-90% compared to raw JSON. Set `format="json"`
        for the full nested dictionary. Set `visible_only=True` to exclude hidden items.

        Args:
            root: Root objectId to start traversal from (default: top-level objects)
            maxDepth: Maximum tree depth (default: 3). Use -1 for unbounded.
            format: Output format: 'compact' (default, indented outline) or 'json' (raw dictionary)
            visible_only: When True, omit hidden objects and their children

        Example: qt_objects_tree(maxDepth=2)
        Example: qt_objects_tree(format="compact", visible_only=True)
        Example: qt_objects_tree(format="json", maxDepth=2)
        """
        from qtpilot.server import require_probe
        from qtpilot.tree_format import format_compact_tree, prune_hidden_nodes

        if format not in ("compact", "json"):
            raise ValueError(f"Invalid format '{format}'. Valid options are 'compact' and 'json'.")

        params: dict = {}
        if root is not None:
            params["root"] = root
        if maxDepth is not None:
            params["maxDepth"] = maxDepth

        raw_tree = await require_probe().call("qt.objects.tree", params)
        if format == "json":
            if visible_only:
                return prune_hidden_nodes(raw_tree)
            return raw_tree
        return format_compact_tree(raw_tree, visible_only=visible_only)

    @mcp.tool
    async def qt_objects_inspect(
        objectId: str,
        parts: str | list[str] | None = None,
        part: str | list[str] | None = None,
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
                   Comma-separated string like "info,properties" is also accepted.
            part: Alias for `parts`.

        Example: qt_objects_inspect(objectId="MainWindow", parts=["info","geometry"])
        """
        from qtpilot.server import require_probe

        raw_parts = part if part is not None else parts
        if isinstance(raw_parts, str):
            if raw_parts == "all":
                parsed_parts: str | list[str] = "all"
            elif "," in raw_parts:
                parsed_parts = [p.strip() for p in raw_parts.split(",") if p.strip()]
            else:
                parsed_parts = [raw_parts.strip()]
        elif raw_parts is not None:
            parsed_parts = list(raw_parts)
        else:
            parsed_parts = ["info"]

        params: dict = {"objectId": objectId, "parts": parsed_parts}
        return await require_probe().call("qt.objects.inspect", params)

    @mcp.tool
    async def qt_objects_search(
        objectName: str | None = None,
        className: str | None = None,
        properties: dict | None = None,
        root: str | None = None,
        limit: int | None = None,
        name: str | None = None,
        class_name: str | None = None,
        root_id: str | None = None,
        rootId: str | None = None,
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
            name: Alias for `objectName`
            class_name: Alias for `className`
            root_id / rootId: Alias for `root`

        Example: qt_objects_search(className="QPushButton", properties={"enabled": True})
        """
        from qtpilot.server import require_probe

        resolved_name = objectName if objectName is not None else name
        resolved_class = className if className is not None else class_name
        resolved_root = root if root is not None else (root_id or rootId)

        params: dict = {}
        if resolved_name is not None:
            params["objectName"] = resolved_name
        if resolved_class is not None:
            params["className"] = resolved_class
        if properties is not None:
            params["properties"] = properties
        if resolved_root is not None:
            params["root"] = resolved_root
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
        """Invoke a Qt slot or Q_INVOKABLE method with optional arguments.

        Signals are notifications and cannot be emitted through this tool.
        Example: qt_methods_invoke(objectId="settingsDialog", method="accept")
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
        position: dict | list | tuple | None = None,
        viewObjectId: str | None = None,
        modifiers: str | list[str] | None = None,
        ctx: Context = None,
    ) -> dict:
        """Click on a widget or QGraphicsView scene item.

        For QGraphicsObject scene items, pass viewObjectId when the scene has
        more than one rendering view. The optional position is local to the
        target object; omitted means the target center.

        Keyboard modifiers may be given as "ctrl", "ctrl+shift", or
        ["ctrl", "shift"]. Accepted names: alt, cmd, command, control, ctrl,
        keypad, meta, option, shift, super, win. Note that Qt reports macOS
        Command as ControlModifier, so "ctrl" is Command there and "meta"
        reaches the physical Control key.

        Example: qt_ui_click(objectId="submitButton")
        Example: qt_ui_click(objectId="TextBox_...", viewObjectId="layoutView")
        Example: qt_ui_click(objectId="productA", modifiers="ctrl")
        """
        from qtpilot.server import require_probe

        params: dict = {"objectId": objectId}
        if button is not None:
            params["button"] = button
        if position is not None:
            if isinstance(position, (list, tuple)) and len(position) >= 2:
                params["position"] = {"x": position[0], "y": position[1]}
            else:
                params["position"] = position
        if viewObjectId is not None:
            params["viewObjectId"] = viewObjectId
        if modifiers is not None:
            params["modifiers"] = modifiers
        return await require_probe().call("qt.ui.click", params)

    @mcp.tool
    async def qt_ui_doubleClick(
        objectId: str,
        button: str | None = None,
        position: dict | list | tuple | None = None,
        viewObjectId: str | None = None,
        modifiers: str | list[str] | None = None,
        ctx: Context = None,
    ) -> dict:
        """Double-click on a widget or QGraphicsView scene item.

        For QGraphicsObject scene items, pass viewObjectId when the scene has
        more than one rendering view. The optional position is local to the
        target object; omitted means the target center.

        Example: qt_ui_doubleClick(objectId="lineEdit")
        Example: qt_ui_doubleClick(objectId="TextBox_...", viewObjectId="layoutView")
        """
        from qtpilot.server import require_probe

        params: dict = {"objectId": objectId}
        if button is not None:
            params["button"] = button
        if position is not None:
            if isinstance(position, (list, tuple)) and len(position) >= 2:
                params["position"] = {"x": position[0], "y": position[1]}
            else:
                params["position"] = position
        if viewObjectId is not None:
            params["viewObjectId"] = viewObjectId
        if modifiers is not None:
            params["modifiers"] = modifiers
        return await require_probe().call("qt.ui.doubleClick", params)

    @mcp.tool
    async def qt_ui_contextMenu(
        objectId: str,
        position: dict | list | tuple | None = None,
        viewObjectId: str | None = None,
        ctx: Context = None,
    ) -> dict:
        """Open the context menu for a widget or QGraphicsView scene item.

        A synthesized right-click does not produce the QContextMenuEvent that
        opens a Qt context menu, so this sends that event instead. It returns
        as soon as the event is queued -- a handler that answers with
        QMenu::exec() would otherwise block -- so follow it with
        qt_ui_activeMenu to see what opened.

        Example: qt_ui_contextMenu(objectId="fileTree")
        """
        from qtpilot.server import require_probe

        params: dict = {"objectId": objectId}
        if position is not None:
            if isinstance(position, (list, tuple)) and len(position) >= 2:
                params["position"] = {"x": position[0], "y": position[1]}
            else:
                params["position"] = position
        if viewObjectId is not None:
            params["viewObjectId"] = viewObjectId
        return await require_probe().call("qt.ui.contextMenu", params)

    @mcp.tool
    async def qt_ui_activeMenu(ctx: Context = None) -> dict:
        """List the entries of the context menu that is currently open.

        Each entry reports text, enabled, visible, checkable, checked,
        separator and hasSubmenu. Errors when no menu is open.

        Example: qt_ui_activeMenu()
        """
        from qtpilot.server import require_probe

        return await require_probe().call("qt.ui.activeMenu", {})

    @mcp.tool
    async def qt_ui_activateMenuItem(text: str, ctx: Context = None) -> dict:
        """Choose an entry in the open context menu by its label.

        The label is matched with any mnemonic '&' removed, so pass what the
        entry reads as on screen. Errors when no menu is open, when nothing
        carries that label, or when the entry is disabled.

        Example: qt_ui_activateMenuItem(text="Delete")
        """
        from qtpilot.server import require_probe

        return await require_probe().call("qt.ui.activateMenuItem", {"text": text})

    @mcp.tool
    async def qt_ui_sendKeys(
        objectId: str,
        text: str | None = None,
        sequence: str | None = None,
        key: str | None = None,
        keys: str | None = None,
        viewObjectId: str | None = None,
        modifiers: str | list[str] | None = None,
        ctx: Context = None,
    ) -> dict:
        """Send key input to a widget or QGraphicsView scene item.

        For QGraphicsObject scene items, pass viewObjectId when the scene has
        more than one rendering view.

        `modifiers` applies to `text` only and cannot be combined with
        `sequence`, which already spells its own ("Ctrl+S"); passing both is an
        error rather than a merge.

        Example: qt_ui_sendKeys(objectId="lineEdit", text="hello")
        Example: qt_ui_sendKeys(objectId="TextBox_...", text="hello", viewObjectId="layoutView")
        Example: qt_ui_sendKeys(objectId="canvas", text="a", modifiers="ctrl")
        Example: qt_ui_sendKeys(objectId="canvas", sequence="Ctrl+Shift+A")
        """
        from qtpilot.server import require_probe

        resolved_sequence = sequence if sequence is not None else (key or keys)
        params: dict = {"objectId": objectId}
        if text is not None:
            params["text"] = text
        if resolved_sequence is not None:
            params["sequence"] = resolved_sequence
        if viewObjectId is not None:
            params["viewObjectId"] = viewObjectId
        if modifiers is not None:
            params["modifiers"] = modifiers
        return await require_probe().call("qt.ui.sendKeys", params)

    @mcp.tool
    async def qt_ui_screenshot(
        objectId: str,
        fullWindow: bool | None = None,
        region: dict | None = None,
        save_to: str | None = None,
        as_image: bool = True,
        ctx: Context = None,
    ) -> types.ImageContent | dict:
        """Capture a screenshot of a widget or window.

        By default, returns an MCP ImageContent object so vision-capable models
        consume image tokens rather than raw text tokens. Pass `save_to` to save
        the PNG to disk and return lean metadata (~30 tokens).

        Args:
            objectId: The object to screenshot
            fullWindow: Whether to capture the entire top-level window
            region: Optional crop rect `{"x": int, "y": int, "width": int, "height": int}`
            save_to: Optional file path to save PNG artifact directly to disk
            as_image: When True (default), returns native MCP ImageContent. Set False for base64 dict.

        Example: qt_ui_screenshot(objectId="MainWindow")
        Example: qt_ui_screenshot(objectId="MainWindow", save_to="artifacts/win.png")
        Example: qt_ui_screenshot(objectId="MainWindow", as_image=False)
        """
        from qtpilot.server import require_probe
        from qtpilot.tools.screenshot_helper import process_screenshot_response

        params: dict = {"objectId": objectId}
        if fullWindow is not None:
            params["fullWindow"] = fullWindow
        if region is not None:
            params["region"] = region
        resp = await require_probe().call("qt.ui.screenshot", params)
        return process_screenshot_response(resp, save_to=save_to, as_image=as_image)

    @mcp.tool
    async def qt_ui_geometry(
        objectId: str, viewObjectId: str | None = None, ctx: Context = None
    ) -> dict:
        """Get the geometry (position, size) of a widget or QGraphicsView scene item.

        Widgets return {local, global, devicePixelRatio} with integer values.

        A QGraphicsObject (anything in a QGraphicsView scene) additionally returns
        {scene, viewport, visible, views}, with double values, already carrying the
        view's zoom and scroll -- pass global's centre straight to cu_drag/cu_click.
        One scene can be rendered into several views; "views" has one entry each,
        and viewObjectId picks which one the top-level rect mirrors. An item
        scrolled out of the viewport reports visible=false rather than an error.

        Example: qt_ui_geometry(objectId="MainWindow")
        Example: qt_ui_geometry(objectId="...sceneItem_a1b2c3", viewObjectId="...planView")
        """
        from qtpilot.server import require_probe

        params: dict = {"objectId": objectId}
        if viewObjectId is not None:
            params["viewObjectId"] = viewObjectId
        return await require_probe().call("qt.ui.geometry", params)

    @mcp.tool
    async def qt_ui_hitTest(
        x: float, y: float, viewObjectId: str | None = None, ctx: Context = None
    ) -> dict:
        """Find the widget -- or QGraphicsView scene item -- at the given coordinates.

        Without viewObjectId, x/y are screen coordinates; a hit landing on a
        QGraphicsView's viewport continues into its scene and reports the item,
        not the viewport widget. With viewObjectId, x/y are that view's viewport
        coordinates and the search runs inside its scene only.

        Coordinates may be fractional -- qt_ui_geometry reports scene geometry as
        qreal, so the centre of a rect it returns can be fed straight back here.
        A point outside the view's viewport is a miss, not a hit on whatever the
        view would project it onto.

        Example: qt_ui_hitTest(x=100, y=200)
        Example: qt_ui_hitTest(viewObjectId="...planView", x=175, y=175)
        """
        from qtpilot.server import require_probe

        params: dict = {"x": x, "y": y}
        if viewObjectId is not None:
            params["viewObjectId"] = viewObjectId
        return await require_probe().call("qt.ui.hitTest", params)

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
        maxHits: int | None = None,
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

        hits = maxHits if maxHits is not None else max_hits
        params: dict = {
            "objectId": objectId,
            "value": value,
            "column": column,
            "role": role,
            "match": match,
            "maxHits": hits,
        }
        if parent is not None:
            params["parent"] = parent
        return await require_probe().call("qt.models.search", params)

    @mcp.tool
    async def qt_ui_clickItem(
        objectId: str,
        itemPath: list[str] | str | None = None,
        path: list[int] | None = None,
        target: list[str] | str | None = None,
        column: int = 0,
        action: str = "click",
        editColumn: int | None = None,
        expand: bool = True,
        scroll: bool = True,
        ctx: Context = None,
    ) -> dict:
        """Select / click / double-click / edit an item in a view.

        Provide either `itemPath` (exact display text per level, or delimited string
        like "File > Save" / "Menu/File/Save") or `path` (int[] row path).
        `target` is accepted as an alias for `itemPath`.
        `column` selects which cell of the addressed row is acted on (and, for
        `itemPath`, which column's text is matched).
        `action` one of "select", "click", "doubleClick", "edit".
        Works on QTreeView, QTableView, QListView, QComboBox. For combo boxes,
        paths must be length 1 and `edit` returns kNotEditable.
        Example: qt_ui_clickItem(objectId="treeView", itemPath=["ETC","fos4 Fresnel"])
        """
        from qtpilot.server import require_probe

        resolved_item_path = itemPath if itemPath is not None else target
        if isinstance(resolved_item_path, str):
            if ">" in resolved_item_path:
                resolved_item_path = [p.strip() for p in resolved_item_path.split(">") if p.strip()]
            elif "/" in resolved_item_path:
                resolved_item_path = [p.strip() for p in resolved_item_path.split("/") if p.strip()]
            else:
                resolved_item_path = [resolved_item_path]

        if (resolved_item_path is None) == (path is None):
            raise ValueError("Exactly one of itemPath or path must be provided")
        params: dict = {
            "objectId": objectId,
            "column": column,
            "action": action,
            "expand": expand,
            "scroll": scroll,
        }
        if resolved_item_path is not None:
            params["itemPath"] = resolved_item_path
        if path is not None:
            params["path"] = path
        if editColumn is not None:
            params["editColumn"] = editColumn
        return await require_probe().call("qt.ui.clickItem", params)
