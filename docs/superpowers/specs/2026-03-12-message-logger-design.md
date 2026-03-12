# Design: QtMCP Message Logger

## Context

When debugging or auditing QtMCP interactions, there's no way to see the full conversation between Claude and the probe. The existing `EventRecorder` captures probe-side signals/events, but not the request/response traffic. A structured message log showing MCP tool calls, JSON-RPC wire traffic, and probe notifications — written to a file and buffered in memory — enables post-session analysis, live debugging via tail, and performance auditing.

## Design Summary

Three layers of capture at three verbosity levels, wired through FastMCP middleware (MCP layer) and ProbeConnection observers/handlers (JSON-RPC and notification layers). Logs are written as JSON Lines to a file and kept in a configurable in-memory ring buffer accessible via `qtmcp_log_tail`.

Builds on top of the `ServerState` class from the dynamic mode switching work.

---

## 1. ProbeConnection Multi-Handler Evolution

**File:** `python/src/qtmcp/connection.py`

Replace `_notification_handler` (single callable) with two lists:

### Notification handlers — `_notification_handlers: list[Callable]`

- `add_notification_handler(handler)` / `remove_notification_handler(handler)`
- `on_notification(handler)` becomes a backward-compat shim (clears list, sets one)
- `_recv_loop` iterates the list, catching exceptions per-handler so one bad handler doesn't kill the others

### Call observers — `_call_observers: list[Callable]`

- `add_call_observer(observer)` / `remove_call_observer(observer)`
- Observer signature: `(request: dict, result_or_exc: dict | Exception, duration_ms: float) -> None`
- Modified `call()` method wraps `await future` in try/except to capture both success and error:

```python
async def call(self, method, params=None):
    request = {"jsonrpc": "2.0", "method": method, "params": params or {}, "id": self._next_id}
    # ... send request, create future ...
    t0 = time.monotonic()
    try:
        result = await future
    except Exception as exc:
        duration_ms = (time.monotonic() - t0) * 1000
        self._notify_call_observers(request, exc, duration_ms)
        raise
    else:
        duration_ms = (time.monotonic() - t0) * 1000
        self._notify_call_observers(request, result, duration_ms)
        return result
```

- `_notify_call_observers` iterates observers safely (catching per-observer exceptions)

### EventRecorder migration (required prerequisite)

**File:** `python/src/qtmcp/event_recorder.py`

Two-line change:
- `start()`: `probe.add_notification_handler(self._handle_notification)`
- `stop()`: `probe.remove_notification_handler(self._handle_notification)`

This migration is **required** before the MessageLogger can coexist with EventRecorder. The old `on_notification()` API clears the entire handler list, so any code still using it will silently remove other handlers.

The `on_notification()` shim remains for external backward compat but should emit a `DeprecationWarning` to discourage use within the project.

---

## 2. MessageLogger Class

**New file:** `python/src/qtmcp/message_logger.py`

### State

- `_active: bool` — whether logging is on
- `_file: TextIO | None` — open file handle (JSON Lines)
- `_path: str | None` — file path
- `_level: int` — verbosity (1/2/3)
- `_start_time: float` — monotonic clock at start
- `_entry_count: int` — total entries written
- `_buffer: deque[dict]` — ring buffer of recent entries, `maxlen` configurable (default 200)
- `_attached_probe: ProbeConnection | None` — the probe we're currently attached to (for safe detach)

### Public API

- `start(path=None, level=2, buffer_size=200) -> dict` — opens file, resets counters, creates deque
- `stop() -> dict` — closes file, returns summary (path, entry count, duration)
- `status() -> dict` — current state snapshot
- `attach(probe) / detach(probe)` — registers/removes call observer + notification handler on the probe
- `tail(count=50) -> list[dict]` — returns last N entries from ring buffer

### Internal hooks (called by middleware and observers)

- `log_mcp_in(tool_name, arguments)` — level >= 1
- `log_mcp_out(tool_name, result_summary, duration_ms, is_error)` — level >= 1
- `_on_call_complete(request, result_or_exc, duration_ms)` — level >= 2, registered as call observer
- `_on_notification(method, params)` — level >= 3, registered as notification handler

### Internal

- `_write_entry(entry)` — adds ISO timestamp, writes JSON + newline to file, flushes, appends to ring buffer, increments counter
- `_truncate(value, max_len=4096)` — truncates large payloads, replaces base64 image data with `"<image:NNNb>"`
- Skips logging its own tools (`qtmcp_log_*`) to avoid recursion

### Key behaviors

- `start()`/`stop()` are synchronous — just file management
- If `start()` is called while already active, it calls `stop()` first (closing the current file), then begins a new session
- `attach()`/`detach()` are separate from start/stop because the probe may reconnect mid-session
- `attach()` can be called regardless of active state — handlers are registered but produce no output until `start()` is called
- `detach(probe)` is safe to call even if `attach(probe)` was never called (remove operations are no-ops for unregistered handlers)
- File flushes after each write for crash safety
- Ring buffer always populated when active, regardless of whether a file is open
- Ring buffer persists after `stop()` until next `start()` — `tail()` works after stopping
- Thread safety is not required — all access is from the same async event loop

---

## 3. FastMCP Logging Middleware

**New file:** `python/src/qtmcp/logging_middleware.py`

```python
from fastmcp.server.middleware import Middleware

class LoggingMiddleware(Middleware):
    async def on_call_tool(self, context, call_next):
        # context is MiddlewareContext[CallToolRequestParams]
        tool_name = context.message.name
        args = context.message.arguments or {}
        # ... intercept, time, forward via call_next(context) ...
        # result is a ToolResult with .content (list of ContentBlock)
```

### Behavior

- Grabs the `MessageLogger` via `from qtmcp.server import get_message_logger`
- Extracts tool name via `context.message.name` and arguments via `context.message.arguments`
- Skips `qtmcp_log_*` tools to avoid recursion
- Calls `logger.log_mcp_in(tool_name, args)` before forwarding
- Times the call with `time.monotonic()`
- Calls `logger.log_mcp_out(tool_name, summary, duration_ms)` after — or with `is_error=True` on exception
- `_summarize_tool_result()` helper extracts text from `ToolResult.content` blocks (each has a `.type` and content attributes), truncates large values, replaces image content with placeholders

---

## 4. Logging Tools

**New file:** `python/src/qtmcp/tools/logging_tools.py`

Follows the `recording_tools.py` pattern. Four tools, always registered (mode-agnostic):

| Tool | Args | Returns |
|------|------|---------|
| `qtmcp_log_start` | `path?`, `level?`, `buffer_size?` | `{"logging": true, "path": "...", "level": 2, "buffer_size": 200}` |
| `qtmcp_log_stop` | — | `{"logging": false, "path": "...", "entries": 142, "duration": 45.3}` |
| `qtmcp_log_status` | — | Current state: active, path, level, entry count, duration, buffer size |
| `qtmcp_log_tail` | `count?` (default 50) | `{"entries": [...], "count": N, "total_logged": M}` |

Default path: `qtmcp-log-YYYYMMDD-HHMMSS.jsonl` in the current working directory.

`qtmcp_log_tail` works after `stop()` — buffer persists until next `start()`.

---

## 5. Server Wiring

**File:** `python/src/qtmcp/server.py`

### ServerState changes

- Add `self.message_logger = MessageLogger()` to `ServerState.__init__`

### New accessor

- `get_message_logger() -> MessageLogger` — thin wrapper like `get_recorder()`

### create_server() changes

- Register middleware: `mcp.add_middleware(LoggingMiddleware())`
- Register tools: `register_logging_tools(mcp)` alongside discovery and recording tools

### Probe lifecycle hooks

- `connect_to_probe()`: if logger is active, detach from old probe, then `logger.attach(new_probe)`
- `disconnect_probe()`: if logger is active, `logger.detach(probe)`
- Lifespan `finally`: if logger is active, `logger.stop()`

Logger survives probe reconnections — detaches from old probe, reattaches to new one, continues writing to the same file.

---

## 6. Log Entry Format

JSON Lines — each line is a self-contained JSON object:

```jsonl
{"ts":"2026-03-06T14:23:01.234Z","dir":"mcp_in","tool":"qt_objects_tree","args":{"maxDepth":3}}
{"ts":"2026-03-06T14:23:01.235Z","dir":"req","id":7,"method":"qt.objects.tree","params":{"maxDepth":3}}
{"ts":"2026-03-06T14:23:01.312Z","dir":"res","id":7,"method":"qt.objects.tree","dur_ms":77.2,"result":{...}}
{"ts":"2026-03-06T14:23:01.313Z","dir":"mcp_out","tool":"qt_objects_tree","dur_ms":79.1,"ok":true}
{"ts":"2026-03-06T14:23:02.100Z","dir":"req","id":8,"method":"qt.properties.get","params":{"objectId":"gone"}}
{"ts":"2026-03-06T14:23:02.150Z","dir":"err","id":8,"method":"qt.properties.get","dur_ms":50.0,"error":"Object not found: gone"}
{"ts":"2026-03-06T14:23:02.151Z","dir":"mcp_out","tool":"qt_properties_get","dur_ms":52.3,"ok":false,"error":"Object not found: gone"}
{"ts":"2026-03-06T14:23:06.500Z","dir":"ntf","method":"qtmcp.signalEmitted","params":{"objectId":"btn","signal":"clicked"}}
```

### Error entry semantics

- **`err`** (level 2+): JSON-RPC error from the probe — produced by the call observer when `result_or_exc` is an Exception. Contains the error message.
- **`mcp_out` with `"ok": false`** (level 1+): MCP-layer error — produced by the middleware when the tool raises. These often correspond to an `err` entry at level 2+, giving both perspectives.

### `dir` field values by level

| Level | `dir` values logged |
|-------|-------------------|
| 1 (minimal) | `mcp_in`, `mcp_out` |
| 2 (normal) | + `req`, `res`, `err` |
| 3 (verbose) | + `ntf` |

### Truncation rules

- String values > 4096 chars truncated with `"...<truncated NNNc>"`
- Base64 image data (detected by content pattern) replaced with `"<image:NNNb>"`
- Applied to `args`, `params`, and `result` fields before writing
- Ring buffer entries are also truncated (same data as file)
- Timestamps: ISO 8601 UTC with millisecond precision

---

## 7. Testing Strategy

### `python/tests/test_message_logger.py`

- Start/stop/status lifecycle (active flag, file creation, summary on stop)
- Each entry type (`mcp_in`, `mcp_out`, `req`, `res`, `err`, `ntf`) writes correct JSON structure
- Verbosity filtering: level 1 skips `req`/`res`/`ntf`, level 2 skips `ntf`, level 3 captures all
- Ring buffer: entries accumulate, `tail()` returns correct count, buffer respects `maxlen`
- `tail()` survives `stop()` — buffer persists until next `start()`
- Truncation of large string values and base64 image replacement
- Attach/detach registers/removes handlers on mock probe
- Inactive logger writes nothing
- Uses `mock_probe` fixture from `conftest.py`

### `python/tests/test_logging_middleware.py`

- Middleware calls `log_mcp_in`/`log_mcp_out` for normal tool calls
- Skips `qtmcp_log_*` tools (no recursion)
- Records `is_error=True` on exceptions
- Timing is captured (duration_ms > 0)

### `python/tests/test_logging_tools.py`

- Four tools registered (`qtmcp_log_start`, `qtmcp_log_stop`, `qtmcp_log_status`, `qtmcp_log_tail`)
- Follows `test_recording_tools.py` pattern with `_tool_names()` helper and `mock_mcp` fixture

### `python/tests/test_connection_multi_handler.py`

- Multiple notification handlers receive the same notification
- `add`/`remove` works correctly
- One handler crashing doesn't kill others
- Call observers fire with correct request/result/duration
- Backward compat: `on_notification()` still works (clears list, sets one)
- Backward compat destruction: adding handlers via `add_notification_handler`, then calling `on_notification(new_handler)`, results in only the new handler receiving notifications (verifies the list-clearing behavior)
- `on_notification()` emits `DeprecationWarning`

---

## Files Summary

| File | Change |
|------|--------|
| `python/src/qtmcp/connection.py` | Multi-handler notifications, call observers |
| `python/src/qtmcp/event_recorder.py` | 2-line migration to `add/remove_notification_handler` |
| `python/src/qtmcp/server.py` | Add `message_logger` to `ServerState`, middleware, tool registration, attach/detach on connect/disconnect |
| `python/src/qtmcp/message_logger.py` | **NEW** — core `MessageLogger` class |
| `python/src/qtmcp/logging_middleware.py` | **NEW** — FastMCP middleware for MCP-level interception |
| `python/src/qtmcp/tools/logging_tools.py` | **NEW** — `qtmcp_log_start/stop/status/tail` tools |
| `python/tests/test_message_logger.py` | **NEW** — unit tests for logger |
| `python/tests/test_logging_middleware.py` | **NEW** — middleware tests |
| `python/tests/test_logging_tools.py` | **NEW** — tool registration tests |
| `python/tests/test_connection_multi_handler.py` | **NEW** — ProbeConnection evolution tests |

## Verification

1. **Unit tests:** `pytest python/tests/test_message_logger.py python/tests/test_logging_middleware.py python/tests/test_logging_tools.py python/tests/test_connection_multi_handler.py -v`
2. **Existing tests pass:** `pytest python/tests/ -v` — especially `test_event_recorder.py`
3. **Integration test with live app:**
   - Launch: `build/bin/Release/qtmcp-launcher.exe build/bin/Release/qtmcp-test-app.exe`
   - Connect: `qtmcp_connect_probe(ws_url="ws://localhost:9222")`
   - Start: `qtmcp_log_start(level=3)`
   - Interact: `qt_ping()`, `qt_objects_tree(maxDepth=2)`, `qt_ui_click(objectId="...")`
   - Tail: `qtmcp_log_tail(count=10)` — verify recent entries in memory
   - Stop: `qtmcp_log_stop()` — check entry count and path
   - Tail after stop: `qtmcp_log_tail()` — verify buffer persists
   - Read log file — verify JSON Lines format with correct `dir` values at each level
