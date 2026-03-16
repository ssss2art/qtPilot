# Phase 7: Python Integration - Research

**Researched:** 2026-02-01
**Domain:** Python MCP server bridging Claude to Qt probe via WebSocket
**Confidence:** HIGH

## Summary

Phase 7 delivers a Python MCP server package (`qtpilot`) that bridges Claude (via MCP protocol over stdio) to the qtPilot probe (via WebSocket/JSON-RPC). The server operates in one of three modes (`--mode native|cu|chrome`), each exposing only its own set of tools. The architecture is: Claude <-> stdio <-> FastMCP server <-> WebSocket <-> Qt probe.

The standard stack is FastMCP 2.x (decorator-based MCP framework, production-stable) + `websockets` 16.x (asyncio WebSocket client). FastMCP handles all MCP protocol details (tool registration, schema generation, stdio transport), while `websockets` provides the async connection to the probe. The lifespan pattern in FastMCP cleanly maps to WebSocket connection lifecycle management.

**Primary recommendation:** Use FastMCP 2.x with `@mcp.tool` decorators and its `@lifespan` hook for WebSocket connection management. Each tool function sends a JSON-RPC request over the WebSocket and returns the result. Keep tools thin -- they are just bridges, not business logic.

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| [fastmcp](https://pypi.org/project/fastmcp/) | 2.x (`<3`) | MCP server framework | Powers 70% of MCP servers. Decorator-based, handles schema generation from type hints/docstrings, stdio transport built-in. FastMCP 1.0 was incorporated into official MCP SDK. Pin to `<3` for stability (3.0 is beta). |
| [websockets](https://pypi.org/project/websockets/) | 16.x | Async WebSocket client | Standard Python WebSocket library. v16.0 released Jan 2026 with CPython 3.14 support. asyncio-native, auto-reconnect via infinite iterator, production-proven. |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| Python | 3.11+ | Runtime | FastMCP 2.x requires 3.10+. 3.11+ for best asyncio performance and ExceptionGroup support. |
| [uv](https://docs.astral.sh/uv/) | latest | Package/project manager | Recommended by MCP ecosystem for dependency isolation. Used in Claude Desktop config for ephemeral envs. |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| FastMCP 2.x | Official MCP Python SDK (`mcp` package) | More boilerplate, lower-level. FastMCP wraps it. Official SDK v2 not yet stable (Q1 2026 target). |
| FastMCP 2.x | FastMCP 3.0 beta | New features but still beta. Pin to v2 for stability. |
| `websockets` | `aiohttp` | Heavier dependency (full HTTP framework). `websockets` is focused and lighter for pure WS client. |

**Installation:**
```bash
pip install 'fastmcp<3' websockets
# Or with uv:
uv pip install 'fastmcp<3' websockets
```

## Architecture Patterns

### Recommended Project Structure
```
python/
├── pyproject.toml         # Package metadata, CLI entry point
├── src/
│   └── qtpilot/
│       ├── __init__.py    # Package version
│       ├── __main__.py    # python -m qtpilot entry point
│       ├── cli.py         # argparse: --mode, --ws-url, --target, --port
│       ├── connection.py  # WebSocket client + JSON-RPC request/response
│       ├── server.py      # FastMCP server factory (creates server per mode)
│       ├── tools/
│       │   ├── __init__.py
│       │   ├── native.py  # ~33 qt.* tool definitions
│       │   ├── cu.py      # 13 cu.* tool definitions
│       │   └── chrome.py  # 8 chr.* tool definitions
│       └── status.py      # Status resource endpoint
├── tests/
│   ├── test_connection.py
│   ├── test_tools_native.py
│   ├── test_tools_cu.py
│   ├── test_tools_chrome.py
│   └── conftest.py        # Mock WebSocket fixtures
└── README.md              # Config snippets for Claude Desktop/Code
```

### Pattern 1: Lifespan for WebSocket Connection
**What:** Use FastMCP's `@lifespan` decorator to establish the WebSocket connection at server startup and tear it down on shutdown. The connection object is passed to tools via `ctx.lifespan_context`.
**When to use:** Always -- this is the single connection lifecycle pattern.
**Example:**
```python
# Source: https://gofastmcp.com/servers/lifespan
from fastmcp import FastMCP, Context
from fastmcp.server.lifespan import lifespan
from qtpilot.connection import ProbeConnection

@lifespan
async def probe_lifespan(server):
    conn = ProbeConnection(ws_url="ws://localhost:9222")
    await conn.connect()
    try:
        yield {"probe": conn}
    finally:
        await conn.disconnect()

mcp = FastMCP("qtPilot Native", lifespan=probe_lifespan)

@mcp.tool
async def qt_objects_find(name: str, root: str | None = None, ctx: Context) -> dict:
    """Find a Qt object by objectName.
    Example: qt_objects_find(name="submitButton")
    """
    probe = ctx.lifespan_context["probe"]
    return await probe.call("qt.objects.find", {"name": name, "root": root})
```

### Pattern 2: Thin Bridge Tools
**What:** Each MCP tool is a thin wrapper that forwards parameters to the probe via JSON-RPC and returns the result. No business logic in the tool function -- the probe does all the work.
**When to use:** All tools follow this pattern.
**Example:**
```python
@mcp.tool
async def cu_screenshot(ctx: Context) -> dict:
    """Take a screenshot of the active Qt window.
    Returns base64 PNG image data.
    Example: cu_screenshot()
    """
    probe = ctx.lifespan_context["probe"]
    return await probe.call("cu.screenshot", {})
```

### Pattern 3: JSON-RPC over WebSocket
**What:** A `ProbeConnection` class encapsulates the WebSocket client and JSON-RPC 2.0 request/response correlation. It assigns sequential IDs, sends `{"jsonrpc":"2.0","method":...,"params":...,"id":N}`, and matches responses by ID.
**When to use:** All probe communication.
**Example:**
```python
import json
import asyncio
from websockets.asyncio.client import connect

class ProbeConnection:
    def __init__(self, ws_url: str):
        self._ws_url = ws_url
        self._ws = None
        self._next_id = 1
        self._pending: dict[int, asyncio.Future] = {}

    async def connect(self):
        self._ws = await connect(self._ws_url)
        asyncio.create_task(self._recv_loop())

    async def call(self, method: str, params: dict) -> dict:
        req_id = self._next_id
        self._next_id += 1
        msg = {"jsonrpc": "2.0", "method": method, "params": params, "id": req_id}
        future = asyncio.get_event_loop().create_future()
        self._pending[req_id] = future
        await self._ws.send(json.dumps(msg))
        result = await future
        return result

    async def _recv_loop(self):
        async for raw in self._ws:
            msg = json.loads(raw)
            req_id = msg.get("id")
            if req_id and req_id in self._pending:
                if "error" in msg:
                    self._pending.pop(req_id).set_exception(
                        ProbeError(msg["error"]))
                else:
                    self._pending.pop(req_id).set_result(msg["result"])
```

### Pattern 4: Mode-Based Server Factory
**What:** A factory function creates and configures the FastMCP server based on the selected mode, registering only that mode's tools.
**When to use:** Server initialization based on `--mode` CLI arg.
**Example:**
```python
def create_server(mode: str, ws_url: str) -> FastMCP:
    @lifespan
    async def probe_lifespan(server):
        conn = ProbeConnection(ws_url)
        await conn.connect()
        try:
            yield {"probe": conn}
        finally:
            await conn.disconnect()

    mcp = FastMCP(f"qtPilot {mode.title()}", lifespan=probe_lifespan)

    if mode == "native":
        register_native_tools(mcp)
    elif mode == "cu":
        register_cu_tools(mcp)
    elif mode == "chrome":
        register_chrome_tools(mcp)

    register_status_resource(mcp)
    return mcp
```

### Pattern 5: Status Resource
**What:** A single MCP resource endpoint exposing probe connection state.
**When to use:** For Claude/agents to check if the probe is connected.
**Example:**
```python
@mcp.resource("qtpilot://status")
async def get_status(ctx: Context) -> str:
    probe = ctx.lifespan_context["probe"]
    return json.dumps({
        "connected": probe.is_connected,
        "ws_url": probe.ws_url,
        "mode": probe.mode,
        "target": probe.target_info
    })
```

### Anti-Patterns to Avoid
- **Fat tools with logic:** Don't put Qt introspection logic in Python. The probe handles everything -- tools are just bridges.
- **Synchronous WebSocket calls:** All tool functions must be `async`. FastMCP supports async tools natively. Never use `asyncio.run()` inside a tool.
- **Global connection state:** Don't use module-level globals for the WebSocket connection. Use lifespan context to ensure proper cleanup.
- **Re-implementing JSON-RPC:** Don't hand-roll JSON-RPC parsing. Use a simple request/response correlator class.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| MCP protocol handling | Custom JSON-RPC MCP server | FastMCP 2.x | Protocol compliance, schema generation, transport handling are all solved. |
| Tool schema generation | Manual JSON schema for each tool | FastMCP type hints + docstrings | FastMCP auto-generates from Python type annotations and docstrings. |
| WebSocket reconnection | Custom retry loop | `websockets` auto-reconnect iterator | `connect()` as async iterator handles reconnection with backoff. |
| Stdio MCP transport | Custom stdin/stdout reader | `mcp.run()` | FastMCP handles all stdio framing and message routing. |
| CLI argument parsing | Manual sys.argv | `argparse` (stdlib) | Simple, no dependency needed. |
| Environment variable config | Custom env loading | `os.environ.get()` with defaults | Standard Python pattern, no library needed. |

**Key insight:** The Python MCP server is a pure bridge. It translates MCP tool calls into JSON-RPC requests and returns results. All intelligence lives in the Qt probe. The Python code should be minimal glue.

## Common Pitfalls

### Pitfall 1: Logging to stdout in stdio mode
**What goes wrong:** Any `print()` or stdout logging corrupts the JSON-RPC messages between Claude and the MCP server.
**Why it happens:** Stdio transport uses stdout for protocol messages. Python's default print goes to stdout.
**How to avoid:** Use `logging` module configured to write to stderr only. FastMCP's Context.info()/Context.warning() log to stderr correctly.
**Warning signs:** "Parse error" from Claude, garbled tool responses.

### Pitfall 2: Blocking the event loop
**What goes wrong:** Using synchronous operations (e.g., `time.sleep()`, synchronous HTTP) blocks all tool handling.
**Why it happens:** FastMCP runs on asyncio. Blocking calls stall the entire server.
**How to avoid:** Use `await asyncio.sleep()`, async WebSocket operations, `asyncio.to_thread()` for unavoidable sync calls.
**Warning signs:** Tools timing out, unresponsive server.

### Pitfall 3: Connection lifecycle mismatch
**What goes wrong:** WebSocket connects but probe disconnects (e.g., target app closes). Tools continue to be called but fail.
**Why it happens:** MCP server stays running even when the probe WebSocket drops.
**How to avoid:** Implement connection state tracking in ProbeConnection. Tools should return clear error messages when disconnected. Optionally use `websockets` reconnect iterator for auto-reconnection.
**Warning signs:** Repeated "connection closed" errors from tools.

### Pitfall 4: Tool count overwhelming Claude
**What goes wrong:** Native mode has ~33 tools. Claude may struggle with too many tools or consume excessive context.
**Why it happens:** Each tool definition takes context tokens. Claude Code enables Tool Search automatically when tools exceed 10% of context.
**How to avoid:** Keep tool descriptions minimal (1-2 sentences + 1 example). This is already specified in CONTEXT.md decisions. The per-mode design (only one mode's tools at a time) inherently limits tool count.
**Warning signs:** Claude not picking the right tool, slow responses.

### Pitfall 5: Windows cmd /c wrapper for Claude Desktop
**What goes wrong:** On Windows, Claude Desktop cannot directly execute Python/uv commands.
**Why it happens:** Windows process spawning requires cmd.exe wrapper for non-.exe commands.
**How to avoid:** Document Windows config with `"command": "cmd"` and `"args": ["/c", "uv", "run", ...]` pattern. This is a known MCP ecosystem issue.
**Warning signs:** "Connection closed" or "spawn ENOENT" errors in Claude Desktop.

### Pitfall 6: Auto-launch subprocess management
**What goes wrong:** The MCP server launches qtpilot-launch.exe but doesn't clean up the child process on shutdown.
**Why it happens:** Subprocess lifecycle not tied to server lifespan.
**How to avoid:** Use the lifespan pattern to start the subprocess and terminate it in the `finally` block. Use `asyncio.create_subprocess_exec()` for non-blocking management.
**Warning signs:** Orphaned Qt app processes after closing Claude.

## Code Examples

### Complete minimal server (Native mode)
```python
# Source: FastMCP docs (gofastmcp.com) + websockets docs
import argparse
import json
from fastmcp import FastMCP, Context
from fastmcp.server.lifespan import lifespan
from qtpilot.connection import ProbeConnection

@lifespan
async def probe_lifespan(server):
    conn = ProbeConnection(ws_url="ws://localhost:9222")
    await conn.connect()
    try:
        yield {"probe": conn}
    finally:
        await conn.disconnect()

mcp = FastMCP("qtPilot Native", lifespan=probe_lifespan)

@mcp.tool
async def qt_objects_find(
    name: str,
    root: str | None = None,
    ctx: Context = None,
) -> dict:
    """Find a Qt object by its objectName property.
    Example: qt_objects_find(name="submitButton")
    """
    probe = ctx.lifespan_context["probe"]
    params = {"name": name}
    if root:
        params["root"] = root
    return await probe.call("qt.objects.find", params)

@mcp.tool
async def qt_properties_get(
    objectId: str,
    name: str,
    ctx: Context = None,
) -> dict:
    """Read a property value from a Qt object.
    Example: qt_properties_get(objectId="QMainWindow/centralWidget", name="visible")
    """
    probe = ctx.lifespan_context["probe"]
    return await probe.call("qt.properties.get", {"objectId": objectId, "name": name})

if __name__ == "__main__":
    mcp.run()
```

### Claude Desktop config (Windows)
```json
{
  "mcpServers": {
    "qtpilot-native": {
      "command": "cmd",
      "args": ["/c", "uv", "run", "--with", "fastmcp<3", "--with", "websockets",
               "python", "-m", "qtpilot", "--mode", "native",
               "--ws-url", "ws://localhost:9222"],
      "env": {}
    }
  }
}
```

### Claude Desktop config (macOS/Linux)
```json
{
  "mcpServers": {
    "qtpilot-native": {
      "command": "uv",
      "args": ["run", "--with", "fastmcp<3", "--with", "websockets",
               "python", "-m", "qtpilot", "--mode", "native",
               "--ws-url", "ws://localhost:9222"],
      "env": {}
    }
  }
}
```

### Claude Code config
```bash
claude mcp add --transport stdio qtpilot-native -- uv run --with "fastmcp<3" --with websockets python -m qtpilot --mode native --ws-url ws://localhost:9222
```

### Auto-launch config (target app)
```json
{
  "mcpServers": {
    "qtpilot-native": {
      "command": "uv",
      "args": ["run", "--with", "fastmcp<3", "--with", "websockets",
               "python", "-m", "qtpilot", "--mode", "native",
               "--target", "C:/path/to/myapp.exe"],
      "env": {
        "QTPILOT_PORT": "9222"
      }
    }
  }
}
```

### pyproject.toml
```toml
[project]
name = "qtpilot"
version = "0.1.0"
description = "MCP server for controlling Qt applications via qtPilot probe"
requires-python = ">=3.11"
dependencies = [
    "fastmcp>=2.0,<3",
    "websockets>=14.0",
]

[project.scripts]
qtpilot = "qtpilot.cli:main"

[build-system]
requires = ["hatchling"]
build-backend = "hatchling.build"
```

## Tool Inventory

### Native Mode (~33 tools)
Mapped from qt.* JSON-RPC methods. Tool names use underscores (Python convention):

| MCP Tool Name | JSON-RPC Method | Key Parameters |
|---------------|----------------|----------------|
| qt_ping | qt.ping | (none) |
| qt_version | qt.version | (none) |
| qt_modes | qt.modes | (none) |
| qt_objects_find | qt.objects.find | name, root? |
| qt_objects_findByClass | qt.objects.findByClass | className, root? |
| qt_objects_tree | qt.objects.tree | root?, maxDepth? |
| qt_objects_info | qt.objects.info | objectId |
| qt_objects_inspect | qt.objects.inspect | objectId |
| qt_objects_query | qt.objects.query | className?, properties?, root? |
| qt_properties_list | qt.properties.list | objectId |
| qt_properties_get | qt.properties.get | objectId, name |
| qt_properties_set | qt.properties.set | objectId, name, value |
| qt_methods_list | qt.methods.list | objectId |
| qt_methods_invoke | qt.methods.invoke | objectId, method, args? |
| qt_signals_list | qt.signals.list | objectId |
| qt_signals_subscribe | qt.signals.subscribe | objectId, signal |
| qt_signals_unsubscribe | qt.signals.unsubscribe | subscriptionId |
| qt_signals_setLifecycle | qt.signals.setLifecycle | enabled |
| qt_ui_click | qt.ui.click | objectId, button?, position? |
| qt_ui_sendKeys | qt.ui.sendKeys | objectId, text?, sequence? |
| qt_ui_screenshot | qt.ui.screenshot | objectId, fullWindow?, region? |
| qt_ui_geometry | qt.ui.geometry | objectId |
| qt_ui_hitTest | qt.ui.hitTest | x, y |
| qt_names_register | qt.names.register | name, path |
| qt_names_unregister | qt.names.unregister | name |
| qt_names_list | qt.names.list | (none) |
| qt_names_validate | qt.names.validate | (none) |
| qt_names_load | qt.names.load | filePath |
| qt_qml_inspect | qt.qml.inspect | objectId |
| qt_models_list | qt.models.list | (none) |
| qt_models_info | qt.models.info | objectId |
| qt_models_data | qt.models.data | objectId, row?, column?, role?, offset?, limit? |

### Computer Use Mode (13 tools)
| MCP Tool Name | JSON-RPC Method | Key Parameters |
|---------------|----------------|----------------|
| cu_screenshot | cu.screenshot | (none) |
| cu_leftClick | cu.leftClick | x, y, screenAbsolute?, delay_ms? |
| cu_rightClick | cu.rightClick | x, y, screenAbsolute?, delay_ms? |
| cu_middleClick | cu.middleClick | x, y, screenAbsolute?, delay_ms? |
| cu_doubleClick | cu.doubleClick | x, y, screenAbsolute?, delay_ms? |
| cu_mouseMove | cu.mouseMove | x, y, screenAbsolute? |
| cu_mouseDrag | cu.mouseDrag | startX, startY, endX, endY, screenAbsolute? |
| cu_mouseDown | cu.mouseDown | x, y, button?, screenAbsolute? |
| cu_mouseUp | cu.mouseUp | x, y, button?, screenAbsolute? |
| cu_type | cu.type | text |
| cu_key | cu.key | key |
| cu_scroll | cu.scroll | x, y, direction, amount?, screenAbsolute? |
| cu_cursorPosition | cu.cursorPosition | (none) |

### Chrome Mode (8 tools)
| MCP Tool Name | JSON-RPC Method | Key Parameters |
|---------------|----------------|----------------|
| chr_readPage | chr.readPage | filter?, maxDepth? |
| chr_click | chr.click | ref |
| chr_formInput | chr.formInput | ref, value |
| chr_getPageText | chr.getPageText | (none) |
| chr_find | chr.find | query |
| chr_navigate | chr.navigate | ref |
| chr_tabsContext | chr.tabsContext | (none) |
| chr_readConsoleMessages | chr.readConsoleMessages | limit?, pattern?, clear? |

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Official MCP Python SDK (low-level) | FastMCP 2.x (high-level) | 2024 (FastMCP 1.0 merged into SDK) | Use FastMCP for all new servers. Less boilerplate. |
| `websockets` legacy API | `websockets.asyncio.client` | v14.0 (legacy deprecated) | Use `websockets.asyncio.client.connect()`, not `websockets.connect()`. |
| SSE transport for remote MCP | HTTP Streamable transport | 2025 | SSE deprecated. Stdio remains standard for local servers. |
| Manual `claude_desktop_config.json` | `fastmcp install claude-desktop` | FastMCP v2.10.3 | Easier installation, but manual config still works and is more explicit. |
| `pip install` for MCP servers | `uv run --with` ephemeral envs | 2025 | Claude Desktop/Code ecosystem prefers uv for dependency isolation. |

**Deprecated/outdated:**
- `websockets.legacy` module: Deprecated in v14.0, removal by 2030. Use `websockets.asyncio`.
- FastMCP 1.x: Merged into official SDK. Standalone FastMCP is now 2.x+.
- SSE transport: Deprecated in MCP spec. Use HTTP Streamable or stdio.

## Open Questions

1. **Auto-launch subprocess on Windows**
   - What we know: MCP server needs to run `qtpilot-launch.exe target.exe` as a subprocess when `--target` is provided.
   - What's unclear: Exact path resolution for qtpilot-launch.exe -- should it be on PATH, or in a known location relative to the Python package?
   - Recommendation: Accept `--launcher-path` CLI arg with `QTPILOT_LAUNCHER` env var fallback. Default to looking for `qtpilot-launch` on PATH.

2. **Push notifications (signal emissions)**
   - What we know: The probe sends JSON-RPC notifications for subscribed signals. These arrive on the WebSocket without a request ID.
   - What's unclear: How to surface these to Claude. MCP tools are request/response. Notifications don't fit the tool model.
   - Recommendation: For now, ignore push notifications in the MCP server (they flow to nowhere). Could add a `qt_signals_poll` tool that collects buffered notifications. Or expose via MCP resource that Claude can poll.

3. **Error mapping from probe to MCP**
   - What we know: Probe uses JSON-RPC error codes (-32001 to -32093). MCP tools can raise `ToolError`.
   - What's unclear: Whether to pass through probe error codes or simplify to generic messages.
   - Recommendation: Pass through the probe error message as the ToolError message. Include the error code in a structured field for debugging. Don't lose information.

4. **Testing without a running Qt app**
   - What we know: Tools are thin bridges over WebSocket. Unit testing can mock the WebSocket.
   - What's unclear: Integration testing strategy -- do we need a test Qt app running?
   - Recommendation: Unit test with mock WebSocket (verify correct JSON-RPC messages are sent). Integration testing can be a future phase or manual step using the existing test app from Phase 1.

## Sources

### Primary (HIGH confidence)
- [FastMCP documentation](https://gofastmcp.com/) - Tools, resources, lifespan, deployment
- [websockets 16.0 documentation](https://websockets.readthedocs.io/) - asyncio client API
- [Claude Code MCP docs](https://code.claude.com/docs/en/mcp) - Claude Code MCP config format, scopes
- [FastMCP PyPI](https://pypi.org/project/fastmcp/) - Current version, installation
- Existing probe source code (native_mode_api.cpp, computer_use_mode_api.cpp, chrome_mode_api.cpp) - JSON-RPC method signatures and parameters

### Secondary (MEDIUM confidence)
- [Claude Desktop MCP config](https://support.claude.com/en/articles/10949351) - Config file location and format
- [FastMCP Claude Desktop integration](https://gofastmcp.com/integrations/claude-desktop) - uv-based config patterns
- [MCP specification](https://modelcontextprotocol.io/) - Protocol details

### Tertiary (LOW confidence)
- None -- all findings verified with primary or secondary sources.

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH - FastMCP 2.x and websockets 16.x are well-documented, widely used, verified via official docs
- Architecture: HIGH - Lifespan pattern and thin bridge tools verified in FastMCP docs; probe method signatures verified from source code
- Pitfalls: HIGH - stdio logging and Windows cmd wrapper are documented MCP ecosystem issues; event loop blocking is standard asyncio pitfall

**Research date:** 2026-02-01
**Valid until:** 2026-03-01 (stable libraries, 30-day validity)
