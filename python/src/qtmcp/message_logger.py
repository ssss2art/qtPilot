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
            path = os.path.join(os.getcwd(), f"qtmcp-log-{ts}.jsonl")

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
