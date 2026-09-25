"""Several qtPilot servers on one host must all be able to hear probe broadcasts.

Each MCP client (every agent session, and pytest itself) runs its own server,
and each server listens on the same UDP discovery port. SO_REUSEADDR lets that
work on Linux and Windows. BSD and macOS also need SO_REUSEPORT on every socket;
without it the second server fails to start with "Address already in use".
"""

from __future__ import annotations

import socket

import pytest
from qtpilot.discovery import DiscoveryListener


def _free_udp_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.bind(("0.0.0.0", 0))
        return s.getsockname()[1]


@pytest.mark.asyncio
async def test_two_servers_can_listen_on_one_discovery_port() -> None:
    port = _free_udp_port()
    first = DiscoveryListener(port=port)
    second = DiscoveryListener(port=port)
    try:
        await first.start()
        await second.start()
        assert first.is_running and second.is_running
    finally:
        await second.stop()
        await first.stop()
