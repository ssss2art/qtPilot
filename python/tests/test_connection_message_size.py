"""The probe connection accepts messages the size of a full-window screenshot.

The defect: the connection used the ``websockets`` default ``max_size`` of 1 MiB, so a
probe reply carrying a larger screenshot closed the connection (code 1009) and never
arrived. Runs against a real loopback WebSocket server.
"""

from __future__ import annotations

import json

import pytest
from websockets.asyncio.server import ServerConnection, serve

from qtpilot import connection
from qtpilot.connection import ProbeConnection

_IMAGE_CHARS = 2 * 1024 * 1024


async def _big_reply(ws: ServerConnection) -> None:
    async for raw in ws:
        request = json.loads(raw)
        reply = {"meta": {}, "result": {"image": "A" * _IMAGE_CHARS}}
        await ws.send(json.dumps({"jsonrpc": "2.0", "id": request["id"], "result": reply}))


@pytest.mark.asyncio
async def test_a_reply_over_one_mebibyte_arrives() -> None:
    async with serve(_big_reply, "127.0.0.1", 0, max_size=None) as server:
        conn = ProbeConnection(f"ws://127.0.0.1:{server.sockets[0].getsockname()[1]}")
        await conn.connect()
        try:
            resp = await conn.call("qt.ui.screenshot", {"objectId": "MainWindow"}, timeout=10)
            assert len(resp["result"]["image"]) == _IMAGE_CHARS
        finally:
            await conn.disconnect()


def test_the_cap_is_256_mebibytes() -> None:
    assert connection.MAX_PROBE_MESSAGE_BYTES == 256 * 1024 * 1024
