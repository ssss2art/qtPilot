# Message Logger Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a message logging system that captures MCP tool calls, JSON-RPC wire traffic, and probe notifications at three verbosity levels, written to JSON Lines files with an in-memory ring buffer for live tail access.

**Architecture:** Three capture layers — FastMCP middleware for MCP-level interception, call observers on ProbeConnection for JSON-RPC wire traffic, and notification handlers for probe events. A `MessageLogger` class owns all state (file I/O, ring buffer, counters). Four MCP tools expose start/stop/status/tail. Builds on top of `ServerState` from the dynamic mode switching work.

**Tech Stack:** Python 3.11+, FastMCP 2.14.x (middleware API), websockets, pytest + pytest-asyncio

**Spec:** `docs/superpowers/specs/2026-03-12-message-logger-design.md`

---

## Chunk 1: ProbeConnection Multi-Handler Evolution

### Task 1: Multi-handler notification tests

**Files:**
- Create: `python/tests/test_connection_multi_handler.py`

- [ ] **Step 1: Write multi-handler notification tests**

Create `python/tests/test_connection_multi_handler.py`:

```python
"""Unit tests for ProbeConnection multi-handler and call observer support."""

from __future__ import annotations

import asyncio

import pytest

pytestmark = pytest.mark.asyncio


class TestMultiNotificationHandlers:
    async def test_multiple_handlers_receive_notification(self, mock_probe):
        """All registered handlers receive the same notification."""
        probe, mock_ws = mock_probe
        received_a = []
        received_b = []

        probe.add_notification_handler(lambda m, p: received_a.append(m))
        probe.add_notification_handler(lambda m, p: received_b.append(m))

        await mock_ws.inject_notification({
            "jsonrpc": "2.0",
            "method": "qtpilot.signalEmitted",
            "params": {"objectId": "btn", "signal": "clicked"},
        })
        await asyncio.sleep(0.1)

        assert len(received_a) == 1
        assert len(received_b) == 1
        assert received_a[0] == "qtpilot.signalEmitted"

    async def test_remove_handler(self, mock_probe):
        """Removed handler stops receiving notifications."""
        probe, mock_ws = mock_probe
        received = []

        handler = lambda m, p: received.append(m)
        probe.add_notification_handler(handler)

        await mock_ws.inject_notification({
            "jsonrpc": "2.0",
            "method": "qtpilot.signalEmitted",
            "params": {},
        })
        await asyncio.sleep(0.1)
        assert len(received) == 1

        probe.remove_notification_handler(handler)

        await mock_ws.inject_notification({
            "jsonrpc": "2.0",
            "method": "qtpilot.signalEmitted",
            "params": {},
        })
        await asyncio.sleep(0.1)
        assert len(received) == 1  # no new

    async def test_remove_unregistered_handler_is_noop(self, mock_probe):
        """Removing a handler that was never added does not raise."""
        probe, mock_ws = mock_probe
        probe.remove_notification_handler(lambda m, p: None)  # no error

    async def test_crashing_handler_does_not_kill_others(self, mock_probe):
        """One handler raising does not prevent other handlers from running."""
        probe, mock_ws = mock_probe
        received = []

        def bad_handler(m, p):
            raise RuntimeError("boom")

        probe.add_notification_handler(bad_handler)
        probe.add_notification_handler(lambda m, p: received.append(m))

        await mock_ws.inject_notification({
            "jsonrpc": "2.0",
            "method": "qtpilot.signalEmitted",
            "params": {},
        })
        await asyncio.sleep(0.1)

        assert len(received) == 1  # second handler still ran

    async def test_on_notification_backward_compat(self, mock_probe):
        """Legacy on_notification() still works — clears list, sets one handler."""
        probe, mock_ws = mock_probe
        received = []

        with pytest.warns(DeprecationWarning):
            probe.on_notification(lambda m, p: received.append(m))

        await mock_ws.inject_notification({
            "jsonrpc": "2.0",
            "method": "qtpilot.signalEmitted",
            "params": {},
        })
        await asyncio.sleep(0.1)
        assert len(received) == 1

    async def test_on_notification_clears_add_handlers(self, mock_probe):
        """Calling on_notification() destroys handlers added via add_notification_handler."""
        probe, mock_ws = mock_probe
        received_add = []
        received_on = []

        probe.add_notification_handler(lambda m, p: received_add.append(m))
        probe.on_notification(lambda m, p: received_on.append(m))

        await mock_ws.inject_notification({
            "jsonrpc": "2.0",
            "method": "qtpilot.signalEmitted",
            "params": {},
        })
        await asyncio.sleep(0.1)

        assert len(received_add) == 0  # cleared by on_notification
        assert len(received_on) == 1

    async def test_on_notification_emits_deprecation_warning(self, mock_probe):
        """on_notification() emits a DeprecationWarning."""
        probe, mock_ws = mock_probe
        with pytest.warns(DeprecationWarning, match="add_notification_handler"):
            probe.on_notification(lambda m, p: None)

    async def test_on_notification_none_clears_all(self, mock_probe):
        """on_notification(None) clears the handler list."""
        probe, mock_ws = mock_probe
        received = []

        probe.add_notification_handler(lambda m, p: received.append(m))
        with pytest.warns(DeprecationWarning):
            probe.on_notification(None)

        await mock_ws.inject_notification({
            "jsonrpc": "2.0",
            "method": "qtpilot.signalEmitted",
            "params": {},
        })
        await asyncio.sleep(0.1)
        assert len(received) == 0
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `pytest python/tests/test_connection_multi_handler.py -v`
Expected: FAIL — `ProbeConnection` has no `add_notification_handler` method

- [ ] **Step 3: Implement multi-handler notifications in ProbeConnection**

In `python/src/qtpilot/connection.py`:

1. Add `import warnings` at top (line 2 area).

2. In `__init__` (line 40-47), replace `self._notification_handler` with:
```python
        self._notification_handlers: list[Callable[[str, dict], None]] = []
```

3. Add new methods after `on_notification` (after line 66):
```python
    def add_notification_handler(self, handler: Callable[[str, dict], None]) -> None:
        """Add a notification handler. Multiple handlers can coexist."""
        self._notification_handlers.append(handler)

    def remove_notification_handler(self, handler: Callable[[str, dict], None]) -> None:
        """Remove a previously added notification handler. No-op if not found."""
        try:
            self._notification_handlers.remove(handler)
        except ValueError:
            pass
```

4. Change `on_notification` (lines 59-66) to a backward-compat shim:
```python
    def on_notification(self, handler: Callable[[str, dict], None] | None) -> None:
        """Register or unregister a callback for JSON-RPC notifications.

        .. deprecated:: Use add_notification_handler / remove_notification_handler.
            This method clears ALL existing handlers before setting the new one.
        """
        warnings.warn(
            "on_notification() is deprecated. Use add_notification_handler() / "
            "remove_notification_handler() instead.",
            DeprecationWarning,
            stacklevel=2,
        )
        self._notification_handlers.clear()
        if handler is not None:
            self._notification_handlers.append(handler)
```

5. In `_recv_loop` (lines 148-158), replace the single-handler dispatch:
```python
                    # JSON-RPC notification (no id, has method)
                    method = msg.get("method")
                    if method and self._notification_handlers:
                        for handler in list(self._notification_handlers):
                            try:
                                handler(method, msg.get("params", {}))
                            except Exception:
                                logger.debug(
                                    "Notification handler error", exc_info=True
                                )
                    elif not method:
                        logger.debug("Ignoring message with id=%s", msg_id)
                    continue
```

- [ ] **Step 4: Run multi-handler tests to verify they pass**

Run: `pytest python/tests/test_connection_multi_handler.py -v`
Expected: All 8 tests PASS

- [ ] **Step 5: Commit**

```bash
git add python/tests/test_connection_multi_handler.py python/src/qtpilot/connection.py
git commit -m "feat: evolve ProbeConnection to support multiple notification handlers"
```

---

### Task 2: Call observer and send observer tests and implementation

**Files:**
- Modify: `python/tests/test_connection_multi_handler.py`
- Modify: `python/src/qtpilot/connection.py`

- [ ] **Step 1: Write call observer and send observer tests**

Append to `python/tests/test_connection_multi_handler.py`:

```python
class TestCallObservers:
    async def test_observer_receives_successful_call(self, mock_probe):
        """Call observer fires with request, result, and duration on success."""
        probe, mock_ws = mock_probe
        observed = []

        probe.add_call_observer(lambda req, res, dur: observed.append((req, res, dur)))

        mock_ws.responses[probe._next_id] = {
            "jsonrpc": "2.0",
            "result": {"pong": True},
            "id": probe._next_id,
        }
        result = await probe.call("qt.ping")

        assert result == {"pong": True}
        assert len(observed) == 1
        req, res, dur = observed[0]
        assert req["method"] == "qt.ping"
        assert res == {"pong": True}
        assert dur > 0

    async def test_observer_receives_error_call(self, mock_probe):
        """Call observer fires with the exception on probe error."""
        probe, mock_ws = mock_probe
        observed = []

        probe.add_call_observer(lambda req, res, dur: observed.append((req, res, dur)))

        mock_ws.responses[probe._next_id] = {
            "jsonrpc": "2.0",
            "error": {"code": -1, "message": "Not found"},
            "id": probe._next_id,
        }
        with pytest.raises(Exception, match="Not found"):
            await probe.call("qt.objects.info", {"objectId": "gone"})

        assert len(observed) == 1
        req, res_or_exc, dur = observed[0]
        assert req["method"] == "qt.objects.info"
        assert isinstance(res_or_exc, Exception)
        assert dur > 0

    async def test_remove_call_observer(self, mock_probe):
        """Removed observer stops being called."""
        probe, mock_ws = mock_probe
        observed = []

        observer = lambda req, res, dur: observed.append(1)
        probe.add_call_observer(observer)
        probe.remove_call_observer(observer)

        mock_ws.responses[probe._next_id] = {
            "jsonrpc": "2.0",
            "result": {},
            "id": probe._next_id,
        }
        await probe.call("qt.ping")

        assert len(observed) == 0

    async def test_crashing_observer_does_not_break_call(self, mock_probe):
        """A crashing observer does not prevent the call from returning."""
        probe, mock_ws = mock_probe
        received = []

        def bad_observer(req, res, dur):
            raise RuntimeError("observer crash")

        probe.add_call_observer(bad_observer)
        probe.add_call_observer(lambda req, res, dur: received.append(1))

        mock_ws.responses[probe._next_id] = {
            "jsonrpc": "2.0",
            "result": {"ok": True},
            "id": probe._next_id,
        }
        result = await probe.call("qt.ping")

        assert result == {"ok": True}
        assert len(received) == 1  # second observer still ran


class TestSendObservers:
    async def test_send_observer_fires_on_call(self, mock_probe):
        """Send observer fires with the request dict when a call is sent."""
        probe, mock_ws = mock_probe
        sent = []

        probe.add_send_observer(lambda req: sent.append(req))

        mock_ws.responses[probe._next_id] = {
            "jsonrpc": "2.0",
            "result": {"pong": True},
            "id": probe._next_id,
        }
        await probe.call("qt.ping")

        assert len(sent) == 1
        assert sent[0]["method"] == "qt.ping"
        assert "id" in sent[0]

    async def test_remove_send_observer(self, mock_probe):
        """Removed send observer stops being called."""
        probe, mock_ws = mock_probe
        sent = []

        observer = lambda req: sent.append(1)
        probe.add_send_observer(observer)
        probe.remove_send_observer(observer)

        mock_ws.responses[probe._next_id] = {
            "jsonrpc": "2.0",
            "result": {},
            "id": probe._next_id,
        }
        await probe.call("qt.ping")

        assert len(sent) == 0

    async def test_crashing_send_observer_does_not_break_call(self, mock_probe):
        """A crashing send observer does not prevent the call from completing."""
        probe, mock_ws = mock_probe

        def bad_observer(req):
            raise RuntimeError("send observer crash")

        probe.add_send_observer(bad_observer)

        mock_ws.responses[probe._next_id] = {
            "jsonrpc": "2.0",
            "result": {"ok": True},
            "id": probe._next_id,
        }
        result = await probe.call("qt.ping")
        assert result == {"ok": True}
```

- [ ] **Step 2: Run tests to verify new tests fail**

Run: `pytest python/tests/test_connection_multi_handler.py::TestCallObservers python/tests/test_connection_multi_handler.py::TestSendObservers -v`
Expected: FAIL — `ProbeConnection` has no `add_call_observer` / `add_send_observer` method

- [ ] **Step 3: Implement call observers and send observers in ProbeConnection**

In `python/src/qtpilot/connection.py`:

1. Add `import time` at top.

2. In `__init__`, add after `_notification_handlers`:
```python
        self._call_observers: list[Callable] = []
        self._send_observers: list[Callable] = []
```

3. Add methods after `remove_notification_handler`:
```python
    def add_call_observer(self, observer: Callable) -> None:
        """Add a call observer. Called with (request, result_or_exc, duration_ms) after completion."""
        self._call_observers.append(observer)

    def remove_call_observer(self, observer: Callable) -> None:
        """Remove a call observer. No-op if not found."""
        try:
            self._call_observers.remove(observer)
        except ValueError:
            pass

    def add_send_observer(self, observer: Callable) -> None:
        """Add a send observer. Called with (request) when a request is sent."""
        self._send_observers.append(observer)

    def remove_send_observer(self, observer: Callable) -> None:
        """Remove a send observer. No-op if not found."""
        try:
            self._send_observers.remove(observer)
        except ValueError:
            pass

    def _notify_call_observers(
        self, request: dict, result_or_exc: dict | Exception, duration_ms: float
    ) -> None:
        """Notify all call observers safely."""
        for observer in list(self._call_observers):
            try:
                observer(request, result_or_exc, duration_ms)
            except Exception:
                logger.debug("Call observer error", exc_info=True)

    def _notify_send_observers(self, request: dict) -> None:
        """Notify all send observers safely."""
        for observer in list(self._send_observers):
            try:
                observer(request)
            except Exception:
                logger.debug("Send observer error", exc_info=True)
```

4. In `call()`, change the try block (lines 129-135) to notify send observers, capture timing, and notify call observers. Note: `t0` is initialized *before* `send()` so it's always bound even if `send()` raises:
```python
        t0 = time.monotonic()
        try:
            await self._ws.send(json.dumps(request))
            logger.debug("Sent request id=%d method=%s", request_id, method)
            self._notify_send_observers(request)
            result = await future
        except asyncio.CancelledError:
            self._pending.pop(request_id, None)
            raise
        except Exception as exc:
            duration_ms = (time.monotonic() - t0) * 1000
            self._notify_call_observers(request, exc, duration_ms)
            raise
        else:
            duration_ms = (time.monotonic() - t0) * 1000
            self._notify_call_observers(request, result, duration_ms)
            return result
```

- [ ] **Step 4: Run all multi-handler tests**

Run: `pytest python/tests/test_connection_multi_handler.py -v`
Expected: All 15 tests PASS

- [ ] **Step 5: Commit**

```bash
git add python/tests/test_connection_multi_handler.py python/src/qtpilot/connection.py
git commit -m "feat: add call observers and send observers to ProbeConnection"
```

---

### Task 3: Migrate EventRecorder and update existing tests

**Files:**
- Modify: `python/src/qtpilot/event_recorder.py:167,212`
- Modify: `python/tests/test_event_recorder.py:36,55,86,109,119`

- [ ] **Step 1: Migrate EventRecorder to multi-handler API**

In `python/src/qtpilot/event_recorder.py`:

Line 167 — change:
```python
        probe.on_notification(self._handle_notification)
```
to:
```python
        probe.add_notification_handler(self._handle_notification)
```

Line 212 — change:
```python
        probe.on_notification(None)
```
to:
```python
        probe.remove_notification_handler(self._handle_notification)
```

- [ ] **Step 2: Update TestNotificationRouting tests to use new API**

In `python/tests/test_event_recorder.py`, the `TestNotificationRouting` class calls `probe.on_notification()` which now emits `DeprecationWarning`. Update these tests to use `add_notification_handler` / `remove_notification_handler`:

Line 36 — change:
```python
        probe.on_notification(lambda method, params: received.append((method, params)))
```
to:
```python
        probe.add_notification_handler(lambda method, params: received.append((method, params)))
```

Line 55 — same change:
```python
        probe.add_notification_handler(lambda method, params: received.append((method, params)))
```

Line 86 — change:
```python
        probe.on_notification(bad_handler)
```
to:
```python
        probe.add_notification_handler(bad_handler)
```

Line 109 — change:
```python
        probe.on_notification(lambda m, p: received.append(m))
```
to:
```python
        probe.add_notification_handler(lambda m, p: received.append(m))
```

Line 119 — change:
```python
        probe.on_notification(None)
```
to:
```python
        probe.remove_notification_handler(received_handler)
```

Note: For line 109/119, you'll need to capture the handler in a variable so you can remove it:
```python
        received_handler = lambda m, p: received.append(m)
        probe.add_notification_handler(received_handler)
```
Then on line 119:
```python
        probe.remove_notification_handler(received_handler)
```

- [ ] **Step 3: Run existing EventRecorder tests to verify no regressions**

Run: `pytest python/tests/test_event_recorder.py -v`
Expected: All existing tests PASS

- [ ] **Step 4: Run full test suite**

Run: `pytest python/tests/ -v`
Expected: All tests PASS (multi-handler tests + existing tests)

- [ ] **Step 5: Commit**

```bash
git add python/src/qtpilot/event_recorder.py python/tests/test_event_recorder.py
git commit -m "refactor: migrate EventRecorder and tests to multi-handler notification API"
```

---

## Chunk 2: MessageLogger Class

### Task 4: MessageLogger core — start/stop/status lifecycle tests

**Files:**
- Create: `python/src/qtpilot/message_logger.py`
- Create: `python/tests/test_message_logger.py`

- [ ] **Step 1: Write lifecycle tests**

Create `python/tests/test_message_logger.py`:

```python
"""Unit tests for MessageLogger."""

from __future__ import annotations

import json
import os
import tempfile

import pytest

from qtpilot.message_logger import MessageLogger

pytestmark = pytest.mark.asyncio


class TestMessageLoggerLifecycle:
    def test_initial_state(self):
        """Logger starts inactive."""
        logger = MessageLogger()
        assert logger.is_active is False
        status = logger.status()
        assert status["logging"] is False

    def test_start_creates_file(self):
        """start() creates a log file and returns status."""
        logger = MessageLogger()
        with tempfile.TemporaryDirectory() as tmpdir:
            path = os.path.join(tmpdir, "test.jsonl")
            result = logger.start(path=path)
            assert result["logging"] is True
            assert result["path"] == path
            assert result["level"] == 2
            assert result["buffer_size"] == 200
            assert logger.is_active is True
            assert os.path.exists(path)
            logger.stop()

    def test_start_default_path(self):
        """start() with no path creates a timestamped file in cwd."""
        logger = MessageLogger()
        with tempfile.TemporaryDirectory() as tmpdir:
            orig = os.getcwd()
            try:
                os.chdir(tmpdir)
                result = logger.start()
                assert result["logging"] is True
                assert result["path"].startswith(tmpdir.replace("\\", "/") if os.name == "nt" else tmpdir) or os.path.exists(result["path"])
                assert result["path"].endswith(".jsonl")
                logger.stop()
            finally:
                os.chdir(orig)

    def test_stop_returns_summary(self):
        """stop() returns entry count, duration, path."""
        logger = MessageLogger()
        with tempfile.TemporaryDirectory() as tmpdir:
            path = os.path.join(tmpdir, "test.jsonl")
            logger.start(path=path)
            # Write a dummy entry to have a nonzero count
            logger.log_mcp_in("qt_ping", {})
            result = logger.stop()
            assert result["logging"] is False
            assert result["entries"] == 1
            assert result["duration"] >= 0
            assert result["path"] == path

    def test_stop_when_not_active(self):
        """stop() when inactive returns empty summary."""
        logger = MessageLogger()
        result = logger.stop()
        assert result["logging"] is False
        assert result["entries"] == 0

    def test_start_while_active_restarts(self):
        """start() while active calls stop() first, then starts new session."""
        logger = MessageLogger()
        with tempfile.TemporaryDirectory() as tmpdir:
            path1 = os.path.join(tmpdir, "log1.jsonl")
            path2 = os.path.join(tmpdir, "log2.jsonl")
            logger.start(path=path1)
            logger.log_mcp_in("qt_ping", {})
            logger.start(path=path2)
            assert logger.is_active is True
            assert logger.status()["path"] == path2
            logger.stop()

    def test_status_while_active(self):
        """status() returns full state while logging."""
        logger = MessageLogger()
        with tempfile.TemporaryDirectory() as tmpdir:
            path = os.path.join(tmpdir, "test.jsonl")
            logger.start(path=path, level=3, buffer_size=50)
            logger.log_mcp_in("qt_ping", {})
            status = logger.status()
            assert status["logging"] is True
            assert status["path"] == path
            assert status["level"] == 3
            assert status["entries"] == 1
            assert status["buffer_size"] == 50
            assert status["duration"] >= 0
            logger.stop()

    def test_custom_level_and_buffer_size(self):
        """start() accepts custom level and buffer_size."""
        logger = MessageLogger()
        with tempfile.TemporaryDirectory() as tmpdir:
            path = os.path.join(tmpdir, "test.jsonl")
            result = logger.start(path=path, level=1, buffer_size=500)
            assert result["level"] == 1
            assert result["buffer_size"] == 500
            logger.stop()
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `pytest python/tests/test_message_logger.py::TestMessageLoggerLifecycle -v`
Expected: FAIL — `qtpilot.message_logger` does not exist

- [ ] **Step 3: Write minimal MessageLogger implementation**

Create `python/src/qtpilot/message_logger.py`:

```python
"""Logs MCP and JSON-RPC messages to a JSON Lines file with ring buffer."""

from __future__ import annotations

import json
import logging
import os
import time
from collections import deque
from datetime import datetime, timezone
from typing import TextIO

logger = logging.getLogger(__name__)


class MessageLogger:
    """Captures MCP tool calls, JSON-RPC traffic, and probe notifications."""

    def __init__(self) -> None:
        self._active: bool = False
        self._file: TextIO | None = None
        self._path: str | None = None
        self._level: int = 2
        self._start_time: float = 0.0
        self._entry_count: int = 0
        self._buffer: deque[dict] = deque(maxlen=200)
        self._buffer_size: int = 200
        self._attached_probe = None  # ProbeConnection | None

    @property
    def is_active(self) -> bool:
        return self._active

    @property
    def level(self) -> int:
        return self._level

    def start(
        self,
        path: str | None = None,
        level: int = 2,
        buffer_size: int = 200,
    ) -> dict:
        """Start logging. If already active, stops first."""
        if self._active:
            self.stop()

        if path is None:
            ts = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
            path = os.path.join(os.getcwd(), f"qtPilot-log-{ts}.jsonl")

        self._path = path
        self._level = level
        self._buffer_size = buffer_size
        self._buffer = deque(maxlen=buffer_size)
        self._entry_count = 0
        self._start_time = time.monotonic()
        self._file = open(path, "a", encoding="utf-8")
        self._active = True

        return {
            "logging": True,
            "path": self._path,
            "level": self._level,
            "buffer_size": self._buffer_size,
        }

    def stop(self) -> dict:
        """Stop logging and return summary."""
        if not self._active:
            return {"logging": False, "entries": 0, "duration": 0.0}

        duration = round(time.monotonic() - self._start_time, 3)
        path = self._path
        entries = self._entry_count

        if self._file is not None:
            self._file.close()
            self._file = None

        self._active = False

        return {
            "logging": False,
            "path": path,
            "entries": entries,
            "duration": duration,
        }

    def status(self) -> dict:
        """Return current logging state."""
        result: dict = {"logging": self._active}
        if self._active:
            result["path"] = self._path
            result["level"] = self._level
            result["entries"] = self._entry_count
            result["buffer_size"] = self._buffer_size
            result["duration"] = round(time.monotonic() - self._start_time, 3)
        return result

    # -- MCP-level hooks (called by middleware) ----------------------------

    def log_mcp_in(self, tool_name: str, arguments: dict) -> None:
        """Log an incoming MCP tool call. Level >= 1."""
        if not self._active or self._level < 1:
            return
        self._write_entry({"dir": "mcp_in", "tool": tool_name, "args": arguments})

    def log_mcp_out(
        self,
        tool_name: str,
        result_summary: str,
        duration_ms: float,
        is_error: bool = False,
    ) -> None:
        """Log an outgoing MCP tool result. Level >= 1."""
        if not self._active or self._level < 1:
            return
        entry: dict = {
            "dir": "mcp_out",
            "tool": tool_name,
            "dur_ms": round(duration_ms, 1),
            "ok": not is_error,
        }
        if is_error:
            entry["error"] = result_summary
        self._write_entry(entry)

    # -- JSON-RPC level hooks (send + call observers) --------------------

    def _on_call_send(self, request: dict) -> None:
        """Send observer callback. Logs outbound JSON-RPC request. Level >= 2."""
        if not self._active or self._level < 2:
            return
        self._write_entry({
            "dir": "req",
            "id": request.get("id"),
            "method": request.get("method", ""),
            "params": self._truncate(request.get("params", {})),
        })

    def _on_call_complete(
        self, request: dict, result_or_exc: dict | Exception, duration_ms: float
    ) -> None:
        """Call observer callback. Level >= 2."""
        if not self._active or self._level < 2:
            return
        method = request.get("method", "")
        req_id = request.get("id")

        if isinstance(result_or_exc, Exception):
            self._write_entry({
                "dir": "err",
                "id": req_id,
                "method": method,
                "dur_ms": round(duration_ms, 1),
                "error": str(result_or_exc),
            })
        else:
            self._write_entry({
                "dir": "res",
                "id": req_id,
                "method": method,
                "dur_ms": round(duration_ms, 1),
                "result": self._truncate(result_or_exc),
            })

    # -- Notification level hooks ------------------------------------------

    def _on_notification(self, method: str, params: dict) -> None:
        """Notification handler callback. Level >= 3."""
        if not self._active or self._level < 3:
            return
        self._write_entry({
            "dir": "ntf",
            "method": method,
            "params": self._truncate(params),
        })

    # -- Probe attach/detach -----------------------------------------------

    def attach(self, probe) -> None:
        """Register send/call observers and notification handler on a probe."""
        self._attached_probe = probe
        probe.add_send_observer(self._on_call_send)
        probe.add_call_observer(self._on_call_complete)
        probe.add_notification_handler(self._on_notification)

    def detach(self, probe) -> None:
        """Remove send/call observers and notification handler from a probe."""
        probe.remove_send_observer(self._on_call_send)
        probe.remove_call_observer(self._on_call_complete)
        probe.remove_notification_handler(self._on_notification)
        if self._attached_probe is probe:
            self._attached_probe = None

    # -- Ring buffer --------------------------------------------------------

    def tail(self, count: int = 50) -> dict:
        """Return last N entries from the ring buffer."""
        entries = list(self._buffer)[-count:]
        return {
            "entries": entries,
            "count": len(entries),
            "total_logged": self._entry_count,
        }

    # -- Internal -----------------------------------------------------------

    def _write_entry(self, entry: dict) -> None:
        """Add timestamp, write to file, append to buffer."""
        now = datetime.now(timezone.utc)
        entry["ts"] = now.strftime("%Y-%m-%dT%H:%M:%S.") + f"{now.microsecond // 1000:03d}Z"
        if self._file is not None:
            self._file.write(json.dumps(entry, default=str) + "\n")
            self._file.flush()
        self._buffer.append(entry)
        self._entry_count += 1

    @staticmethod
    def _truncate(value, max_len: int = 4096):
        """Truncate large values for logging. Returns modified copy."""
        if isinstance(value, str):
            if len(value) > max_len:
                return value[:max_len] + f"...<truncated {len(value)}c>"
            # Detect base64 image data: long string with no spaces/newlines,
            # only base64 charset (alphanumeric + /+=)
            if (
                len(value) > 200
                and " " not in value[:100]
                and "\n" not in value[:100]
                and all(c.isalnum() or c in "+/=" for c in value[:100])
            ):
                return f"<image:{len(value)}b>"
            return value
        if isinstance(value, dict):
            return {k: MessageLogger._truncate(v, max_len) for k, v in value.items()}
        if isinstance(value, list):
            return [MessageLogger._truncate(item, max_len) for item in value]
        return value
```

- [ ] **Step 4: Run lifecycle tests to verify they pass**

Run: `pytest python/tests/test_message_logger.py::TestMessageLoggerLifecycle -v`
Expected: All 8 tests PASS

- [ ] **Step 5: Commit**

```bash
git add python/src/qtpilot/message_logger.py python/tests/test_message_logger.py
git commit -m "feat: add MessageLogger class with start/stop/status lifecycle"
```

---

### Task 5: MessageLogger entry types and verbosity filtering

**Files:**
- Modify: `python/tests/test_message_logger.py`

- [ ] **Step 1: Write entry type and filtering tests**

Append to `python/tests/test_message_logger.py`:

```python
class TestMessageLoggerEntries:
    def test_mcp_in_entry(self):
        """log_mcp_in writes correct structure."""
        logger = MessageLogger()
        with tempfile.TemporaryDirectory() as tmpdir:
            path = os.path.join(tmpdir, "test.jsonl")
            logger.start(path=path, level=1)
            logger.log_mcp_in("qt_objects_tree", {"maxDepth": 3})
            logger.stop()
            with open(path) as f:
                entry = json.loads(f.readline())
            assert entry["dir"] == "mcp_in"
            assert entry["tool"] == "qt_objects_tree"
            assert entry["args"] == {"maxDepth": 3}
            assert "ts" in entry

    def test_mcp_out_success(self):
        """log_mcp_out writes ok=True on success."""
        logger = MessageLogger()
        with tempfile.TemporaryDirectory() as tmpdir:
            path = os.path.join(tmpdir, "test.jsonl")
            logger.start(path=path, level=1)
            logger.log_mcp_out("qt_ping", "pong", 12.5)
            logger.stop()
            with open(path) as f:
                entry = json.loads(f.readline())
            assert entry["dir"] == "mcp_out"
            assert entry["ok"] is True
            assert entry["dur_ms"] == 12.5

    def test_mcp_out_error(self):
        """log_mcp_out writes ok=False and error on failure."""
        logger = MessageLogger()
        with tempfile.TemporaryDirectory() as tmpdir:
            path = os.path.join(tmpdir, "test.jsonl")
            logger.start(path=path, level=1)
            logger.log_mcp_out("qt_ping", "connection lost", 5.0, is_error=True)
            logger.stop()
            with open(path) as f:
                entry = json.loads(f.readline())
            assert entry["ok"] is False
            assert entry["error"] == "connection lost"

    def test_req_entry(self):
        """_on_call_send writes req entry."""
        logger = MessageLogger()
        with tempfile.TemporaryDirectory() as tmpdir:
            path = os.path.join(tmpdir, "test.jsonl")
            logger.start(path=path, level=2)
            logger._on_call_send({"method": "qt.ping", "id": 1, "params": {}})
            logger.stop()
            with open(path) as f:
                entry = json.loads(f.readline())
            assert entry["dir"] == "req"
            assert entry["method"] == "qt.ping"
            assert entry["id"] == 1
            assert "params" in entry

    def test_call_observer_success_entry(self):
        """_on_call_complete writes res entry on success."""
        logger = MessageLogger()
        with tempfile.TemporaryDirectory() as tmpdir:
            path = os.path.join(tmpdir, "test.jsonl")
            logger.start(path=path, level=2)
            logger._on_call_complete(
                {"method": "qt.ping", "id": 1},
                {"pong": True},
                50.0,
            )
            logger.stop()
            with open(path) as f:
                entry = json.loads(f.readline())
            assert entry["dir"] == "res"
            assert entry["method"] == "qt.ping"
            assert entry["id"] == 1
            assert entry["dur_ms"] == 50.0

    def test_call_observer_error_entry(self):
        """_on_call_complete writes err entry on exception."""
        logger = MessageLogger()
        with tempfile.TemporaryDirectory() as tmpdir:
            path = os.path.join(tmpdir, "test.jsonl")
            logger.start(path=path, level=2)
            logger._on_call_complete(
                {"method": "qt.objects.info", "id": 2},
                RuntimeError("Not found"),
                30.0,
            )
            logger.stop()
            with open(path) as f:
                entry = json.loads(f.readline())
            assert entry["dir"] == "err"
            assert entry["method"] == "qt.objects.info"
            assert "Not found" in entry["error"]

    def test_notification_entry(self):
        """_on_notification writes ntf entry."""
        logger = MessageLogger()
        with tempfile.TemporaryDirectory() as tmpdir:
            path = os.path.join(tmpdir, "test.jsonl")
            logger.start(path=path, level=3)
            logger._on_notification("qtpilot.signalEmitted", {"objectId": "btn"})
            logger.stop()
            with open(path) as f:
                entry = json.loads(f.readline())
            assert entry["dir"] == "ntf"
            assert entry["method"] == "qtpilot.signalEmitted"


class TestMessageLoggerVerbosity:
    def test_level1_skips_jsonrpc_and_notifications(self):
        """Level 1 only logs mcp_in/mcp_out."""
        logger = MessageLogger()
        with tempfile.TemporaryDirectory() as tmpdir:
            path = os.path.join(tmpdir, "test.jsonl")
            logger.start(path=path, level=1)
            logger.log_mcp_in("qt_ping", {})
            logger._on_call_send({"method": "qt.ping", "id": 1, "params": {}})
            logger._on_call_complete({"method": "qt.ping", "id": 1}, {}, 10.0)
            logger._on_notification("qtpilot.signalEmitted", {})
            logger.log_mcp_out("qt_ping", "ok", 10.0)
            logger.stop()
            with open(path) as f:
                lines = f.readlines()
            assert len(lines) == 2
            assert json.loads(lines[0])["dir"] == "mcp_in"
            assert json.loads(lines[1])["dir"] == "mcp_out"

    def test_level2_skips_notifications(self):
        """Level 2 logs mcp + jsonrpc (req/res) but not notifications."""
        logger = MessageLogger()
        with tempfile.TemporaryDirectory() as tmpdir:
            path = os.path.join(tmpdir, "test.jsonl")
            logger.start(path=path, level=2)
            logger.log_mcp_in("qt_ping", {})
            logger._on_call_send({"method": "qt.ping", "id": 1, "params": {}})
            logger._on_call_complete({"method": "qt.ping", "id": 1}, {}, 10.0)
            logger._on_notification("qtpilot.signalEmitted", {})
            logger.log_mcp_out("qt_ping", "ok", 10.0)
            logger.stop()
            with open(path) as f:
                lines = f.readlines()
            assert len(lines) == 4  # mcp_in, req, res, mcp_out
            dirs = [json.loads(l)["dir"] for l in lines]
            assert "ntf" not in dirs
            assert "req" in dirs
            assert "res" in dirs

    def test_level3_captures_everything(self):
        """Level 3 captures all entry types."""
        logger = MessageLogger()
        with tempfile.TemporaryDirectory() as tmpdir:
            path = os.path.join(tmpdir, "test.jsonl")
            logger.start(path=path, level=3)
            logger.log_mcp_in("qt_ping", {})
            logger._on_call_send({"method": "qt.ping", "id": 1, "params": {}})
            logger._on_call_complete({"method": "qt.ping", "id": 1}, {}, 10.0)
            logger._on_notification("qtpilot.signalEmitted", {})
            logger.log_mcp_out("qt_ping", "ok", 10.0)
            logger.stop()
            with open(path) as f:
                lines = f.readlines()
            assert len(lines) == 5  # mcp_in, req, res, ntf, mcp_out

    def test_inactive_logger_writes_nothing(self):
        """All hooks are no-ops when logger is inactive."""
        logger = MessageLogger()
        logger.log_mcp_in("qt_ping", {})
        logger.log_mcp_out("qt_ping", "ok", 10.0)
        logger._on_call_send({"method": "qt.ping", "id": 1, "params": {}})
        logger._on_call_complete({"method": "qt.ping", "id": 1}, {}, 10.0)
        logger._on_notification("qtpilot.signalEmitted", {})
        assert logger.tail()["count"] == 0
```

- [ ] **Step 2: Run entry/verbosity tests**

Run: `pytest python/tests/test_message_logger.py::TestMessageLoggerEntries python/tests/test_message_logger.py::TestMessageLoggerVerbosity -v`
Expected: All tests PASS (implementation already handles these)

- [ ] **Step 3: Commit**

```bash
git add python/tests/test_message_logger.py
git commit -m "test: add entry type and verbosity filtering tests for MessageLogger"
```

---

### Task 6: Ring buffer and truncation tests

**Files:**
- Modify: `python/tests/test_message_logger.py`

- [ ] **Step 1: Write ring buffer and truncation tests**

Append to `python/tests/test_message_logger.py`:

```python
class TestMessageLoggerBuffer:
    def test_tail_returns_recent_entries(self):
        """tail() returns the most recent N entries."""
        logger = MessageLogger()
        with tempfile.TemporaryDirectory() as tmpdir:
            path = os.path.join(tmpdir, "test.jsonl")
            logger.start(path=path, level=1, buffer_size=100)
            for i in range(10):
                logger.log_mcp_in(f"tool_{i}", {})
            result = logger.tail(count=3)
            assert result["count"] == 3
            assert result["total_logged"] == 10
            assert result["entries"][-1]["tool"] == "tool_9"
            logger.stop()

    def test_buffer_respects_maxlen(self):
        """Buffer drops oldest entries when full."""
        logger = MessageLogger()
        with tempfile.TemporaryDirectory() as tmpdir:
            path = os.path.join(tmpdir, "test.jsonl")
            logger.start(path=path, level=1, buffer_size=5)
            for i in range(10):
                logger.log_mcp_in(f"tool_{i}", {})
            result = logger.tail(count=100)
            assert result["count"] == 5
            assert result["entries"][0]["tool"] == "tool_5"
            logger.stop()

    def test_tail_survives_stop(self):
        """tail() works after stop() — buffer persists."""
        logger = MessageLogger()
        with tempfile.TemporaryDirectory() as tmpdir:
            path = os.path.join(tmpdir, "test.jsonl")
            logger.start(path=path, level=1)
            logger.log_mcp_in("qt_ping", {})
            logger.stop()
            result = logger.tail()
            assert result["count"] == 1

    def test_start_clears_buffer(self):
        """start() resets the buffer from the previous session."""
        logger = MessageLogger()
        with tempfile.TemporaryDirectory() as tmpdir:
            path1 = os.path.join(tmpdir, "log1.jsonl")
            path2 = os.path.join(tmpdir, "log2.jsonl")
            logger.start(path=path1, level=1)
            logger.log_mcp_in("qt_ping", {})
            logger.stop()
            logger.start(path=path2, level=1)
            assert logger.tail()["count"] == 0
            logger.stop()


class TestMessageLoggerTruncation:
    def test_truncate_long_string(self):
        """Strings longer than max_len are truncated."""
        long_str = "x" * 5000
        result = MessageLogger._truncate(long_str, max_len=4096)
        assert len(result) < 5000
        assert "truncated" in result
        assert "5000c" in result

    def test_truncate_base64_image(self):
        """Base64-like strings are replaced with placeholder."""
        # Simulate base64 image data (long alphanumeric string)
        b64 = "A" * 300
        result = MessageLogger._truncate(b64, max_len=4096)
        assert result == f"<image:{len(b64)}b>"

    def test_truncate_dict_recursive(self):
        """Truncation recurses into dicts."""
        data = {"key": "x" * 5000}
        result = MessageLogger._truncate(data, max_len=100)
        assert "truncated" in result["key"]

    def test_truncate_list_recursive(self):
        """Truncation recurses into lists."""
        data = ["x" * 5000]
        result = MessageLogger._truncate(data, max_len=100)
        assert "truncated" in result[0]

    def test_truncate_short_string_unchanged(self):
        """Short strings pass through unchanged."""
        assert MessageLogger._truncate("hello") == "hello"

    def test_truncate_non_string_unchanged(self):
        """Numbers, booleans, None pass through unchanged."""
        assert MessageLogger._truncate(42) == 42
        assert MessageLogger._truncate(True) is True
        assert MessageLogger._truncate(None) is None
```

- [ ] **Step 2: Run buffer and truncation tests**

Run: `pytest python/tests/test_message_logger.py::TestMessageLoggerBuffer python/tests/test_message_logger.py::TestMessageLoggerTruncation -v`
Expected: All tests PASS

- [ ] **Step 3: Commit**

```bash
git add python/tests/test_message_logger.py
git commit -m "test: add ring buffer and truncation tests for MessageLogger"
```

---

### Task 7: MessageLogger attach/detach tests

**Files:**
- Modify: `python/tests/test_message_logger.py`

- [ ] **Step 1: Write attach/detach tests**

Append to `python/tests/test_message_logger.py`:

```python
class TestMessageLoggerAttachDetach:
    async def test_attach_registers_handlers(self, mock_probe):
        """attach() adds send observer, call observer, and notification handler to probe."""
        probe, mock_ws = mock_probe
        logger = MessageLogger()
        initial_handlers = len(probe._notification_handlers)
        initial_call_observers = len(probe._call_observers)
        initial_send_observers = len(probe._send_observers)

        logger.attach(probe)

        assert len(probe._notification_handlers) == initial_handlers + 1
        assert len(probe._call_observers) == initial_call_observers + 1
        assert len(probe._send_observers) == initial_send_observers + 1

        logger.detach(probe)

    async def test_detach_removes_handlers(self, mock_probe):
        """detach() removes all observers and handlers."""
        probe, mock_ws = mock_probe
        logger = MessageLogger()

        logger.attach(probe)
        logger.detach(probe)

        assert logger._on_notification not in probe._notification_handlers
        assert logger._on_call_complete not in probe._call_observers
        assert logger._on_call_send not in probe._send_observers

    async def test_detach_without_attach_is_noop(self, mock_probe):
        """detach() on a probe that was never attached does not raise."""
        probe, mock_ws = mock_probe
        logger = MessageLogger()
        logger.detach(probe)  # no error
```

- [ ] **Step 2: Run attach/detach tests**

Run: `pytest python/tests/test_message_logger.py::TestMessageLoggerAttachDetach -v`
Expected: All 3 tests PASS

- [ ] **Step 3: Commit**

```bash
git add python/tests/test_message_logger.py
git commit -m "test: add attach/detach tests for MessageLogger"
```

---

## Chunk 3: Middleware, Tools, and Server Wiring

### Task 8: LoggingMiddleware

**Files:**
- Create: `python/src/qtpilot/logging_middleware.py`
- Create: `python/tests/test_logging_middleware.py`

- [ ] **Step 1: Write middleware tests**

Create `python/tests/test_logging_middleware.py`:

```python
"""Unit tests for LoggingMiddleware."""

from __future__ import annotations

import time
from unittest.mock import AsyncMock, MagicMock, patch

import pytest

from qtpilot.logging_middleware import LoggingMiddleware

pytestmark = pytest.mark.asyncio


def _make_context(tool_name: str, args: dict | None = None):
    """Create a mock MiddlewareContext."""
    ctx = MagicMock()
    ctx.message.name = tool_name
    ctx.message.arguments = args or {}
    return ctx


def _make_result(text: str = "ok"):
    """Create a mock ToolResult."""
    content_block = MagicMock()
    content_block.type = "text"
    content_block.text = text
    result = MagicMock()
    result.content = [content_block]
    return result


class TestLoggingMiddleware:
    async def test_logs_mcp_in_and_out(self):
        """Middleware logs mcp_in before and mcp_out after a tool call."""
        mock_logger = MagicMock()
        mock_logger.is_active = True

        middleware = LoggingMiddleware()

        ctx = _make_context("qt_ping", {})
        call_next = AsyncMock(return_value=_make_result("pong"))

        with patch("qtpilot.logging_middleware.get_message_logger", return_value=mock_logger):
            await middleware.on_call_tool(ctx, call_next)

        mock_logger.log_mcp_in.assert_called_once_with("qt_ping", {})
        mock_logger.log_mcp_out.assert_called_once()
        args = mock_logger.log_mcp_out.call_args
        assert args[0][0] == "qt_ping"  # tool_name
        assert args[1].get("is_error", False) is False or args[0][3] is False

    async def test_skips_log_tools(self):
        """Middleware does not log qtpilot_log_* tools."""
        mock_logger = MagicMock()
        mock_logger.is_active = True

        middleware = LoggingMiddleware()

        ctx = _make_context("qtpilot_log_start", {"level": 2})
        call_next = AsyncMock(return_value=_make_result())

        with patch("qtpilot.logging_middleware.get_message_logger", return_value=mock_logger):
            await middleware.on_call_tool(ctx, call_next)

        mock_logger.log_mcp_in.assert_not_called()
        mock_logger.log_mcp_out.assert_not_called()

    async def test_logs_error_on_exception(self):
        """Middleware logs is_error=True when tool raises."""
        mock_logger = MagicMock()
        mock_logger.is_active = True

        middleware = LoggingMiddleware()

        ctx = _make_context("qt_ping", {})
        call_next = AsyncMock(side_effect=RuntimeError("probe down"))

        with patch("qtpilot.logging_middleware.get_message_logger", return_value=mock_logger):
            with pytest.raises(RuntimeError, match="probe down"):
                await middleware.on_call_tool(ctx, call_next)

        mock_logger.log_mcp_in.assert_called_once()
        mock_logger.log_mcp_out.assert_called_once()
        out_args = mock_logger.log_mcp_out.call_args
        # Check is_error=True was passed
        assert out_args[1].get("is_error") is True or out_args[0][3] is True

    async def test_inactive_logger_skips_logging(self):
        """Middleware does nothing when logger is inactive."""
        mock_logger = MagicMock()
        mock_logger.is_active = False

        middleware = LoggingMiddleware()

        ctx = _make_context("qt_ping", {})
        call_next = AsyncMock(return_value=_make_result())

        with patch("qtpilot.logging_middleware.get_message_logger", return_value=mock_logger):
            await middleware.on_call_tool(ctx, call_next)

        mock_logger.log_mcp_in.assert_not_called()
        mock_logger.log_mcp_out.assert_not_called()

    async def test_duration_is_positive(self):
        """Middleware records a positive duration_ms."""
        mock_logger = MagicMock()
        mock_logger.is_active = True

        middleware = LoggingMiddleware()

        ctx = _make_context("qt_ping", {})
        call_next = AsyncMock(return_value=_make_result())

        with patch("qtpilot.logging_middleware.get_message_logger", return_value=mock_logger):
            await middleware.on_call_tool(ctx, call_next)

        out_args = mock_logger.log_mcp_out.call_args
        duration_ms = out_args[0][2]  # 3rd positional arg
        assert duration_ms >= 0
```

- [ ] **Step 2: Run middleware tests to verify they fail**

Run: `pytest python/tests/test_logging_middleware.py -v`
Expected: FAIL — `qtpilot.logging_middleware` does not exist

- [ ] **Step 3: Write LoggingMiddleware implementation**

Create `python/src/qtpilot/logging_middleware.py`:

```python
"""FastMCP middleware that intercepts tool calls for message logging."""

from __future__ import annotations

import time

from fastmcp.server.middleware import Middleware


def _summarize_tool_result(result) -> str:
    """Extract a text summary from a ToolResult."""
    parts = []
    for block in getattr(result, "content", []):
        if getattr(block, "type", None) == "text":
            text = getattr(block, "text", "")
            if len(text) > 200:
                text = text[:200] + "..."
            parts.append(text)
        elif getattr(block, "type", None) == "image":
            parts.append("<image>")
        else:
            parts.append(f"<{getattr(block, 'type', 'unknown')}>")
    return " ".join(parts) if parts else str(result)


class LoggingMiddleware(Middleware):
    """Intercepts MCP tool calls to log request/response at the MCP layer."""

    async def on_call_tool(self, context, call_next):
        from qtpilot.server import get_message_logger

        logger = get_message_logger()
        tool_name = context.message.name
        args = context.message.arguments or {}

        # Skip logging our own tools to avoid recursion
        if tool_name.startswith("qtpilot_log_"):
            return await call_next(context)

        if not logger.is_active:
            return await call_next(context)

        logger.log_mcp_in(tool_name, args)

        t0 = time.monotonic()
        try:
            result = await call_next(context)
            duration_ms = (time.monotonic() - t0) * 1000
            summary = _summarize_tool_result(result)
            logger.log_mcp_out(tool_name, summary, duration_ms, is_error=False)
            return result
        except Exception as exc:
            duration_ms = (time.monotonic() - t0) * 1000
            logger.log_mcp_out(tool_name, str(exc), duration_ms, is_error=True)
            raise
```

- [ ] **Step 4: Run middleware tests**

Run: `pytest python/tests/test_logging_middleware.py -v`
Expected: All 5 tests PASS

- [ ] **Step 5: Commit**

```bash
git add python/src/qtpilot/logging_middleware.py python/tests/test_logging_middleware.py
git commit -m "feat: add LoggingMiddleware for MCP-level message interception"
```

---

### Task 9: Logging tools

**Files:**
- Create: `python/src/qtpilot/tools/logging_tools.py`
- Create: `python/tests/test_logging_tools.py`

- [ ] **Step 1: Write tool registration tests**

Create `python/tests/test_logging_tools.py`:

```python
"""Unit tests for logging tool registration."""

from __future__ import annotations

import pytest

from fastmcp import FastMCP

from qtpilot.tools.logging_tools import register_logging_tools


def _tool_names(mcp: FastMCP) -> set[str]:
    """Extract registered tool names from a FastMCP instance."""
    return set(mcp._tool_manager._tools.keys())


class TestLoggingTools:
    def test_logging_tools_registered(self, mock_mcp):
        """Logging tools register exactly 4 tools."""
        register_logging_tools(mock_mcp)
        assert len(_tool_names(mock_mcp)) == 4

    def test_logging_tool_names(self, mock_mcp):
        """All logging tool names are present."""
        register_logging_tools(mock_mcp)
        names = _tool_names(mock_mcp)
        expected = {
            "qtpilot_log_start",
            "qtpilot_log_stop",
            "qtpilot_log_status",
            "qtpilot_log_tail",
        }
        missing = expected - names
        assert not missing, f"Missing logging tools: {missing}"
```

- [ ] **Step 2: Run tool tests to verify they fail**

Run: `pytest python/tests/test_logging_tools.py -v`
Expected: FAIL — `qtpilot.tools.logging_tools` does not exist

- [ ] **Step 3: Write logging tools implementation**

Create `python/src/qtpilot/tools/logging_tools.py`:

```python
"""Logging tools -- start/stop message logging and tail recent entries."""

from __future__ import annotations

from fastmcp import Context, FastMCP


def register_logging_tools(mcp: FastMCP) -> None:
    """Register message logging tools on the MCP server."""

    @mcp.tool
    async def qtpilot_log_start(
        path: str | None = None,
        level: int = 2,
        buffer_size: int = 200,
        ctx: Context = None,
    ) -> dict:
        """Start logging MCP and JSON-RPC messages to a file.

        Captures tool calls, probe wire traffic, and notifications at
        configurable verbosity levels. Also maintains an in-memory ring
        buffer accessible via qtpilot_log_tail.

        Args:
            path: Output file path. Default: qtPilot-log-YYYYMMDD-HHMMSS.jsonl in cwd.
            level: Verbosity level:
                1 = minimal (MCP tool calls only)
                2 = normal (+ JSON-RPC wire traffic) [default]
                3 = verbose (+ probe notifications)
            buffer_size: Ring buffer capacity (default 200 entries).

        Example: qtpilot_log_start(level=3)
        """
        from qtpilot.server import get_message_logger, get_probe

        logger = get_message_logger()
        result = logger.start(path=path, level=level, buffer_size=buffer_size)

        # Attach to current probe if connected
        probe = get_probe()
        if probe is not None and probe.is_connected:
            logger.attach(probe)

        return result

    @mcp.tool
    async def qtpilot_log_stop(ctx: Context = None) -> dict:
        """Stop message logging and return summary.

        Returns the file path, total entries logged, and session duration.
        The ring buffer is preserved — use qtpilot_log_tail to read entries
        after stopping.

        Example: qtpilot_log_stop()
        """
        from qtpilot.server import get_message_logger, get_probe

        logger = get_message_logger()

        # Detach from probe before stopping
        probe = get_probe()
        if probe is not None and logger._attached_probe is probe:
            logger.detach(probe)

        return logger.stop()

    @mcp.tool
    async def qtpilot_log_status(ctx: Context = None) -> dict:
        """Get current message logging status.

        Returns whether logging is active, file path, verbosity level,
        entry count, buffer size, and session duration.

        Example: qtpilot_log_status()
        """
        from qtpilot.server import get_message_logger

        return get_message_logger().status()

    @mcp.tool
    async def qtpilot_log_tail(count: int = 50, ctx: Context = None) -> dict:
        """Return recent log entries from the in-memory ring buffer.

        Works even after qtpilot_log_stop — the buffer persists until the
        next qtpilot_log_start.

        Args:
            count: Number of recent entries to return (default 50).

        Example: qtpilot_log_tail(count=10)
        """
        from qtpilot.server import get_message_logger

        return get_message_logger().tail(count=count)
```

- [ ] **Step 4: Run tool tests**

Run: `pytest python/tests/test_logging_tools.py -v`
Expected: All 2 tests PASS

- [ ] **Step 5: Commit**

```bash
git add python/src/qtpilot/tools/logging_tools.py python/tests/test_logging_tools.py
git commit -m "feat: add qtpilot_log_start/stop/status/tail MCP tools"
```

---

### Task 10: Wire everything into server.py

**Files:**
- Modify: `python/src/qtpilot/server.py:15,35,40,112-116,123-136,139-144,307-313`

- [ ] **Step 1: Add MessageLogger to ServerState**

In `python/src/qtpilot/server.py`:

1. Add import at top (after line 15):
```python
from qtpilot.message_logger import MessageLogger
```

2. In `ServerState.__init__` (line 40), add after `self.recorder`:
```python
        self.message_logger: MessageLogger = MessageLogger()
```

3. Add accessor after `get_recorder()` (after line 116):
```python
def get_message_logger() -> MessageLogger:
    """Get the shared MessageLogger instance."""
    if _state is None:
        raise RuntimeError("Server not initialized")
    return _state.message_logger
```

- [ ] **Step 2: Add middleware and tool registration to create_server()**

In `python/src/qtpilot/server.py`, in `create_server()`:

1. After creating `_state` (line 335), add middleware registration:
```python
    # Register logging middleware (before tool registration)
    from qtpilot.logging_middleware import LoggingMiddleware
    mcp.add_middleware(LoggingMiddleware())
```

2. After discovery tools registration (line 339), add logging tools:
```python
    # Register logging tools (always available regardless of mode)
    from qtpilot.tools.logging_tools import register_logging_tools
    register_logging_tools(mcp)
```

- [ ] **Step 3: Add logger attach/detach to probe lifecycle**

In `python/src/qtpilot/server.py`:

1. In `connect_to_probe()` (around line 130, after `state.probe = None`), add:
```python
    # Detach logger from old probe
    if state.message_logger._attached_probe is not None:
        state.message_logger.detach(state.message_logger._attached_probe)
```

And after `state.probe = conn` (around line 134), add:
```python
    # Attach logger if active
    if state.message_logger.is_active:
        state.message_logger.attach(conn)
```

2. In `disconnect_probe()` (around line 142, before `await state.probe.disconnect()`), add:
```python
    if state.message_logger._attached_probe is state.probe:
        state.message_logger.detach(state.probe)
```

3. In lifespan `finally` block (around line 307, before recorder stop), add:
```python
            # Stop message logger if active
            if state.message_logger.is_active:
                state.message_logger.stop()
```

- [ ] **Step 4: Run full test suite**

Run: `pytest python/tests/ -v`
Expected: All tests PASS — no regressions

- [ ] **Step 5: Commit**

```bash
git add python/src/qtpilot/server.py
git commit -m "feat: wire MessageLogger into ServerState, middleware, and probe lifecycle"
```

---

### Task 11: Final regression check

- [ ] **Step 1: Run the full test suite**

Run: `pytest python/tests/ -v`
Expected: All tests PASS

- [ ] **Step 2: Verify no import errors**

Run: `python -c "from qtpilot.message_logger import MessageLogger; from qtpilot.logging_middleware import LoggingMiddleware; from qtpilot.tools.logging_tools import register_logging_tools; print('All imports OK')"`
Expected: `All imports OK`

- [ ] **Step 3: Verify the module structure is correct**

Run: `python -c "from qtpilot.server import get_message_logger; print('Server accessor OK')"`
Expected: Should fail gracefully with `RuntimeError: Server not initialized` (expected — no server running)
