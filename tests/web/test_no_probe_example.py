"""Verify that the default WASM example build does not expose a probe."""

from __future__ import annotations

import functools
import os
import threading
import unittest
from http.server import ThreadingHTTPServer
from pathlib import Path

from playwright.sync_api import sync_playwright

from test_webmcp_example import CHROME, ROOT, IsolatedHandler


class NoProbeExampleTest(unittest.TestCase):
    def test_default_build_has_no_bridge(self) -> None:
        app_dir = Path(os.environ.get("QTPILOT_WASM_APP_NO_PROBE_DIR", ROOT / "build/webmcp-example-no-probe"))
        if not (app_dir / "qtpilot_webmcp_example.html").is_file():
            raise RuntimeError(f"Build the example without the probe first: {app_dir}")
        server = ThreadingHTTPServer(
            ("127.0.0.1", 0), functools.partial(IsolatedHandler, directory=str(app_dir))
        )
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            with sync_playwright() as playwright:
                browser = playwright.chromium.launch(executable_path=CHROME, headless=True)
                try:
                    page = browser.new_page()
                    page.goto(f"http://127.0.0.1:{server.server_port}/qtpilot_webmcp_example.html")
                    page.locator("canvas").first.wait_for(timeout=60000)
                    self.assertEqual(page.evaluate("typeof globalThis.qtpilot"), "undefined")
                finally:
                    browser.close()
        finally:
            server.shutdown()
            server.server_close()
            thread.join()


if __name__ == "__main__":
    unittest.main()
