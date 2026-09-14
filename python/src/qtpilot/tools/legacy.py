"""Legacy compatibility shims ("ghost tools") for backwards compatibility.

These tools restore the pre-consolidation tool contracts so existing client
scripts, skills, and agent prompts never experience a hard break.

Ghost tools are registered on the FastMCP server so they resolve in `get_tool()`
and execute, but are filtered out of `tools/list` via the visibility transform
so they never bloat agent prompt token budget.
"""

from __future__ import annotations

from fastmcp import Context, FastMCP

LEGACY_GHOST_TOOLS: set[str] = {
    "qt_objects_find",
    "qt_objects_findByClass",
    "qt_objects_query",
    "qt_objects_info",
    "qt_properties_list",
    "qt_methods_list",
    "qt_signals_list",
    "qt_qml_inspect",
    "qt_models_info",
    "qt_modes",
    "qtpilot_list_probes",
    "qtpilot_get_mode",
    "qtPilot_probe_status",
    "qtpilot_log_tail",
    "qtpilot_start_recording",
    "qtpilot_stop_recording",
    "qt_events_startCapture",
    "qt_events_stopCapture",
    "qt_models_find",
}


def register_legacy_tools(mcp: FastMCP) -> None:
    """Register backward-compatible legacy shims."""

    # -- Object Search / Discovery ------------------------------------------

    @mcp.tool
    async def qt_objects_find(
        name: str, root: str | None = None, ctx: Context = None
    ) -> dict:
        """[Legacy] Find objects by QObject objectName.

        Deprecated: use qt_objects_search(objectName=...) instead.
        """
        from qtpilot.server import require_probe

        params: dict = {"objectName": name}
        if root is not None:
            params["root"] = root
        return await require_probe().call("qt.objects.search", params)

    @mcp.tool
    async def qt_objects_findByClass(
        className: str, root: str | None = None, ctx: Context = None
    ) -> dict:
        """[Legacy] Find objects by class name.

        Deprecated: use qt_objects_search(className=...) instead.
        """
        from qtpilot.server import require_probe

        params: dict = {"className": className}
        if root is not None:
            params["root"] = root
        return await require_probe().call("qt.objects.search", params)

    @mcp.tool
    async def qt_objects_query(
        query: str | None = None,
        className: str | None = None,
        properties: dict | None = None,
        root: str | None = None,
        limit: int | None = None,
        ctx: Context = None,
    ) -> dict:
        """[Legacy] Find objects by class name and property filters.

        Deprecated: use qt_objects_search(...) instead.
        """
        from qtpilot.server import require_probe

        params: dict = {}
        if className is not None:
            params["className"] = className
        if properties is not None:
            params["properties"] = properties
        if root is not None:
            params["root"] = root
        if limit is not None:
            params["limit"] = limit
        return await require_probe().call("qt.objects.search", params)

    # -- Object Introspection -----------------------------------------------

    @mcp.tool
    async def qt_objects_info(objectId: str, ctx: Context = None) -> dict:
        """[Legacy] Get basic object metadata.

        Deprecated: use qt_objects_inspect(objectId=..., parts=["info"]) instead.
        """
        from qtpilot.server import require_probe

        res = await require_probe().call(
            "qt.objects.inspect", {"objectId": objectId, "parts": ["info"]}
        )
        return res.get("info", res)

    @mcp.tool
    async def qt_properties_list(objectId: str, ctx: Context = None) -> dict:
        """[Legacy] List all Qt properties for an object.

        Deprecated: use qt_objects_inspect(objectId=..., parts=["properties"]) instead.
        """
        from qtpilot.server import require_probe

        res = await require_probe().call(
            "qt.objects.inspect", {"objectId": objectId, "parts": ["properties"]}
        )
        return {"objectId": objectId, "properties": res.get("properties", [])}

    @mcp.tool
    async def qt_methods_list(objectId: str, ctx: Context = None) -> dict:
        """[Legacy] List all invokable methods and slots for an object.

        Deprecated: use qt_objects_inspect(objectId=..., parts=["methods"]) instead.
        """
        from qtpilot.server import require_probe

        res = await require_probe().call(
            "qt.objects.inspect", {"objectId": objectId, "parts": ["methods"]}
        )
        return {"objectId": objectId, "methods": res.get("methods", [])}

    @mcp.tool
    async def qt_signals_list(objectId: str, ctx: Context = None) -> dict:
        """[Legacy] List all signals for an object.

        Deprecated: use qt_objects_inspect(objectId=..., parts=["signals"]) instead.
        """
        from qtpilot.server import require_probe

        res = await require_probe().call(
            "qt.objects.inspect", {"objectId": objectId, "parts": ["signals"]}
        )
        return {"objectId": objectId, "signals": res.get("signals", [])}

    @mcp.tool
    async def qt_qml_inspect(objectId: str, ctx: Context = None) -> dict:
        """[Legacy] Inspect QML-specific properties.

        Deprecated: use qt_objects_inspect(objectId=..., parts=["qml"]) instead.
        """
        from qtpilot.server import require_probe

        res = await require_probe().call(
            "qt.objects.inspect", {"objectId": objectId, "parts": ["qml"]}
        )
        return res.get("qml") or {}

    @mcp.tool
    async def qt_models_info(objectId: str, ctx: Context = None) -> dict:
        """[Legacy] Get metadata for a QAbstractItemModel.

        Deprecated: use qt_objects_inspect(objectId=..., parts=["model"]) instead.
        """
        from qtpilot.server import require_probe

        res = await require_probe().call(
            "qt.objects.inspect", {"objectId": objectId, "parts": ["model"]}
        )
        return res.get("model") or {}

    # -- Session & Discovery ------------------------------------------------

    @mcp.tool
    async def qt_modes(ctx: Context = None) -> dict:
        """[Legacy] List available API modes on the probe.

        Deprecated: use qtpilot_status() instead.
        """
        from qtpilot.server import get_state

        state = get_state()
        return {
            "modes": ["native", "cu", "chrome", "all"],
            "active": state.mode,
        }

    @mcp.tool
    async def qtpilot_list_probes(ctx: Context = None) -> dict:
        """[Legacy] List all discovered probes on the local network.

        Deprecated: use qtpilot_status() instead.
        """
        from qtpilot.server import get_discovery, get_probe

        discovery = get_discovery()
        probe = get_probe()
        probes = []
        if discovery:
            discovery.prune_stale()
            current_url = probe.ws_url if probe and probe.is_connected else None
            for dp in discovery.probes.values():
                probes.append({
                    "ws_url": dp.ws_url,
                    "app_name": dp.app_name,
                    "pid": dp.pid,
                    "qt_version": dp.qt_version,
                    "hostname": dp.hostname,
                    "mode": dp.mode,
                    "uptime": round(dp.uptime, 1),
                    "connected": dp.ws_url == current_url,
                })
        return {"probes": probes}

    @mcp.tool
    async def qtpilot_get_mode(ctx: Context = None) -> dict:
        """[Legacy] Get current active API mode.

        Deprecated: use qtpilot_status() instead.
        """
        from qtpilot.server import get_state

        return {"mode": get_state().mode}

    @mcp.tool
    async def qtPilot_probe_status(ctx: Context = None) -> dict:
        """[Legacy] Get probe connection status.

        Deprecated: use qtpilot_status() instead.
        """
        from qtpilot.tools.discovery_tools import register_discovery_tools
        from qtpilot.server import get_discovery, get_probe, get_state
        from qtpilot import _mcp_compat as mcp_compat

        state = get_state()
        probe = get_probe()
        discovery = get_discovery()

        connection: dict = {
            "connected": probe is not None and probe.is_connected,
        }
        if probe and probe.is_connected:
            connection["ws_url"] = probe.ws_url
            connection["probe_version"] = getattr(probe, "probe_version", None)
            connection["protocol_version"] = getattr(probe, "protocol_version", None)

        disc_info: dict = {
            "active": discovery is not None and discovery.is_running,
            "probes": [],
        }
        if discovery:
            discovery.prune_stale()
            current_url = probe.ws_url if probe and probe.is_connected else None
            for dp in discovery.probes.values():
                disc_info["probes"].append({
                    "ws_url": dp.ws_url,
                    "app_name": dp.app_name,
                    "pid": dp.pid,
                    "qt_version": dp.qt_version,
                    "hostname": dp.hostname,
                    "mode": dp.mode,
                    "uptime": round(dp.uptime, 1),
                    "connected": dp.ws_url == current_url,
                })

        return {
            "mode": state.mode,
            "available_modes": ["native", "cu", "chrome", "all"],
            "connection": connection,
            "discovery": disc_info,
            "mcp": mcp_compat.describe(),
        }

    @mcp.tool
    async def qtpilot_log_tail(count: int = 10, ctx: Context = None) -> dict:
        """[Legacy] Tail recent message log entries.

        Deprecated: use qtpilot_log_status(tail=N) instead.
        """
        from qtpilot.server import get_message_logger

        logger = get_message_logger()
        return logger.tail(count=count)

    # -- Renamed Tools Aliases ----------------------------------------------

    @mcp.tool
    async def qtpilot_start_recording(
        targets: list[dict],
        include_lifecycle: bool = True,
        capture_events: bool = True,
        ctx: Context = None,
    ) -> dict:
        """[Legacy] Start recording Qt signals on specified objects.

        Deprecated: use qtpilot_recording_start(...) instead.
        """
        from qtpilot.event_recorder import TargetSpec
        from qtpilot.server import get_recorder, require_probe

        probe = require_probe()
        recorder = get_recorder()

        specs = [
            TargetSpec(
                object_id=t.get("objectId") or t.get("object_id") or str(t),
                signals=t.get("signals") if isinstance(t, dict) else None,
                recursive=t.get("recursive", False) if isinstance(t, dict) else False,
            )
            for t in (targets if isinstance(targets, list) else [targets])
        ]

        return await recorder.start(
            probe, specs, include_lifecycle=include_lifecycle, capture_events=capture_events
        )

    @mcp.tool
    async def qtpilot_stop_recording(ctx: Context = None) -> dict:
        """[Legacy] Stop recording and return captured event log.

        Deprecated: use qtpilot_recording_stop() instead.
        """
        from qtpilot.server import get_recorder, require_probe

        probe = require_probe()
        recorder = get_recorder()
        return await recorder.stop(probe)

    @mcp.tool
    async def qt_events_startCapture(ctx: Context = None) -> dict:
        """[Legacy] Start global event capture on the Qt application.

        Deprecated: use qt_events_start() instead.
        """
        from qtpilot.server import require_probe

        return await require_probe().call("qt.events.start")

    @mcp.tool
    async def qt_events_stopCapture(ctx: Context = None) -> dict:
        """[Legacy] Stop global event capture.

        Deprecated: use qt_events_stop() instead.
        """
        from qtpilot.server import require_probe

        return await require_probe().call("qt.events.stop")

    @mcp.tool
    async def qt_models_find(
        objectId: str,
        value: str,
        column: int = 0,
        role: str = "display",
        match: str = "contains",
        max_hits: int = 10,
        parent: list[int] | None = None,
        ctx: Context = None,
    ) -> dict:
        """[Legacy] Search a model for rows matching value.

        Deprecated: use qt_models_search(...) instead.
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
