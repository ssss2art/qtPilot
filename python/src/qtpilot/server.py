"""FastMCP server factory with lifespan-managed WebSocket connection."""

from __future__ import annotations

import asyncio
import logging
import os
from contextlib import asynccontextmanager
from typing import AsyncIterator

from fastmcp import FastMCP

from qtpilot.connection import ProbeConnection, ProbeError
from qtpilot.discovery import DiscoveryListener
from qtpilot.event_recorder import EventRecorder
from qtpilot.message_logger import MessageLogger

logger = logging.getLogger(__name__)

# ---------------------------------------------------------------------------
# Prefix mapping: mode -> tool name prefixes
# ---------------------------------------------------------------------------
_MODE_PREFIXES: dict[str, list[str]] = {
    "native": ["qt_"],
    "cu": ["cu_"],
    "chrome": ["chr_"],
}


# ---------------------------------------------------------------------------
# ServerState — replaces module-level globals for testability
# ---------------------------------------------------------------------------
class ServerState:
    """Encapsulates runtime state for the MCP server."""

    def __init__(self, mcp: FastMCP, mode: str = "native") -> None:
        self.mcp = mcp
        self.mode = mode
        self.probe: ProbeConnection | None = None
        self.discovery: DiscoveryListener | None = None
        self.recorder: EventRecorder = EventRecorder()
        self.message_logger: MessageLogger = MessageLogger()

    # -- mode switching -----------------------------------------------------

    def set_mode(self, new_mode: str) -> dict:
        """Switch the active tool set. Returns previous and new mode."""
        valid = {"native", "cu", "chrome", "all"}
        if new_mode not in valid:
            return {"error": f"Invalid mode '{new_mode}'. Choose from: {', '.join(sorted(valid))}"}

        if new_mode == self.mode:
            return {"mode": new_mode, "changed": False}

        old_mode = self.mode

        if new_mode == "all":
            # Switching to "all": just add any missing mode tool sets
            for mode_key in _MODE_PREFIXES:
                _register_mode_tools_if_absent(self.mcp, mode_key)
        else:
            # Switching to a single mode: remove everything except target
            prefixes_to_remove: list[str] = []
            for mode_key, pfx in _MODE_PREFIXES.items():
                if mode_key != new_mode:
                    prefixes_to_remove.extend(pfx)
            _remove_tools_by_prefixes(self.mcp, prefixes_to_remove)
            _register_mode_tools_if_absent(self.mcp, new_mode)

        self.mode = new_mode
        return {"mode": new_mode, "previous_mode": old_mode, "changed": True}


_state: ServerState | None = None


def get_state() -> ServerState:
    """Get the server state. Raises RuntimeError if not initialised."""
    if _state is None:
        raise RuntimeError("Server not initialized")
    return _state


# ---------------------------------------------------------------------------
# Convenience accessors (thin wrappers around ServerState)
# ---------------------------------------------------------------------------

def get_probe() -> ProbeConnection | None:
    """Get the current probe connection, or None if not connected."""
    return _state.probe if _state else None


def require_probe() -> ProbeConnection:
    """Get the current probe connection. Raises ProbeError if not connected."""
    probe = get_probe()
    if probe is None or not probe.is_connected:
        raise ProbeError(
            "No probe connected. Use qtpilot_list_probes to see available probes, "
            "then qtpilot_connect_probe to connect to one."
        )
    return probe


def get_discovery() -> DiscoveryListener | None:
    """Get the discovery listener, or None if discovery is disabled."""
    return _state.discovery if _state else None


def get_mode() -> str:
    """Get the current server mode."""
    return _state.mode if _state else "native"


def get_recorder() -> EventRecorder:
    """Get the shared EventRecorder instance."""
    if _state is None:
        raise RuntimeError("Server not initialized")
    return _state.recorder


def get_message_logger() -> MessageLogger:
    """Get the shared MessageLogger instance."""
    if _state is None:
        raise RuntimeError("Server not initialized")
    return _state.message_logger


# ---------------------------------------------------------------------------
# Probe connection helpers
# ---------------------------------------------------------------------------

async def connect_to_probe(ws_url: str) -> ProbeConnection:
    """Connect to a probe at the given WebSocket URL."""
    state = get_state()

    if state.probe is not None and state.probe.is_connected:
        logger.info("Disconnecting from current probe at %s", state.probe.ws_url)
        # Detach logger from old probe
        if state.message_logger._attached_probe is not None:
            state.message_logger.detach(state.message_logger._attached_probe)
        await state.probe.disconnect()
        state.probe = None

    conn = ProbeConnection(ws_url)
    await conn.connect()
    state.probe = conn
    logger.info("Connected to probe at %s", ws_url)

    # Attach logger if active
    if state.message_logger.is_active:
        state.message_logger.attach(conn)

    return conn


async def disconnect_probe() -> None:
    """Disconnect the current probe connection if any."""
    state = get_state()
    if state.probe is not None:
        if state.message_logger._attached_probe is state.probe:
            state.message_logger.detach(state.probe)
        await state.probe.disconnect()
        state.probe = None


# ---------------------------------------------------------------------------
# Tool registration helpers
# ---------------------------------------------------------------------------

def _has_tools_with_prefix(mcp: FastMCP, prefixes: list[str]) -> bool:
    """Check if any tools with the given prefixes are already registered."""
    for name in mcp._tool_manager._tools:
        if any(name.startswith(p) for p in prefixes):
            return True
    return False


def _remove_tools_by_prefixes(mcp: FastMCP, prefixes: list[str]) -> None:
    """Remove all tools whose names match any of the given prefixes."""
    to_remove = [
        name for name in list(mcp._tool_manager._tools)
        if any(name.startswith(p) for p in prefixes)
    ]
    for name in to_remove:
        mcp.remove_tool(name)


def _register_mode_tools_if_absent(mcp: FastMCP, mode: str) -> None:
    """Register tools for a mode, skipping if tools with that prefix already exist."""
    prefixes = _MODE_PREFIXES.get(mode, [])
    if _has_tools_with_prefix(mcp, prefixes):
        return
    if mode == "native":
        from qtpilot.tools.native import register_native_tools
        register_native_tools(mcp)
    elif mode == "cu":
        from qtpilot.tools.cu import register_cu_tools
        register_cu_tools(mcp)
    elif mode == "chrome":
        from qtpilot.tools.chrome import register_chrome_tools
        register_chrome_tools(mcp)


def _register_tools_for_mode(mcp: FastMCP, mode: str) -> None:
    """Register all tool sets for a given mode (including 'all')."""
    if mode == "all":
        for mode_key in _MODE_PREFIXES:
            _register_mode_tools_if_absent(mcp, mode_key)
    else:
        _register_mode_tools_if_absent(mcp, mode)

    # Recording tools are always useful (only 3 tools)
    from qtpilot.tools.recording_tools import register_recording_tools
    register_recording_tools(mcp)


# ---------------------------------------------------------------------------
# Server factory
# ---------------------------------------------------------------------------

def create_server(
    mode: str = "native",
    ws_url: str | None = None,
    target: str | None = None,
    port: int = 9222,
    launcher_path: str | None = None,
    discovery_port: int = 9221,
    discovery_enabled: bool = True,
    qt_version: str | None = None,
    qt_dir: str | None = None,
) -> FastMCP:
    """Create a FastMCP server for the given mode.

    Args:
        mode: API mode - "native", "cu", "chrome", or "all".
        ws_url: Optional WebSocket URL to auto-connect on startup.
        target: Optional path to Qt application exe to auto-launch.
        port: Port for auto-launched probe.
        launcher_path: Optional path to qtpilot-launch executable.
        discovery_port: UDP port for probe discovery (default: 9221).
        discovery_enabled: Whether to start the discovery listener.
        qt_version: Optional Qt version for probe auto-detection (e.g. "5.15").
        qt_dir: Optional path to Qt installation prefix for env auto-setup.

    Returns:
        Configured FastMCP instance ready to run.
    """
    global _state

    @asynccontextmanager
    async def lifespan(server: FastMCP) -> AsyncIterator[dict]:
        state = get_state()
        process = None

        try:
            # Start discovery listener
            if discovery_enabled:
                state.discovery = DiscoveryListener(port=discovery_port)
                await state.discovery.start()

            # Auto-launch target if specified
            actual_ws_url = ws_url
            if target is not None:
                from qtpilot.download import get_launcher_filename
                from qtpilot.qt_env import build_subprocess_env

                launcher = (
                    launcher_path
                    or os.environ.get("QTPILOT_LAUNCHER")
                    or get_launcher_filename()
                )
                logger.debug(
                    "Launching target %s via %s on port %d", target, launcher, port
                )

                # Build environment with Qt paths detected/configured
                env = build_subprocess_env(
                    target_path=target,
                    qt_dir=qt_dir,
                )

                launch_args = [
                    launcher,
                    target,
                    "--port",
                    str(port),
                ]
                if qt_version:
                    launch_args.extend(["--qt-version", qt_version])
                if qt_dir:
                    launch_args.extend(["--qt-dir", qt_dir])
                try:
                    process = await asyncio.create_subprocess_exec(
                        *launch_args,
                        stdout=asyncio.subprocess.PIPE,
                        stderr=asyncio.subprocess.PIPE,
                        env=env,
                    )
                except FileNotFoundError:
                    raise FileNotFoundError(
                        f"Launcher not found: {launcher!r}. "
                        "Install with: qtpilot download-tools --qt-version <VERSION>"
                    )
                except OSError as e:
                    raise OSError(
                        f"Could not start launcher {launcher!r}: {e}. "
                        "Install with: qtpilot download-tools --qt-version <VERSION>"
                    ) from e
                await asyncio.sleep(1.5)
                actual_ws_url = f"ws://localhost:{port}"

            # Auto-connect only if an explicit URL was given or target was launched
            if actual_ws_url is not None:
                try:
                    await connect_to_probe(actual_ws_url)
                except Exception as e:
                    logger.warning(
                        "Could not auto-connect to %s: %s. "
                        "Use qtpilot_list_probes and qtpilot_connect_probe to connect later.",
                        actual_ws_url,
                        e,
                    )

            yield {"probe": state.probe, "discovery": state.discovery}

        finally:
            # Stop message logger if active
            if state.message_logger.is_active:
                state.message_logger.stop()

            # Stop recording if active
            if state.recorder.is_recording and state.probe is not None and state.probe.is_connected:
                try:
                    await state.recorder.stop(state.probe)
                except Exception:
                    logger.debug("Failed to stop recording during shutdown", exc_info=True)

            # Disconnect probe
            await disconnect_probe()

            # Stop discovery
            if state.discovery is not None:
                await state.discovery.stop()
                state.discovery = None

            # Terminate launched process
            if process is not None:
                process.terminate()
                try:
                    await asyncio.wait_for(process.wait(), timeout=5.0)
                except asyncio.TimeoutError:
                    process.kill()

    mode_label = mode.title() if mode != "all" else "All"
    mcp = FastMCP(f"qtPilot {mode_label}", lifespan=lifespan)

    # Initialise server state
    _state = ServerState(mcp, mode=mode)

    # Register logging middleware (before tool registration)
    from qtpilot.logging_middleware import LoggingMiddleware
    mcp.add_middleware(LoggingMiddleware())

    # Register discovery tools (always available regardless of mode)
    from qtpilot.tools.discovery_tools import register_discovery_tools
    register_discovery_tools(mcp)

    # Register logging tools (always available regardless of mode)
    from qtpilot.tools.logging_tools import register_logging_tools
    register_logging_tools(mcp)

    # Register mode-specific tools
    _register_tools_for_mode(mcp, mode)

    # Register status resource
    from qtpilot.status import register_status_resource
    register_status_resource(mcp)

    return mcp
