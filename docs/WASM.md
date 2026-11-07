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
the archive's startup hook is kept. Only ever in a development build (see Security).

## Driving it from Python

`qtpilot.page_connection.PageProbeConnection` is a `ProbeConnection` whose socket is a Playwright
page, so everything built on a connection works unchanged. Playwright is not a qtPilot
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
