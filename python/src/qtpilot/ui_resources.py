"""UI hierarchy resource for exposing live widget trees."""

from __future__ import annotations

import logging
from fastmcp import FastMCP

logger = logging.getLogger(__name__)


def register_ui_resources(mcp: FastMCP) -> None:
    """Register UI hierarchy resources on the MCP server."""

    @mcp.resource("qtpilot://ui/tree")
    async def ui_tree() -> str:
        """Current QObject hierarchy outline in compact token-efficient format."""
        from qtpilot.server import get_probe
        from qtpilot.tree_format import format_compact_tree

        probe = get_probe()
        if probe is None or not probe.is_connected:
            return "Probe not connected"

        try:
            raw_tree = await probe.call("qt.objects.tree", {"maxDepth": 3})
            return format_compact_tree(raw_tree, visible_only=True)
        except Exception as exc:
            logger.debug("Failed to read UI tree resource: %s", exc)
            return f"Error reading UI tree: {exc}"
