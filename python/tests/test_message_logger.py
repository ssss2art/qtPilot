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
