"""UDP discovery listener for finding qtPilot probes on the network."""

from __future__ import annotations

import asyncio
import json
import logging
import socket
import time
from dataclasses import dataclass, field
from typing import Callable

logger = logging.getLogger(__name__)

DEFAULT_DISCOVERY_PORT = 9221
STALE_TIMEOUT = 15.0


@dataclass
class DiscoveredProbe:
    """A qtPilot probe discovered via UDP broadcast."""

    app_name: str
    pid: int
    qt_version: str
    ws_port: int
    hostname: str
    mode: str
    address: str
    last_seen: float = field(default_factory=time.monotonic)
    uptime: float = 0.0

    @property
    def ws_url(self) -> str:
        """WebSocket URL to connect to this probe."""
        return f"ws://{self.address}:{self.ws_port}"

    @property
    def key(self) -> str:
        """Unique identifier for this probe instance."""
        return f"{self.address}:{self.pid}:{self.ws_port}"

    def is_stale(self, timeout: float = STALE_TIMEOUT) -> bool:
        """Check if this probe hasn't been seen recently."""
        return (time.monotonic() - self.last_seen) > timeout


class DiscoveryProtocol(asyncio.DatagramProtocol):
    """asyncio datagram protocol that parses qtPilot discovery messages."""

    def __init__(self, listener: DiscoveryListener) -> None:
        self._listener = listener

    def datagram_received(self, data: bytes, addr: tuple[str, int]) -> None:
        try:
            msg = json.loads(data.decode("utf-8"))
        except (json.JSONDecodeError, UnicodeDecodeError):
            return

        if msg.get("protocol") != "qtPilot-discovery":
            return

        msg_type = msg.get("type")
        source_addr = addr[0]

        if msg_type == "announce":
            self._listener.on_announce(msg, source_addr)
        elif msg_type == "goodbye":
            self._listener.on_goodbye(msg, source_addr)

    def error_received(self, exc: Exception) -> None:
        logger.warning("Discovery UDP error: %s", exc)

    def connection_lost(self, exc: Exception | None) -> None:
        logger.debug("Discovery UDP connection lost: %s", exc)


class DiscoveryListener:
    """Listens for qtPilot probe UDP broadcast announcements."""

    def __init__(self, port: int = DEFAULT_DISCOVERY_PORT) -> None:
        self._port = port
        self._probes: dict[str, DiscoveredProbe] = {}
        self._transport: asyncio.DatagramTransport | None = None
        self._protocol: DiscoveryProtocol | None = None
        self._running = False
        self._found_callbacks: list[Callable[[DiscoveredProbe], None]] = []
        self._found_event = asyncio.Event()

    @property
    def probes(self) -> dict[str, DiscoveredProbe]:
        """Currently known probes keyed by address:pid:ws_port."""
        return self._probes

    @property
    def is_running(self) -> bool:
        return self._running

    async def start(self) -> None:
        """Start listening for discovery broadcasts."""
        if self._running:
            return

        loop = asyncio.get_running_loop()

        # Create socket manually with SO_REUSEADDR so multiple qtpilot
        # server instances can listen for probe broadcasts concurrently.
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        sock.setblocking(False)
        sock.bind(("0.0.0.0", self._port))

        self._transport, self._protocol = await loop.create_datagram_endpoint(
            lambda: DiscoveryProtocol(self),
            sock=sock,
        )
        self._running = True
        logger.info("Discovery listener started on UDP port %d", self._port)

    async def stop(self) -> None:
        """Stop listening."""
        if not self._running:
            return

        self._running = False
        if self._transport:
            self._transport.close()
            self._transport = None
        self._protocol = None
        logger.info("Discovery listener stopped")

    def on_announce(self, msg: dict, source_addr: str) -> None:
        """Handle an announce message from a probe."""
        probe = DiscoveredProbe(
            app_name=msg.get("appName", "unknown"),
            pid=msg.get("pid", 0),
            qt_version=msg.get("qtVersion", "unknown"),
            ws_port=msg.get("wsPort", 9222),
            hostname=msg.get("hostname", "unknown"),
            mode=msg.get("mode", "all"),
            address=source_addr,
            last_seen=time.monotonic(),
            uptime=msg.get("uptime", 0.0),
        )

        is_new = probe.key not in self._probes
        self._probes[probe.key] = probe

        if is_new:
            logger.info("Discovered new probe: %s (pid=%d) at %s", probe.app_name, probe.pid, probe.ws_url)
            self._found_event.set()
            for callback in self._found_callbacks:
                try:
                    callback(probe)
                except Exception:
                    logger.exception("Error in discovery callback")

    def on_goodbye(self, msg: dict, source_addr: str) -> None:
        """Handle a goodbye message -- remove the probe."""
        pid = msg.get("pid", 0)
        ws_port = msg.get("wsPort", 9222)
        key = f"{source_addr}:{pid}:{ws_port}"
        removed = self._probes.pop(key, None)
        if removed:
            logger.info("Probe departed: %s (pid=%d)", removed.app_name, removed.pid)

    async def wait_for_probe(self, timeout: float = 10.0) -> DiscoveredProbe | None:
        """Wait until at least one probe is discovered, or timeout."""
        if self._probes:
            return next(iter(self._probes.values()))

        self._found_event.clear()
        try:
            await asyncio.wait_for(self._found_event.wait(), timeout=timeout)
            if self._probes:
                return next(iter(self._probes.values()))
        except asyncio.TimeoutError:
            pass
        return None

    def prune_stale(self, timeout: float = STALE_TIMEOUT) -> list[str]:
        """Remove probes not seen within timeout. Returns removed keys."""
        stale_keys = [k for k, p in self._probes.items() if p.is_stale(timeout)]
        for key in stale_keys:
            removed = self._probes.pop(key)
            logger.info("Pruned stale probe: %s (pid=%d)", removed.app_name, removed.pid)
        return stale_keys

    def on_found(self, callback: Callable[[DiscoveredProbe], None]) -> None:
        """Register a callback for when new probes are discovered."""
        self._found_callbacks.append(callback)
