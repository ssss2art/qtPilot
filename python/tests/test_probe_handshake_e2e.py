"""Browser origin protection must preserve native/LAN clients and one-client ownership."""

from __future__ import annotations

import asyncio
import json

import pytest
from websockets.asyncio.client import ClientConnection, connect
from websockets.exceptions import ConnectionClosed

from tests.test_replay_e2e import BUILD, live_app

pytestmark = [pytest.mark.real_probe, pytest.mark.skipif(not BUILD.available, reason="Native application not built")]


async def expect_pong(socket: ClientConnection) -> None:
    await socket.send(json.dumps({"jsonrpc": "2.0", "id": 1, "method": "ping"}))
    response = json.loads(await asyncio.wait_for(socket.recv(), timeout=2))
    assert response.get("result") == "pong", f"Native client lost probe access: {response!r}"


@pytest.mark.parametrize("origin", [None, "http://localhost:3000", "http://127.0.0.1", "file://fixture"])
def test_native_and_trusted_origins_retain_access(live_app: str, origin: str | None) -> None:
    async def check() -> None:
        async with connect(live_app, origin=origin) as socket:
            await expect_pong(socket)
    asyncio.run(check())


@pytest.mark.parametrize("origin", ["https://untrusted.invalid", "https://localhost.untrusted.invalid", "null"])
def test_untrusted_browser_origin_cannot_drive_the_probe(live_app: str, origin: str) -> None:
    async def check() -> None:
        async with connect(live_app, origin=origin) as socket:
            with pytest.raises(ConnectionClosed):
                await expect_pong(socket)
    asyncio.run(check())


def test_second_client_is_rejected_without_displacing_the_owner(live_app: str) -> None:
    async def check() -> None:
        async with connect(live_app) as owner:
            await expect_pong(owner)
            async with connect(live_app) as intruder:
                with pytest.raises(ConnectionClosed):
                    await expect_pong(intruder)
            await expect_pong(owner)
    asyncio.run(check())
