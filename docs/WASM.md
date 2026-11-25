# WebAssembly Support (Qt for WebAssembly in a browser)

A Qt for WebAssembly app runs as a page in a browser tab, and a page cannot listen on a port. So
the probe is delivered the way it is on mobile (linked into a development build) but reached
differently: instead of a WebSocket server it installs a JavaScript bridge in the page, and the
client talks to the page.

| | Desktop | Mobile (Android, iOS) | WebAssembly |
|---|---|---|---|
| Delivery | Injected | Linked | Linked |
| Probe artifact | Shared library | Static library (`.a`) | Static library (`.a`) |
| Transport | WebSocket server `:9222` | WebSocket server `:9222` | `globalThis.qtpilot` in the page |
| Client | `ProbeConnection(ws_url)` | `ProbeConnection(ws_url)` | `PageProbeConnection(page)` |
| Discovery (UDP) | Yes | Yes | No |

Everything behind the transport — the JSON-RPC handler, introspection, interaction,
accessibility, the `qt.*`, `cu.*` and `chr.*` methods — is the same code.

## The bridge

A WebAssembly probe (`src/probe/transport/js_bridge.h`) publishes, once Qt has started:

- `qtpilot.call(request)` — hands one JSON-RPC request (a string, or an object it serialises) to
  the handler and returns the reply string. It runs on the browser's main thread, which is Qt's
  GUI thread in a Qt for WebAssembly app, so methods run where they do over a socket.
- `qtpilot.notifications` — the newest notifications (up to 1000) while nobody listens.
- `qtpilot.onnotification` — set it to a function to receive each notification as it is sent.

```js
// In the browser's DevTools console:
JSON.parse(qtpilot.call({jsonrpc: "2.0", id: 1, method: "qt.ping", params: {}}))
```

## Building

Build the probe with the WebAssembly kit's `qt-cmake` and the Emscripten release that kit was
built with. `EMSCRIPTEN` switches qtPilot into linked mode: a static probe, no launcher, and the
JavaScript bridge in place of the socket transport.

```bash
source /path/to/emsdk/emsdk_env.sh
/path/to/Qt/6.10.0/wasm_multithread/bin/qt-cmake -B build-wasm -S . \
    -DCMAKE_BUILD_TYPE=Release -DQT_HOST_PATH=/path/to/Qt/6.10.0/gcc_64
cmake --build build-wasm
```

Link `libqtPilot-probe-qt6.N.a` into the app as on mobile: through the installed package's
`qtPilot_inject_probe(<target>)`, or by raw path with `$<LINK_LIBRARY:WHOLE_ARCHIVE,...>` so
the archive's startup hook is kept (and `-fexceptions`, which `qtPilot::Probe` adds via its
usage requirements). Only ever in a development build (see Security).

## Driving it from Python

`qtpilot.page_connection.PageProbeConnection` is a `ProbeConnection` whose socket is a Playwright
page, so everything built on a connection works unchanged. It is a Python library API for
Playwright scripts (not exposed via the `qtpilot serve` CLI). Playwright is not a qtPilot
dependency; install it alongside (`pip install playwright`).

```python
from playwright.async_api import async_playwright
from qtpilot.page_connection import PageProbeConnection

async with async_playwright() as p:
    browser = await p.chromium.launch()
    page = await browser.new_page()
    await page.goto(app_url)
    connection = PageProbeConnection(page)
    await connection.connect_when_ready()
    print(await connection.call("qt.ping"))
```

A multithreaded Qt for WebAssembly build needs the page to be cross-origin isolated: serve it
with `Cross-Origin-Opener-Policy: same-origin` and `Cross-Origin-Embedder-Policy: require-corp`.

## Tools for a browser agent (WebMCP)

The bridge also carries an adapter for [WebMCP](https://developer.chrome.com/docs/ai/webmcp),
Chrome's experiment that lets a page offer tools to an AI agent in the browser
(`modelContext.registerTool`). When the page opts in — `?qtpilot-webmcp` in its address, or
`globalThis.qtpilotWebMcp = true` before the app starts — and the browser offers `modelContext`,
the probe registers:

| Tool | Method |
|---|---|
| `qt_ping` | `qt.ping` |
| `qt_find` | `qt.objects.search` (objectName, className, limit) |
| `qt_get_property` | `qt.properties.get` |
| `qt_click` | `qt.ui.click` |
| `qt_screenshot` | `cu.screenshot` |

`qtpilot.installWebMcp({allowInvoke: true})` adds `qt_invoke` (`qt.methods.invoke`), the one tool
that can change anything the app can; `qtpilot.uninstallWebMcp()` removes them all. Without the
opt-in, or without `modelContext`, nothing is registered. The adapter is
`src/probe/transport/webmcp.js`, tested in Node with a stand-in page (`node --test tests/web/*.test.mjs`).
WebMCP is experimental and its entry point is still changing (`navigator.modelContext` or
`document.modelContext`; both are looked for).

Tool results are JSON text containing the probe's response envelope. A probe error is JSON text
with an `error` field containing its code, message and optional data; it is not a successful
`result`. Chrome otherwise replaces exceptions thrown by a tool with a generic error.

## Local Qt Quick example

`examples/webmcp` is a small Qt Quick counter. Playwright can load the page while
`PageProbeConnection` finds and clicks its Qt button and reads the counter. The same app also
supports the optional WebMCP tools. The probe is **off by default**; enable it only for a local
development build, never for a distributed app.

For example, with Qt 6.12 and Chrome 157 on macOS, Playwright's DOM role locator for the
counter's `Increment` button finds no element, while `qt.objects.search` finds the named Qt
button. This is specific to that build and browser; Qt accessibility support can change.

With matching host and WASM Qt kits and the kit's Emscripten release active:

```bash
# Set these to the same Qt version's WASM and host installations.
export QT_WASM=/path/to/Qt/6.12.0/wasm_multithread
export QT_HOST=/path/to/Qt/6.12.0/host

"$QT_WASM/bin/qt-cmake" -S . -B build/wasm -DCMAKE_BUILD_TYPE=Release -DQT_HOST_PATH="$QT_HOST"
cmake --build build/wasm --parallel
cmake --install build/wasm --prefix "$PWD/build/wasm-install"
"$QT_WASM/bin/qt-cmake" -S examples/webmcp -B build/webmcp-example \
  -DCMAKE_BUILD_TYPE=Release -DQT_HOST_PATH="$QT_HOST" \
  -DqtPilot_DIR="$PWD/build/wasm-install/share/cmake/qtPilot" \
  -DQTPILOT_EXAMPLE_ENABLE_PROBE=ON
cmake --build build/webmcp-example --parallel
```

For manual use, serve the build directory on localhost with cross-origin isolation, for example
`uvx --from qtwasmserver qtwasmserver -i build/webmcp-example`. Open the generated HTML page.
Append `?qtpilot-webmcp` to opt in to WebMCP, and enable Chrome's WebMCP testing flag. The
page bridge works without that flag or the query parameter.

The browser regressions use an isolated loopback server and a fresh headless Chrome process:

```bash
# Set QTPILOT_CHROME to the Chrome executable if it is not detected automatically.
uv run --project python --locked --with playwright python tests/web/test_page_connection_example.py -v
uv run --project python --locked --with playwright python tests/web/test_webmcp_example.py -v
```

The multithreaded Qt kit links with `-pthread` and `ALLOW_MEMORY_GROWTH`, so Emscripten
warns that JavaScript memory accesses may be slower. This also happens without qtPilot.
Keep growth enabled for compatibility with varying workloads; do not treat the warning as
evidence of an observed slowdown. Measure representative applications before changing
their initial memory or trying experimental growable-buffer support.

To verify that the default build has no probe, configure and build a second copy without the
opt-in, then run its check:

```bash
"$QT_WASM/bin/qt-cmake" -S examples/webmcp -B build/webmcp-example-no-probe \
  -DCMAKE_BUILD_TYPE=Release -DQT_HOST_PATH="$QT_HOST"
cmake --build build/webmcp-example-no-probe --parallel
uv run --project python --locked --with playwright python tests/web/test_no_probe_example.py -v
```

These tests exercise the real page bridge and Chrome's experimental tool registry; they do not
establish that a particular AI agent can discover or use the tools.

## Limits

- **No browser user activation.** Input the probe synthesises reaches Qt, not the browser, so it
  does not count as a user gesture: it cannot open the browser's file chooser, start fullscreen or
  play audio. Use the page (Playwright) for those.
- **No discovery.** UDP broadcast does not exist in a browser; the client names the page.
- **One page, one probe.** Each tab is its own app instance with its own bridge.

## Security

The bridge opens no port: only script running in that page can call it — the page itself, the
browser's DevTools, an extension with access to the page, or an automation driver. Such script
can already read and change the page's memory, so the bridge adds convenience rather than reach.
It still gives full control of the app (`qt.methods.invoke` calls any invokable), so keep the
probe out of builds you distribute, as on mobile.
