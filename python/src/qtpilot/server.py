"""FastMCP server factory with lifespan-managed WebSocket connection."""

from __future__ import annotations

import asyncio
import logging
import os
import weakref
from contextlib import asynccontextmanager
from collections.abc import AsyncIterator, MutableMapping

from fastmcp import FastMCP

from qtpilot import _mcp_compat as mcp_compat
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

    async def set_mode(self, new_mode: str) -> dict:
        """Switch the active tool set. Returns previous and new mode.

        Async because tool enumeration is async on every FastMCP generation
        after 2.x.
        """
        valid = {"native", "cu", "chrome", "all"}
        if new_mode not in valid:
            return {"error": f"Invalid mode '{new_mode}'. Choose from: {', '.join(sorted(valid))}"}

        if new_mode == self.mode:
            return {"mode": new_mode, "changed": False}

        old_mode = self.mode
        result: dict = {"mode": new_mode, "previous_mode": old_mode, "changed": True}

        if _uses_mode_visibility(self.mcp):
            # Every mode's tools are registered up front and a transform filters
            # tools/list per request, so switching is just moving this field.
            # Registration stays static and deterministically ordered, which is
            # what MCP 2026-07-28 wants from a cacheable tools/list.
            self.mode = new_mode
            return result

        if new_mode == "all":
            # Switching to "all": just add any missing mode tool sets
            for mode_key in _MODE_PREFIXES:
                _register_mode_tools_if_absent(self.mcp, mode_key)
        else:
            # Switching to a single mode: remove everything except target
            prefixes_to_remove: list[str] = []
            modes_to_drop = [k for k in _MODE_PREFIXES if k != new_mode]
            for mode_key in modes_to_drop:
                prefixes_to_remove.extend(_MODE_PREFIXES[mode_key])

            removed = await _remove_tools_by_prefixes(self.mcp, prefixes_to_remove)
            if removed:
                _registered_modes(self.mcp).difference_update(modes_to_drop)
            elif prefixes_to_remove:
                # FastMCP 4 cannot unregister tools. The mode still changes —
                # it governs which probe APIs qtPilot drives — but the exposed
                # tool list stays as-is. Say so rather than implying a narrowed
                # surface the client can still see.
                result["tools_removed"] = False
                result["note"] = (
                    "Tool surface unchanged: FastMCP "
                    f"{mcp_compat.fastmcp_version()} does not support tool removal. "
                    "Inactive-mode tools remain listed."
                )
            _register_mode_tools_if_absent(self.mcp, new_mode)

        self.mode = new_mode
        return result


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
            "No probe connected. Use qtpilot_status to see available probes, "
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
    # Never fatal: handshake() logs and degrades rather than raising, so a probe
    # that predates protocol negotiation is still usable.
    await conn.handshake()
    logger.info(
        "Connected to probe at %s (version=%s protocol=%s)",
        ws_url, conn.probe_version, conn.probe_protocol_version,
    )

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

# Which mode tool sets have been registered on a given server instance.
#
# Tracking this ourselves replaces scanning the server's private tool manager,
# which does not exist after FastMCP 2.x. Weak keys so a discarded server (every
# test builds one) drops its entry instead of pinning it for the process
# lifetime — and so a recycled id() can never inherit a stale entry.
_REGISTERED_MODES: MutableMapping[FastMCP, set[str]] = weakref.WeakKeyDictionary()


def _registered_modes(mcp: FastMCP) -> set[str]:
    """Return the mutable set of mode keys registered on ``mcp``."""
    return _REGISTERED_MODES.setdefault(mcp, set())


# Servers whose tool surface is filtered by a mode-visibility transform rather
# than by registering and unregistering tools. Weak keys for the same reason as
# _REGISTERED_MODES above.
_MODE_VISIBILITY: MutableMapping[FastMCP, bool] = weakref.WeakKeyDictionary()


def _uses_mode_visibility(mcp: FastMCP) -> bool:
    """True when ``mcp`` filters tools by transform instead of by mutation."""
    return _MODE_VISIBILITY.get(mcp, False)


async def _remove_tools_by_prefixes(mcp: FastMCP, prefixes: list[str]) -> list[str]:
    """Remove every tool whose name matches any prefix; return what was removed.

    Returns an empty list on FastMCP 4, which dropped ``remove_tool`` — see
    ``docs/plans/2026-08-01-mcp-2026-07-28-migration.md`` §3. Callers must treat
    a short return as "the tool surface did not change" rather than an error.
    """
    names = await mcp_compat.list_tool_names(mcp)
    to_remove = [n for n in names if any(n.startswith(p) for p in prefixes)]
    return mcp_compat.remove_tools(mcp, to_remove)


def _register_mode_tools_if_absent(mcp: FastMCP, mode: str) -> None:
    """Register tools for a mode, skipping if that mode is already registered."""
    registered = _registered_modes(mcp)
    if mode in registered:
        return
    registered.add(mode)
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
                        "Use qtpilot_status and qtpilot_connect_probe to connect later.",
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

    # Register mode-specific tools.
    #
    # Where the SDK supports transforms, register *every* mode up front and let
    # a transform filter tools/list down to the active mode. That keeps
    # registration static and deterministically ordered — required for the
    # cacheable tools/list of MCP 2026-07-28 — and is the only way to narrow the
    # surface on FastMCP 4, which dropped remove_tool. Older SDKs fall back to
    # registering just the active mode and mutating on switch.
    _MODE_VISIBILITY[mcp] = mcp_compat.install_mode_visibility(
        mcp, lambda: get_state().mode, _MODE_PREFIXES
    )
    _register_tools_for_mode(mcp, "all" if _uses_mode_visibility(mcp) else mode)

    # Register status resource
    from qtpilot.status import register_status_resource
    register_status_resource(mcp)

    return mcp
