"""Status resource for exposing probe connection state."""

from __future__ import annotations

import json
import logging

from fastmcp import FastMCP
from qtpilot.evidence import BufferEvidence

logger = logging.getLogger(__name__)


async def evidence_status() -> dict[str, object]:
    """Shared, fresh evidence view for the status resource and status tool."""
    from qtpilot.server import get_probe, get_recorder

    probe = get_probe()
    counters = (BufferEvidence("probe", None), BufferEvidence("controller", None))
    if probe is not None and probe.is_connected:
        counters = await probe.loss_evidence()
    return {"transport": [counter.to_dict() for counter in counters], "capture": get_recorder().status()}


def register_status_resource(mcp: FastMCP) -> None:
    """Register the qtpilot://status resource on the server."""

    @mcp.resource("qtpilot://status")
    async def probe_status() -> str:
        """Current probe connection status."""
        from qtpilot.server import get_discovery, get_mode, get_probe

        probe = get_probe()
        discovery = get_discovery()

        connected = probe is not None and probe.is_connected
        result = {
            "connected": connected,
            "ws_url": probe.ws_url if connected else None,
            "mode": get_mode(),
            "discovery_active": discovery is not None and discovery.is_running,
            "discovered_probes": len(discovery.probes) if discovery else 0,
            "evidence": await evidence_status(),
        }
        return json.dumps(result)
