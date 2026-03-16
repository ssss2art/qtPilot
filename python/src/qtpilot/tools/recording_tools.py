"""Recording tools -- start/stop signal recording and check status."""

from __future__ import annotations

from fastmcp import Context, FastMCP

from qtpilot.event_recorder import TargetSpec


def register_recording_tools(mcp: FastMCP) -> None:
    """Register signal recording tools on the MCP server."""

    @mcp.tool
    async def qtpilot_start_recording(
        targets: list[dict],
        include_lifecycle: bool = True,
        capture_events: bool = True,
        ctx: Context = None,
    ) -> dict:
        """Start recording Qt signals on specified objects.

        Subscribes to signals and begins buffering events with timestamps.
        The user can then interact with the Qt application. Call
        qtpilot_stop_recording to retrieve the event log.

        Args:
            targets: List of objects to watch. Each target is a dict with:
                - object_id (str): Object to watch (e.g. "MainWindow")
                - signals (list[str], optional): Specific signals. None = smart defaults
                - recursive (bool, optional): Also watch children. Default false
            include_lifecycle: Record object creation/destruction events (default true)
            capture_events: Enable global event capture for mouse, keyboard, and
                focus events (default true). When enabled, the probe installs a
                global event filter so no per-widget subscription is needed.

        Example: qtpilot_start_recording(targets=[{"object_id": "MainWindow", "recursive": true}])
        """
        from qtpilot.server import get_recorder, require_probe

        probe = require_probe()
        recorder = get_recorder()

        specs = [
            TargetSpec(
                object_id=t["object_id"],
                signals=t.get("signals"),
                recursive=t.get("recursive", False),
            )
            for t in targets
        ]

        return await recorder.start(
            probe, specs, include_lifecycle=include_lifecycle, capture_events=capture_events
        )

    @mcp.tool
    async def qtpilot_stop_recording(ctx: Context) -> dict:
        """Stop recording and return the captured event log.

        Returns a list of timestamped events captured since recording started.
        Each event has a "t" field (seconds since start) and type-specific fields.

        Example: qtpilot_stop_recording()
        """
        from qtpilot.server import get_recorder, require_probe

        probe = require_probe()
        recorder = get_recorder()

        return await recorder.stop(probe)

    @mcp.tool
    async def qtpilot_recording_status(ctx: Context) -> dict:
        """Get the current signal recording status.

        Returns whether recording is active, event count, and duration.

        Example: qtpilot_recording_status()
        """
        from qtpilot.server import get_recorder

        return get_recorder().status()
