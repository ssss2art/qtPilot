"""Discovery tools -- always registered regardless of mode."""

from __future__ import annotations

from fastmcp import Context, FastMCP


def register_discovery_tools(mcp: FastMCP) -> None:
    """Register probe discovery and connection management tools."""

    @mcp.tool
    async def qtpilot_connect_probe(ws_url: str, ctx: Context = None) -> dict:
        """Connect to a qtPilot probe by its WebSocket URL.

        Disconnects any existing probe connection first.
        Get available URLs from qtpilot_status (the `discovery.probes` list).

        Args:
            ws_url: WebSocket URL (e.g. "ws://192.168.1.100:9222")

        Example: qtpilot_connect_probe(ws_url="ws://localhost:9222")
        """
        from qtpilot.server import connect_to_probe

        conn = await connect_to_probe(ws_url)
        return {"ok": True, "ws_url": conn.ws_url}

    @mcp.tool
    async def qtpilot_disconnect_probe(ctx: Context) -> dict:
        """Disconnect from the currently connected qtPilot probe.

        Example: qtpilot_disconnect_probe()
        """
        from qtpilot.server import disconnect_probe, get_probe

        probe = get_probe()
        if probe is None or not probe.is_connected:
            return {"ok": False, "reason": "No probe connected"}

        ws_url = probe.ws_url
        await disconnect_probe()
        return {"ok": True, "ws_url": ws_url}

    @mcp.tool
    async def qtpilot_status(ctx: Context = None) -> dict:
        """Get the full qtPilot session status in one call.

        Returns the active mode, the list of available modes, probe connection
        state, and discovery state (including all currently discovered probes).

        Example: qtpilot_status()
        """
        from qtpilot.server import get_discovery, get_probe, get_state

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
        }

    @mcp.tool
    async def qtpilot_set_mode(mode: str, ctx: Context) -> dict:
        """Switch the active API mode, changing which tools are available.

        Modes:
        - "native": Qt object introspection (qt_* tools)
        - "cu": Computer use / screenshot-based (cu_* tools)
        - "chrome": Accessibility tree (chr_* tools)
        - "all": All tools from all modes

        Args:
            mode: Target mode - "native", "cu", "chrome", or "all"

        Example: qtpilot_set_mode(mode="native")
        """
        from qtpilot.server import get_state

        state = get_state()
        result = state.set_mode(mode)
        if "error" in result:
            raise ValueError(result["error"])
        changed = result.pop("changed", False)
        if changed:
            await ctx.send_tool_list_changed()
        return {"ok": True, **result}
