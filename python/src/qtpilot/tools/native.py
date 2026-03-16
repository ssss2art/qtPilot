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

    @mcp.tool
    async def qt_modes(ctx: Context) -> dict:
        """List available API modes on the probe.
        Example: qt_modes()
        """
        from qtpilot.server import require_probe

        return await require_probe().call("qt.modes")

    # -- Object tree --------------------------------------------------------

    @mcp.tool
    async def qt_objects_find(name: str, root: str | None = None, ctx: Context = None) -> dict:
        """Find objects by QObject objectName (the internal C++ identifier, NOT the visible label/text).
        To search by visible text (e.g. button label), use qt_objects_query instead.
        Example: qt_objects_find(name="submitButton")
        """
        from qtpilot.server import require_probe

        params: dict = {"name": name}
        if root is not None:
            params["root"] = root
        return await require_probe().call("qt.objects.find", params)

    @mcp.tool
    async def qt_objects_findByClass(className: str, root: str | None = None, ctx: Context = None) -> dict:
        """Find objects by class name, optionally under a root object.
        Returns all instances of the class. To narrow results by property values
        (e.g. find a specific button by its label), use qt_objects_query instead.
        Example: qt_objects_findByClass(className="QPushButton")
        """
        from qtpilot.server import require_probe

        params: dict = {"className": className}
        if root is not None:
            params["root"] = root
        return await require_probe().call("qt.objects.findByClass", params)

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
    async def qt_objects_info(objectId: str, ctx: Context = None) -> dict:
        """Get basic info (class, parent, children) for an object.
        Example: qt_objects_info(objectId="MainWindow")
        """
        from qtpilot.server import require_probe

        return await require_probe().call("qt.objects.info", {"objectId": objectId})

    @mcp.tool
    async def qt_objects_inspect(objectId: str, ctx: Context = None) -> dict:
        """Deep-inspect an object: properties, methods, signals.
        Example: qt_objects_inspect(objectId="MainWindow")
        """
        from qtpilot.server import require_probe

        return await require_probe().call("qt.objects.inspect", {"objectId": objectId})

    @mcp.tool
    async def qt_objects_query(
        className: str | None = None,
        properties: dict | None = None,
        root: str | None = None,
        ctx: Context = None,
    ) -> dict:
        """Query objects by class and/or property values.
        This is the best way to find widgets by their visible text or state.
        Common patterns:
        - Find a button by label: qt_objects_query(className="QAction", properties={"text": "Next"})
        - Find a checkbox by state: qt_objects_query(className="QCheckBox", properties={"checked": True})
        - Find any widget by text: qt_objects_query(properties={"text": "Search"})
        Example: qt_objects_query(className="QLabel", properties={"text": "Hello"})
        """
        from qtpilot.server import require_probe

        params: dict = {}
        if className is not None:
            params["className"] = className
        if properties is not None:
            params["properties"] = properties
        if root is not None:
            params["root"] = root
        return await require_probe().call("qt.objects.query", params)

    # -- Properties ---------------------------------------------------------

    @mcp.tool
    async def qt_properties_list(objectId: str, ctx: Context = None) -> dict:
        """List all properties of an object.
        Example: qt_properties_list(objectId="MainWindow")
        """
        from qtpilot.server import require_probe

        return await require_probe().call("qt.properties.list", {"objectId": objectId})

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
    async def qt_methods_list(objectId: str, ctx: Context = None) -> dict:
        """List all invocable methods of an object.
        Example: qt_methods_list(objectId="MainWindow")
        """
        from qtpilot.server import require_probe

        return await require_probe().call("qt.methods.list", {"objectId": objectId})

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
    async def qt_signals_list(objectId: str, ctx: Context = None) -> dict:
        """List all signals of an object.
        Example: qt_signals_list(objectId="MainWindow")
        """
        from qtpilot.server import require_probe

        return await require_probe().call("qt.signals.list", {"objectId": objectId})

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
    async def qt_events_startCapture(ctx: Context) -> dict:
        """Start global event capture on the Qt application.

        Installs a global event filter that captures user-interaction events
        (mouse clicks, key presses, focus changes) for every widget without
        needing per-widget signal subscriptions.

        Example: qt_events_startCapture()
        """
        from qtpilot.server import require_probe

        return await require_probe().call("qt.events.startCapture")

    @mcp.tool
    async def qt_events_stopCapture(ctx: Context) -> dict:
        """Stop global event capture.

        Removes the global event filter installed by qt_events_startCapture.

        Example: qt_events_stopCapture()
        """
        from qtpilot.server import require_probe

        return await require_probe().call("qt.events.stopCapture")

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

    # -- QML ----------------------------------------------------------------

    @mcp.tool
    async def qt_qml_inspect(objectId: str, ctx: Context = None) -> dict:
        """Inspect QML-specific properties and bindings of an object.
        Example: qt_qml_inspect(objectId="qmlRoot")
        """
        from qtpilot.server import require_probe

        return await require_probe().call("qt.qml.inspect", {"objectId": objectId})

    # -- Models -------------------------------------------------------------

    @mcp.tool
    async def qt_models_list(ctx: Context) -> dict:
        """List all QAbstractItemModel instances in the application.
        Example: qt_models_list()
        """
        from qtpilot.server import require_probe

        return await require_probe().call("qt.models.list")

    @mcp.tool
    async def qt_models_info(objectId: str, ctx: Context = None) -> dict:
        """Get metadata about a model (row/column counts, role names).
        Example: qt_models_info(objectId="tableModel")
        """
        from qtpilot.server import require_probe

        return await require_probe().call("qt.models.info", {"objectId": objectId})

    @mcp.tool
    async def qt_models_data(
        objectId: str,
        row: int | None = None,
        column: int | None = None,
        role: str | int | None = None,
        offset: int | None = None,
        limit: int | None = None,
        ctx: Context = None,
    ) -> dict:
        """Read data from a model with optional row/column/role filtering and pagination.
        Example: qt_models_data(objectId="tableModel", row=0, column=1)
        """
        from qtpilot.server import require_probe

        params: dict = {"objectId": objectId}
        if row is not None:
            params["row"] = row
        if column is not None:
            params["column"] = column
        if role is not None:
            params["role"] = role
        if offset is not None:
            params["offset"] = offset
        if limit is not None:
            params["limit"] = limit
        return await require_probe().call("qt.models.data", params)
