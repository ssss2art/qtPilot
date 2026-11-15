"""Use Playwright and the page bridge together against the WASM example."""

from __future__ import annotations

import asyncio
import functools
import threading
import unittest
from http.server import ThreadingHTTPServer

from playwright.async_api import Error as PlaywrightError, async_playwright
from qtpilot.connection import ProbeError
from qtpilot.page_connection import PageProbeConnection

from test_webmcp_example import APP_DIR, CHROME, IsolatedHandler


class PageConnectionExampleTest(unittest.IsolatedAsyncioTestCase):
    server: ThreadingHTTPServer
    thread: threading.Thread

    @classmethod
    def setUpClass(cls) -> None:
        if not (APP_DIR / "qtpilot_webmcp_example.html").is_file():
            raise RuntimeError(f"Build the WASM example first: {APP_DIR}")
        cls.server = ThreadingHTTPServer(
            ("127.0.0.1", 0), functools.partial(IsolatedHandler, directory=str(APP_DIR))
        )
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls) -> None:
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join()

    async def test_playwright_and_qt_bridge_work_without_webmcp(self) -> None:
        async with async_playwright() as playwright:
            browser = await playwright.chromium.launch(executable_path=CHROME, headless=True)
            try:
                page = await browser.new_page()
                await page.goto(f"http://127.0.0.1:{self.server.server_port}/qtpilot_webmcp_example.html")
                self.assertTrue(await page.evaluate("crossOriginIsolated"))
                connection = PageProbeConnection(page)
                await connection.connect_when_ready()
                try:
                    found = await connection.call("qt.objects.search", {"objectName": "incrementButton"})
                    button_id = found["result"]["objects"][0]["objectId"]
                    window = await connection.call("qt.objects.search", {"objectName": "demoWindow"})
                    window_id = window["result"]["objects"][0]["objectId"]
                    await connection.call("qt.ui.click", {"objectId": button_id})
                    for _ in range(50):
                        count = await connection.call("qt.properties.get", {"objectId": window_id, "name": "count"})
                        if count["result"]["value"] == 1:
                            break
                        await asyncio.sleep(0.1)
                    self.assertEqual(count["result"]["value"], 1)

                    with self.assertRaises(ProbeError):
                        await connection.call("qt.properties.get", {"objectId": "missing-object", "name": "text"})
                    self.assertTrue((await connection.call("qt.ping"))["result"]["pong"])
                finally:
                    await connection.disconnect()
            finally:
                await browser.close()

    async def test_qt_model_and_signal_are_available_in_the_page(self) -> None:
        async with async_playwright() as playwright:
            browser = await playwright.chromium.launch(executable_path=CHROME, headless=True)
            try:
                page = await browser.new_page()
                await page.goto(f"http://127.0.0.1:{self.server.server_port}/qtpilot_webmcp_example.html")
                connection = PageProbeConnection(page)
                await connection.connect_when_ready()
                try:
                    found = await connection.call("qt.objects.search", {"objectName": "demoModel"})
                    self.assertEqual(found["result"]["count"], 1)
                    model_id = found["result"]["objects"][0]["objectId"]
                    data = await connection.call("qt.models.data", {"objectId": model_id})
                    self.assertEqual(data["result"]["totalRows"], 2)
                    self.assertEqual(data["result"]["rows"][0]["cells"][0]["display"], "First item")
                    view = await connection.call("qt.objects.search", {"objectName": "itemsView"})
                    count = await connection.call(
                        "qt.properties.get", {"objectId": view["result"]["objects"][0]["objectId"], "name": "count"}
                    )
                    self.assertEqual(count["result"]["value"], 2)

                    window = await connection.call("qt.objects.search", {"objectName": "demoWindow"})
                    window_id = window["result"]["objects"][0]["objectId"]
                    button = await connection.call("qt.objects.search", {"objectName": "incrementButton"})
                    button_id = button["result"]["objects"][0]["objectId"]
                    received = asyncio.Event()
                    notifications: list[dict[str, object]] = []

                    def observe(method: str, params: dict[str, object]) -> None:
                        if method == "qtpilot.signalEmitted":
                            notifications.append(params)
                            received.set()

                    connection.add_notification_handler(observe)
                    subscribed = await connection.call(
                        "qt.signals.subscribe", {"objectId": window_id, "signal": "countChanged"}
                    )
                    subscription_id = subscribed["result"]["subscriptionId"]
                    await connection.call("qt.ui.click", {"objectId": button_id})
                    await asyncio.wait_for(received.wait(), timeout=5)
                    self.assertEqual(notifications[0]["subscriptionId"], subscription_id)
                    await connection.call("qt.signals.unsubscribe", {"subscriptionId": subscription_id})
                finally:
                    await connection.disconnect()
            finally:
                await browser.close()

    async def test_page_reload_can_reconnect_and_use_the_probe(self) -> None:
        async with async_playwright() as playwright:
            browser = await playwright.chromium.launch(executable_path=CHROME, headless=True)
            try:
                page = await browser.new_page()
                await page.goto(f"http://127.0.0.1:{self.server.server_port}/qtpilot_webmcp_example.html")
                connection = PageProbeConnection(page)
                await connection.connect_when_ready()
                try:
                    await page.reload()
                    await connection.connect_when_ready()
                    self.assertTrue((await connection.call("qt.ping"))["result"]["pong"])
                    await connection.disconnect()
                    await connection.connect_when_ready()
                    window = await connection.call("qt.objects.search", {"objectName": "demoWindow"})
                    button = await connection.call("qt.objects.search", {"objectName": "incrementButton"})
                    received = asyncio.Event()
                    subscription_id = (
                        await connection.call(
                            "qt.signals.subscribe",
                            {"objectId": window["result"]["objects"][0]["objectId"], "signal": "countChanged"},
                        )
                    )["result"]["subscriptionId"]
                    connection.add_notification_handler(
                        lambda method, params: received.set()
                        if method == "qtpilot.signalEmitted" and params.get("subscriptionId") == subscription_id
                        else None
                    )
                    await connection.call("qt.ui.click", {"objectId": button["result"]["objects"][0]["objectId"]})
                    await asyncio.wait_for(received.wait(), timeout=5)
                finally:
                    await connection.disconnect()
            finally:
                await browser.close()

    async def test_only_one_connection_owns_notifications_on_a_page(self) -> None:
        async with async_playwright() as playwright:
            browser = await playwright.chromium.launch(executable_path=CHROME, headless=True)
            try:
                page = await browser.new_page()
                await page.goto(f"http://127.0.0.1:{self.server.server_port}/qtpilot_webmcp_example.html")
                first = PageProbeConnection(page)
                second = PageProbeConnection(page)
                await first.connect_when_ready()
                try:
                    with self.assertRaisesRegex(PlaywrightError, "already has a qtPilot notification connection"):
                        await second.connect()
                    window = await first.call("qt.objects.search", {"objectName": "demoWindow"})
                    button = await first.call("qt.objects.search", {"objectName": "incrementButton"})
                    received = asyncio.Event()
                    subscription_id = (
                        await first.call(
                            "qt.signals.subscribe",
                            {"objectId": window["result"]["objects"][0]["objectId"], "signal": "countChanged"},
                        )
                    )["result"]["subscriptionId"]
                    first.add_notification_handler(
                        lambda method, params: received.set()
                        if method == "qtpilot.signalEmitted" and params.get("subscriptionId") == subscription_id
                        else None
                    )
                    await first.call("qt.ui.click", {"objectId": button["result"]["objects"][0]["objectId"]})
                    await asyncio.wait_for(received.wait(), timeout=5)
                finally:
                    await first.disconnect()
                try:
                    await second.connect_when_ready()
                    self.assertTrue((await second.call("qt.ping"))["result"]["pong"])
                finally:
                    await second.disconnect()
            finally:
                await browser.close()

    async def test_reconnect_receives_only_notifications_queued_while_disconnected(self) -> None:
        async with async_playwright() as playwright:
            browser = await playwright.chromium.launch(executable_path=CHROME, headless=True)
            try:
                page = await browser.new_page()
                await page.goto(f"http://127.0.0.1:{self.server.server_port}/qtpilot_webmcp_example.html")
                connection = PageProbeConnection(page)
                await connection.connect_when_ready()
                try:
                    window = await connection.call("qt.objects.search", {"objectName": "demoWindow"})
                    button = await connection.call("qt.objects.search", {"objectName": "incrementButton"})
                    button_id = button["result"]["objects"][0]["objectId"]
                    subscription = await connection.call(
                        "qt.signals.subscribe",
                        {"objectId": window["result"]["objects"][0]["objectId"], "signal": "countChanged"},
                    )
                    subscription_id = subscription["result"]["subscriptionId"]
                    received: asyncio.Queue[dict[str, object]] = asyncio.Queue()

                    def observe(method: str, params: dict[str, object]) -> None:
                        if method == "qtpilot.signalEmitted" and params.get("subscriptionId") == subscription_id:
                            received.put_nowait(params)

                    connection.add_notification_handler(observe)
                    await connection.call("qt.ui.click", {"objectId": button_id})
                    await asyncio.wait_for(received.get(), timeout=5)
                    self.assertEqual(await page.evaluate("qtpilot.notifications.length"), 0)

                    await connection.disconnect()
                    reply = await page.evaluate(
                        """id => JSON.parse(qtpilot.call(JSON.stringify({
                            jsonrpc: '2.0', id: 1, method: 'qt.ui.click', params: {objectId: id}
                        })))""",
                        button_id,
                    )
                    self.assertNotIn("error", reply)
                    await page.wait_for_function(
                        """id => qtpilot.notifications.some(text => {
                            const message = JSON.parse(text);
                            return message.method === 'qtpilot.signalEmitted'
                                && message.params.subscriptionId === id;
                        })""",
                        arg=subscription_id,
                    )
                    await connection.connect_when_ready()
                    await asyncio.wait_for(received.get(), timeout=5)
                    self.assertTrue(received.empty())
                    self.assertEqual(await page.evaluate("qtpilot.notifications.length"), 0)
                finally:
                    await connection.disconnect()
            finally:
                await browser.close()


if __name__ == "__main__":
    unittest.main()
