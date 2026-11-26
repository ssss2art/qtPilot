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
        assert name.startswith("__qtpilotDeliver_")
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


@pytest.mark.asyncio
async def test_disconnect_finishes_when_the_page_stopped_answering(monkeypatch: pytest.MonkeyPatch) -> None:
    from qtpilot import page_connection

    monkeypatch.setattr(page_connection, "PAGE_CALL_TIMEOUT", 0.2, raising=False)

    class WedgedPage(FakePage):
        async def evaluate(self, expression: str, arg: Any = None) -> Any:
            if "onnotification = null" in expression:
                await asyncio.Event().wait()  # a renderer that stopped running script
            return await super().evaluate(expression, arg)

    connection = PageProbeConnection(WedgedPage())
    await connection.connect()
    await asyncio.wait_for(connection.disconnect(), timeout=5)
    assert not connection.is_connected


class StalledPage(FakePage):
    """A page that stopped running script: every evaluate that reaches it never returns."""

    def __init__(self, stall: str) -> None:
        super().__init__()
        self.stall = stall

    async def evaluate(self, expression: str, arg: Any = None) -> Any:
        if self.stall in expression:
            await asyncio.Event().wait()
        return await super().evaluate(expression, arg)


@pytest.mark.asyncio
async def test_waiting_for_the_probe_gives_up_on_a_page_that_stopped_running_script() -> None:
    connection = PageProbeConnection(StalledPage("typeof globalThis.qtpilot?.call"))
    with pytest.raises(ConnectionError, match="No qtPilot probe"):
        await asyncio.wait_for(connection.connect_when_ready(ready_timeout=0.2, interval=0.05), timeout=5)


@pytest.mark.asyncio
async def test_a_call_into_a_page_that_stopped_running_script_times_out() -> None:
    from qtpilot.connection import ProbeTimeoutError

    connection = PageProbeConnection(StalledPage("request => qtpilot.call"))
    await connection.connect()
    try:
        with pytest.raises(ProbeTimeoutError):
            await asyncio.wait_for(connection.call("ping", timeout=0.2), timeout=5)
    finally:
        await asyncio.wait_for(connection.disconnect(), timeout=10)


@pytest.mark.asyncio
async def test_a_page_that_fails_a_call_fails_that_request_only() -> None:
    from qtpilot.connection import ProbeError

    class ClosingPage(FakePage):
        async def evaluate(self, expression: str, arg: Any = None) -> Any:
            if expression.startswith("request => qtpilot.call") and json.loads(arg)["method"] == "qt.ping":
                raise RuntimeError("Target page, context or browser has been closed")
            return await super().evaluate(expression, arg)

    connection = PageProbeConnection(ClosingPage())
    await connection.connect()
    try:
        with pytest.raises(ProbeError, match="did not answer"):
            await asyncio.wait_for(connection.call("qt.ping", timeout=5), timeout=2)
        assert await connection.call("ping") == "pong"
    finally:
        await connection.disconnect()


@pytest.mark.asyncio
async def test_page_evaluate_returning_none_fails_that_request_without_closing() -> None:
    from qtpilot.connection import ProbeError

    class NullReplyPage(FakePage):
        async def evaluate(self, expression: str, arg: Any = None) -> Any:
            if expression.startswith("request => qtpilot.call") and json.loads(arg)["method"] == "qt.ping":
                return None
            return await super().evaluate(expression, arg)

    connection = PageProbeConnection(NullReplyPage())
    await connection.connect()
    try:
        with pytest.raises(ProbeError):
            await asyncio.wait_for(connection.call("qt.ping", timeout=2), timeout=2)
        assert connection.is_connected
        assert await connection.call("ping") == "pong"
    finally:
        await connection.disconnect()


@pytest.mark.asyncio
async def test_connect_when_ready_retries_on_navigation_error_and_handshakes() -> None:
    from qtpilot.connection import SUPPORTED_PROTOCOL_VERSION

    class NavigatingPage(FakePage):
        def __init__(self) -> None:
            super().__init__(bridge=False)
            self.ready_checks = 0

        async def evaluate(self, expression: str, arg: Any = None) -> Any:
            if "typeof globalThis.qtpilot?.call" in expression:
                self.ready_checks += 1
                if self.ready_checks == 1:
                    raise RuntimeError("Execution context was destroyed, most likely because of a navigation.")
                if self.ready_checks == 2:
                    return False
                self.bridge = True
                return True
            if expression.startswith("request => qtpilot.call") and json.loads(arg)["method"] == "getVersion":
                request = json.loads(arg)
                return json.dumps({
                    "jsonrpc": "2.0",
                    "id": request["id"],
                    "result": {
                        "version": "0.3.1",
                        "protocolVersion": SUPPORTED_PROTOCOL_VERSION,
                        "name": "qtPilot",
                    },
                })
            return await super().evaluate(expression, arg)

    page = NavigatingPage()
    connection = PageProbeConnection(page)
    version = await asyncio.wait_for(connection.connect_when_ready(ready_timeout=2.0, interval=0.02), timeout=5)
    try:
        assert page.ready_checks >= 3
        assert version["version"] == "0.3.1"
        assert connection.probe_protocol_version == SUPPORTED_PROTOCOL_VERSION
        assert await connection.call("ping") == "pong"
    finally:
        await connection.disconnect()


@pytest.mark.asyncio
async def test_page_probe_connection_reconnects_cleanly() -> None:
    class CountingPage(FakePage):
        def __init__(self) -> None:
            super().__init__()
            self.expose_count = 0

        async def expose_function(self, name: str, callback: Callable[[str], None]) -> None:
            self.expose_count += 1
            await super().expose_function(name, callback)

    page = CountingPage()
    connection = PageProbeConnection(page)
    await connection.connect()
    try:
        assert await connection.call("ping") == "pong"
        await connection.connect()
        assert page.expose_count == 1
        assert connection.is_connected
        assert await connection.call("ping") == "pong"
    finally:
        await connection.disconnect()


@pytest.mark.asyncio
async def test_page_socket_send_when_closed_raises_connection_error() -> None:
    from qtpilot.page_connection import PageSocket

    socket = PageSocket(FakePage(), "__qtpilotDeliver_test")
    await socket.close()
    with pytest.raises(ConnectionError, match="closed"):
        await socket.send('{"jsonrpc":"2.0","id":1,"method":"ping"}')


def test_request_id_handles_bad_and_non_dict_json() -> None:
    from qtpilot.page_connection import _request_id

    assert _request_id("not valid json") is None
    assert _request_id("[1, 2, 3]") is None
    assert _request_id("null") is None
    assert _request_id('{"method": "ping"}') is None
    assert _request_id('{"id": 42, "method": "ping"}') == 42
