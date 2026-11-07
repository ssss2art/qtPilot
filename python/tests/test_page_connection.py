"""PageProbeConnection against a stand-in page: the WebAssembly bridge's contract, no browser."""

from __future__ import annotations

import asyncio
import json
from collections.abc import Callable
from typing import Any

import pytest

from qtpilot.page_connection import PageProbeConnection


class FakePage:
    """A page whose globalThis.qtpilot answers like the probe's JsBridge."""

    url = "http://127.0.0.1:1/app.html"

    def __init__(self, bridge: bool = True) -> None:
        self.bridge = bridge
        self.deliver: Callable[[str], None] | None = None
        self.queued: list[str] = []
        self.subscribed = False

    async def expose_function(self, name: str, callback: Callable[[str], None]) -> None:
        assert name == "__qtpilotDeliver"
        self.deliver = callback

    async def evaluate(self, expression: str, arg: Any = None) -> Any:
        if "typeof globalThis.qtpilot?.call" in expression:
            return self.bridge
        if "bridge.onnotification = text" in expression:
            assert self.deliver is not None
            for text in self.queued:
                self.deliver(text)
            self.queued.clear()
            self.subscribed = True
            return None
        if "onnotification = null" in expression:
            self.subscribed = False
            return None
        if expression.startswith("request => qtpilot.call"):
            request = json.loads(arg)
            result: Any = "pong" if request["method"] == "ping" else {"echo": request["params"]}
            return json.dumps({"jsonrpc": "2.0", "id": request["id"], "result": result})
        raise AssertionError(f"unexpected script: {expression}")

    def notify(self, method: str, params: dict) -> None:
        text = json.dumps({"jsonrpc": "2.0", "method": method, "params": params})
        if self.subscribed and self.deliver is not None:
            self.deliver(text)
        else:
            self.queued.append(text)


@pytest.mark.asyncio
async def test_calls_reach_the_page_bridge_and_replies_come_back() -> None:
    page = FakePage()
    connection = PageProbeConnection(page)
    await connection.connect()
    try:
        assert await connection.call("ping") == "pong"
        assert await connection.call("qtpilot.echo", {"a": 1}) == {"echo": {"a": 1}}
        assert connection.ws_url == page.url
    finally:
        await connection.disconnect()
    assert not page.subscribed


@pytest.mark.asyncio
async def test_notifications_queued_before_connecting_and_after_are_delivered() -> None:
    page = FakePage()
    page.notify("qtpilot.objectCreated", {"id": "early"})
    seen: list[tuple[str, dict]] = []
    connection = PageProbeConnection(page)
    connection.add_notification_handler(lambda method, params: seen.append((method, params)))
    await connection.connect()
    try:
        page.notify("qtpilot.signalEmitted", {"id": "late"})
        for _ in range(50):
            if len(seen) == 2:
                break
            await asyncio.sleep(0.01)
        assert seen == [("qtpilot.objectCreated", {"id": "early"}), ("qtpilot.signalEmitted", {"id": "late"})]
    finally:
        await connection.disconnect()


@pytest.mark.asyncio
async def test_a_page_without_the_probe_is_refused_clearly() -> None:
    with pytest.raises(ConnectionError, match="no qtPilot probe"):
        await PageProbeConnection(FakePage(bridge=False)).connect()
