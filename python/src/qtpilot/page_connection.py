"""A probe in a browser page: the WebAssembly build's transport, reached through Playwright.

A page cannot listen on a port, so a WebAssembly build of the probe installs
``globalThis.qtpilot`` instead (``src/probe/transport/js_bridge.h``): ``call(json)`` returns the
JSON-RPC reply, and notifications go to ``qtpilot.onnotification``. :class:`PageProbeConnection`
is a :class:`ProbeConnection` whose socket is that page, so everything built on a connection
(fluent queries, replay, evidence) works unchanged.

    async with async_playwright() as p:
        browser = await p.chromium.launch()
        page = await browser.new_page()
        await page.goto(url)
        connection = PageProbeConnection(page)
        await connection.connect_when_ready()
        print(await connection.call("qt.ping"))

Playwright is not a qtPilot dependency: the caller installs and imports it (``pip install
playwright``). This module only needs an object with Playwright's ``evaluate`` and
``expose_function``.
"""

from __future__ import annotations

import asyncio
import contextlib
import time
from typing import Any, Protocol
from uuid import uuid4

from .connection import ProbeConnection

# The probe's bridge, and the page function it delivers notifications to.
_BRIDGE_READY = "() => typeof globalThis.qtpilot?.call === 'function'"
_DELIVER = "__qtpilotDeliver"
_SUBSCRIBE = f"""() => {{
    const bridge = globalThis.qtpilot;
    for (const queued of bridge.notifications.splice(0)) {_DELIVER}(queued);
    bridge.onnotification = text => {{ {_DELIVER}(text); }};
}}"""
_UNSUBSCRIBE = "() => { if (globalThis.qtpilot) globalThis.qtpilot.onnotification = null; }"

# Stands in for a URL in messages and logs: the base class wants one, and a page has no socket.
_PAGE_URL = "ws://browser-page.invalid"


class Page(Protocol):
    """The part of a Playwright async ``Page`` this transport uses."""

    @property
    def url(self) -> str: ...

    async def evaluate(self, expression: str, arg: Any = ...) -> Any: ...

    async def expose_function(self, name: str, callback: Any) -> None: ...


class PageSocket:
    """The probe's socket, stood in for by the page: what :class:`ProbeConnection` reads and writes."""

    def __init__(self, page: Page) -> None:
        self._page = page
        self._inbox: asyncio.Queue[str | None] = asyncio.Queue()
        self._closed = False
        self._exposed = False

    async def start(self) -> None:
        """Delivers the probe's notifications, queued ones first, into this socket."""
        if not self._exposed:
            await self._page.expose_function(_DELIVER, self._deliver)
            self._exposed = True
        await self._page.evaluate(_SUBSCRIBE)

    def _deliver(self, text: str) -> None:
        if not self._closed:
            self._inbox.put_nowait(text)

    async def send(self, message: str) -> None:
        # The bridge answers synchronously, so the reply joins the inbox like a socket frame.
        reply = await self._page.evaluate("request => qtpilot.call(request)", message)
        self._inbox.put_nowait(reply)

    async def recv(self) -> str:
        item = await self._inbox.get()
        if item is None:
            raise ConnectionError("The page's probe connection is closed")
        return item

    def __aiter__(self) -> PageSocket:
        return self

    async def __anext__(self) -> str:
        if self._closed and self._inbox.empty():
            raise StopAsyncIteration
        item = await self._inbox.get()
        if item is None:
            raise StopAsyncIteration
        return item

    async def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        self._inbox.put_nowait(None)
        # The page may already be gone (closed, navigated away); closing must not fail on it.
        with contextlib.suppress(Exception):
            await self._page.evaluate(_UNSUBSCRIBE)


class PageProbeConnection(ProbeConnection):
    """A :class:`ProbeConnection` to the probe inside a browser page (a WebAssembly build)."""

    def __init__(self, page: Page) -> None:
        super().__init__(_PAGE_URL)
        self._page = page

    @property
    def ws_url(self) -> str:
        return self._page.url

    async def connect(self) -> None:
        if self._ws is not None or self._recv_task is not None or self._notification_task is not None:
            await self.disconnect()
        if not await self._page.evaluate(_BRIDGE_READY):
            raise ConnectionError(
                f"{self._page.url} has no qtPilot probe (globalThis.qtpilot); is it a build with the probe linked?"
            )
        socket = PageSocket(self._page)
        await socket.start()
        self._ws = socket
        self._connected = True
        self._session_id = uuid4().hex
        self._notification_queue = asyncio.Queue(maxsize=self._notification_queue.maxsize)
        self._notification_drops = 0
        self._recv_task = asyncio.create_task(self._recv_loop())
        self._notification_task = asyncio.create_task(self._notification_dispatcher())

    async def connect_when_ready(self, ready_timeout: float = 60.0, interval: float = 0.5) -> dict:
        """Waits for the page's probe to install its bridge, connects, and returns getVersion."""
        started = time.monotonic()
        while not await self._page.evaluate(_BRIDGE_READY):
            if time.monotonic() - started > ready_timeout:
                raise ConnectionError(f"No qtPilot probe in {self._page.url} after {ready_timeout:.0f}s")
            await asyncio.sleep(interval)
        await self.connect()
        return await self.handshake()
