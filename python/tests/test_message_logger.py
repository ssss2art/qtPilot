"""Unit tests for MessageLogger."""

from __future__ import annotations

import json
import os
import tempfile

import pytest

from qtmcp.message_logger import MessageLogger

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
            logger._on_notification("qtmcp.signalEmitted", {"objectId": "btn"})
            logger.stop()
            with open(path) as f:
                entry = json.loads(f.readline())
            assert entry["dir"] == "ntf"
            assert entry["method"] == "qtmcp.signalEmitted"


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
            logger._on_notification("qtmcp.signalEmitted", {})
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
            logger._on_notification("qtmcp.signalEmitted", {})
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
            logger._on_notification("qtmcp.signalEmitted", {})
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
        logger._on_notification("qtmcp.signalEmitted", {})
        assert logger.tail()["count"] == 0
