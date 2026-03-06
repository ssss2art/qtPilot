# Spec: QtMCP Message Logger

## Context

When debugging or auditing QtMCP interactions, there's no way to see the full conversation between Claude and the probe. The existing `EventRecorder` captures probe-side signals/events, but not the request/response traffic. A structured message log showing MCP tool calls, JSON-RPC wire traffic, and probe notifications — written to a file — would enable post-session analysis and performance auditing.

## Design Summary

Create a `MessageLogger` class that captures the full end-to-end trace at three verbosity levels, written as JSON Lines to a configurable file path. Expose via `qtmcp_log_start/stop/status` MCP tools (mirroring the existing recording tools pattern). Intercept MCP-level tool calls via FastMCP middleware and JSON-RPC traffic via observer hooks on `ProbeConnection`.

### Verbosity Levels

| Level | Name | What's logged |
|-------|------|---------------|
| 1 | minimal | `mcp_in`, `mcp_out` — tool calls and results only |
| 2 | normal (default) | Level 1 + `req`, `res`, `err` — JSON-RPC wire traffic |
| 3 | verbose | Level 2 + `ntf` — probe notifications (signals, lifecycle, events) |

### Log Entry Format (JSON Lines)

```jsonl
{"ts":"2026-03-06T14:23:01.234Z","dir":"mcp_in","tool":"qt_objects_tree","args":{"maxDepth":3}}
{"ts":"2026-03-06T14:23:01.235Z","dir":"req","id":7,"method":"qt.objects.tree","params":{"maxDepth":3}}
{"ts":"2026-03-06T14:23:01.312Z","dir":"res","id":7,"method":"qt.objects.tree","dur_ms":77.2,"result":{...}}
{"ts":"2026-03-06T14:23:01.313Z","dir":"mcp_out","tool":"qt_objects_tree","dur_ms":79.1,"ok":true}
{"ts":"2026-03-06T14:23:06.500Z","dir":"ntf","method":"qtmcp.signalEmitted","params":{"objectId":"btn","signal":"clicked"}}
```

### Tool Interface

```
qtmcp_log_start(path?, level?)  -> {"logging": true, "path": "...", "level": 2}
qtmcp_log_stop()                -> {"logging": false, "path": "...", "entries": 142, "duration": 45.3}
qtmcp_log_status()              -> {"logging": true/false, "path": "...", "entries": 42, "level": 2, "duration": 120.5}
```

Default path: `qtmcp-log-YYYYMMDD-HHMMSS.jsonl` in the current working directory.

---

## Implementation Steps

### Step 1: Evolve `ProbeConnection` to support multiple handlers and observers

**File:** `python/src/qtmcp/connection.py`

Add `import time` at top.

Replace `_notification_handler` (single) with `_notification_handlers` (list). Add `_call_observers` list.

New methods:
- `add_notification_handler(handler)` / `remove_notification_handler(handler)`
- `add_call_observer(observer)` / `remove_call_observer(observer)`
- Keep `on_notification(handler)` as backward-compat shim (clears list, sets one)

In `_recv_loop`: iterate `_notification_handlers` list instead of calling single handler.

In `call()`: record `t0 = time.monotonic()` before send, compute `duration_ms` after future resolves, call `_notify_observers(request, result_or_exc, duration_ms)`. New `_notify_observers` helper iterates `_call_observers` safely.

Observer signature: `(request: dict, result_or_exc: dict | Exception, duration_ms: float) -> None`

### Step 2: Migrate `EventRecorder` to multi-handler API

**File:** `python/src/qtmcp/event_recorder.py`

Two-line change:
- `start()`: replace `probe.on_notification(self._handle_notification)` with `probe.add_notification_handler(self._handle_notification)`
- `stop()`: replace `probe.on_notification(None)` with `probe.remove_notification_handler(self._handle_notification)`

### Step 3: Create `MessageLogger` class

**New file:** `python/src/qtmcp/message_logger.py`

```python
class MessageLogger:
    """Logs MCP and JSON-RPC messages to a JSON Lines file."""

    def __init__(self):
        self._active: bool = False
        self._file: TextIO | None = None
        self._path: str | None = None
        self._start_time: float = 0.0
        self._entry_count: int = 0
        self._level: int = 2  # 1=minimal, 2=normal, 3=verbose
        self._pending_methods: dict[int, str] = {}  # request_id -> method name

    # Public API
    @property
    def is_active(self) -> bool
    @property
    def level(self) -> int
    def status(self) -> dict
    def start(self, path: str | None = None, level: int = 2) -> dict
    def stop(self) -> dict
    def attach(self, probe: ProbeConnection) -> None   # registers observer + notification handler
    def detach(self, probe: ProbeConnection) -> None   # removes them

    # MCP-level (called by middleware)
    def log_mcp_in(self, tool_name: str, arguments: dict) -> None    # level >= 1
    def log_mcp_out(self, tool_name: str, result_summary: str, duration_ms: float, is_error: bool = False) -> None  # level >= 1

    # JSON-RPC level (called by call observer)
    def _on_call_complete(self, request: dict, result_or_exc, duration_ms: float) -> None  # level >= 2

    # Notification level (called by notification handler)
    def _on_notification(self, method: str, params: dict) -> None    # level >= 3

    # Internal
    def _write_entry(self, entry: dict) -> None  # adds "ts", writes JSON + newline, flushes
    @staticmethod
    def _truncate(value, max_len=4096)  # truncate large payloads, replace base64 images
```

Key behaviors:
- `start()` is synchronous — just opens a file, no probe required
- `stop()` is synchronous — closes file, returns summary
- `attach()`/`detach()` manage hooks on `ProbeConnection` (separate from start/stop since probe may reconnect)
- `_write_entry()` flushes after each write for crash safety
- Truncates results > 4096 chars; replaces base64 image data with placeholder
- Skip logging own tools (`qtmcp_log_*`) to avoid recursion

### Step 4: Create FastMCP logging middleware

**New file:** `python/src/qtmcp/logging_middleware.py`

```python
from fastmcp.server.middleware import Middleware, MiddlewareContext, CallNext

class LoggingMiddleware(Middleware):
    async def on_call_tool(self, context, call_next):
        from qtmcp.server import get_message_logger
        logger = get_message_logger()
        tool_name = context.message.name
        args = context.message.arguments or {}

        if logger.is_active and not tool_name.startswith("qtmcp_log_"):
            logger.log_mcp_in(tool_name, args)

        t0 = time.monotonic()
        try:
            result = await call_next(context)
            dur = (time.monotonic() - t0) * 1000
            if logger.is_active and not tool_name.startswith("qtmcp_log_"):
                summary = _summarize_tool_result(result)
                logger.log_mcp_out(tool_name, summary, dur)
            return result
        except Exception as exc:
            dur = (time.monotonic() - t0) * 1000
            if logger.is_active and not tool_name.startswith("qtmcp_log_"):
                logger.log_mcp_out(tool_name, str(exc), dur, is_error=True)
            raise
```

`_summarize_tool_result()` extracts text from `ToolResult.content` blocks, truncates large values, replaces image content with placeholders.

### Step 5: Create logging tools

**New file:** `python/src/qtmcp/tools/logging_tools.py`

Follow `recording_tools.py` pattern exactly. Three tools:

- `qtmcp_log_start(path?, level?)` — starts logging, attaches to current probe if connected
- `qtmcp_log_stop()` — detaches from probe, stops logging, returns summary
- `qtmcp_log_status()` — returns current state

### Step 6: Wire everything in `server.py`

**File:** `python/src/qtmcp/server.py`

Changes:
1. Import `MessageLogger`, add `_message_logger = MessageLogger()` and `get_message_logger()` accessor
2. Register middleware: `mcp.add_middleware(LoggingMiddleware())` after creating FastMCP instance
3. Register logging tools (always, like discovery tools): `register_logging_tools(mcp)`
4. In `connect_to_probe()`: attach logger if active, detach from old probe first
5. In `disconnect_probe()`: detach logger if active
6. In lifespan `finally`: stop logger if active

### Step 7: Tests

**New file:** `python/tests/test_message_logger.py`
- Test start/stop/status lifecycle
- Test each entry type writes correct JSON structure
- Test verbosity levels filter correctly
- Test truncation of large values
- Test attach/detach registers/removes hooks
- Test inactive logger writes nothing
- Use `mock_probe` fixture from conftest.py

**New file:** `python/tests/test_logging_tools.py`
- Test 3 tools registered (mirror `test_recording_tools.py` pattern)
- Use `_tool_names()` helper and `mock_mcp` fixture

---

## Files Modified

| File | Change |
|------|--------|
| `python/src/qtmcp/connection.py` | Add multi-handler notifications, call observers |
| `python/src/qtmcp/event_recorder.py` | 2-line migration to `add/remove_notification_handler` |
| `python/src/qtmcp/server.py` | Add `_message_logger` singleton, middleware, tool registration, attach/detach on connect/disconnect |
| `python/src/qtmcp/message_logger.py` | **NEW** — core `MessageLogger` class |
| `python/src/qtmcp/logging_middleware.py` | **NEW** — FastMCP middleware for MCP-level interception |
| `python/src/qtmcp/tools/logging_tools.py` | **NEW** — `qtmcp_log_start/stop/status` tools |
| `python/tests/test_message_logger.py` | **NEW** — unit tests for logger |
| `python/tests/test_logging_tools.py` | **NEW** — tool registration tests |

## Verification

1. **Unit tests:** Run `pytest python/tests/test_message_logger.py python/tests/test_logging_tools.py -v`
2. **Existing tests pass:** Run `pytest python/tests/ -v` to confirm no regressions (especially `test_event_recorder.py`)
3. **Integration test with live app:**
   - Launch test app: `build/bin/Release/qtmcp-launcher.exe build/bin/Release/qtmcp-test-app.exe`
   - Connect probe: `qtmcp_connect_probe(ws_url="ws://localhost:9222")`
   - Start logging: `qtmcp_log_start(level=3)`
   - Interact: `qt_ping()`, `qt_objects_tree(maxDepth=2)`, `qt_ui_click(objectId="...")`
   - Stop logging: `qtmcp_log_stop()` — check entry count and path
   - Read the log file and verify entries are properly formatted JSON Lines with correct `dir` values at each level
