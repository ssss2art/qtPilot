"""Exercise the Qt WASM example through Chrome's real WebMCP registry."""

from __future__ import annotations

import base64
import functools
import json
import os
import shutil
import threading
import unittest
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from playwright.sync_api import Browser, Page, Playwright, sync_playwright


ROOT = Path(__file__).resolve().parents[2]
APP_DIR = Path(os.environ.get("QTPILOT_WASM_APP_DIR", ROOT / "build/webmcp-example"))
CHROME = os.environ.get("QTPILOT_CHROME") or shutil.which("google-chrome")
if CHROME is None and Path("/Applications/Google Chrome.app").is_dir():
    CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
TOOLS = {"qt_ping", "qt_find", "qt_get_property", "qt_click", "qt_screenshot"}


class IsolatedHandler(SimpleHTTPRequestHandler):
    def end_headers(self) -> None:
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        super().end_headers()

    def log_message(self, format: str, *args: object) -> None:
        pass


class WebMcpExampleTest(unittest.TestCase):
    server: ThreadingHTTPServer
    thread: threading.Thread
    playwright: Playwright
    browser: Browser

    @classmethod
    def setUpClass(cls) -> None:
        if not (APP_DIR / "qtpilot_webmcp_example.html").is_file():
            raise RuntimeError(f"Build the WASM example first: {APP_DIR}")
        cls.server = ThreadingHTTPServer(
            ("127.0.0.1", 0), functools.partial(IsolatedHandler, directory=str(APP_DIR))
        )
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.playwright = sync_playwright().start()
        try:
            cls.browser = cls.playwright.chromium.launch(
                executable_path=CHROME,
                headless=True,
                args=["--enable-features=WebMCP,WebMCPTesting"],
            )
        except Exception:
            cls.playwright.stop()
            cls.server.shutdown()
            cls.server.server_close()
            cls.thread.join()
            raise

    @classmethod
    def tearDownClass(cls) -> None:
        cls.browser.close()
        cls.playwright.stop()
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join()

    def open_app(self, opt_in: bool) -> Page:
        page = self.browser.new_page()
        self.addCleanup(page.close)
        query = "?qtpilot-webmcp" if opt_in else ""
        page.goto(f"http://127.0.0.1:{self.server.server_port}/qtpilot_webmcp_example.html{query}")
        page.wait_for_function("() => globalThis.qtpilot?.call && document.modelContext", timeout=60000)
        self.assertTrue(page.evaluate("crossOriginIsolated"))
        return page

    def call_tool(self, page: Page, name: str, arguments: dict[str, object]) -> dict[str, object]:
        response = page.evaluate(
            """async ({name, arguments: args}) => {
                const tool = (await document.modelContext.getTools()).find(t => t.name === name);
                return await document.modelContext.executeTool(tool, args);
            }""",
            {"name": name, "arguments": arguments},
        )
        self.assertIsInstance(response, str)
        return json.loads(response)["result"]

    def test_registration_requires_opt_in(self) -> None:
        page = self.open_app(False)
        self.assertEqual(page.evaluate("async () => (await document.modelContext.getTools()).length"), 0)

        page = self.open_app(True)
        page.wait_for_function(
            "async () => (await document.modelContext.getTools()).length === 5", timeout=10000
        )
        names = page.evaluate("async () => (await document.modelContext.getTools()).map(tool => tool.name)")
        self.assertEqual(set(names), TOOLS)

    def test_browser_tool_workflow_changes_qt_state(self) -> None:
        page = self.open_app(True)
        page.wait_for_function("async () => (await document.modelContext.getTools()).length === 5")
        self.assertTrue(self.call_tool(page, "qt_ping", {})["pong"])

        found = self.call_tool(page, "qt_find", {"objectName": "incrementButton"})
        self.assertEqual(found["count"], 1)
        button_id = found["objects"][0]["objectId"]
        window = self.call_tool(page, "qt_find", {"objectName": "demoWindow"})
        window_id = window["objects"][0]["objectId"]
        self.assertEqual(self.call_tool(page, "qt_get_property", {"objectId": window_id, "name": "count"})["value"], 0)
        self.call_tool(page, "qt_click", {"objectId": button_id})
        page.wait_for_function(
            """async id => {
                const tools = await document.modelContext.getTools();
                const tool = tools.find(t => t.name === 'qt_get_property');
                const reply = await document.modelContext.executeTool(tool, {objectId: id, name: 'count'});
                return JSON.parse(reply).result.value === 1;
            }""",
            arg=window_id,
        )

        screenshot = self.call_tool(page, "qt_screenshot", {})
        self.assertGreater(screenshot["width"], 0)
        self.assertTrue(base64.b64decode(screenshot["image"]).startswith(b"\x89PNG\r\n\x1a\n"))

    def test_invoke_and_registration_lifecycle_are_page_local(self) -> None:
        page = self.open_app(True)
        other = self.open_app(True)
        page.wait_for_function("async () => (await document.modelContext.getTools()).length === 5")
        page.evaluate("qtpilot.installWebMcp({allowInvoke: true})")
        page.wait_for_function("async () => (await document.modelContext.getTools()).length === 6")
        window = self.call_tool(page, "qt_find", {"objectName": "demoWindow"})
        window_id = window["objects"][0]["objectId"]
        button = self.call_tool(page, "qt_find", {"objectName": "incrementButton"})
        self.call_tool(page, "qt_click", {"objectId": button["objects"][0]["objectId"]})
        self.call_tool(page, "qt_invoke", {"objectId": window_id, "method": "resetCount"})
        self.assertEqual(self.call_tool(page, "qt_get_property", {"objectId": window_id, "name": "count"})["value"], 0)

        page.evaluate("qtpilot.uninstallWebMcp()")
        page.wait_for_function("async () => (await document.modelContext.getTools()).length === 0")
        self.assertEqual(other.evaluate("async () => (await document.modelContext.getTools()).length"), 5)
        page.evaluate("qtpilot.installWebMcp()")
        page.wait_for_function("async () => (await document.modelContext.getTools()).length === 5")

    def test_invalid_object_surfaces_a_tool_error(self) -> None:
        page = self.open_app(True)
        page.wait_for_function("async () => (await document.modelContext.getTools()).length === 5")
        response = page.evaluate(
            """async () => {
                const tool = (await document.modelContext.getTools()).find(t => t.name === 'qt_get_property');
                return await document.modelContext.executeTool(
                    tool, {objectId: 'missing-object', name: 'text'});
            }"""
        )
        error = json.loads(response)
        self.assertNotIn("result", error)
        self.assertIn("Object not found", error["error"]["message"])
        self.assertTrue(self.call_tool(page, "qt_ping", {})["pong"])


if __name__ == "__main__":
    unittest.main()
