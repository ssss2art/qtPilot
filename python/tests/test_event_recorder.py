"""Unit tests for EventRecorder and notification routing."""

from __future__ import annotations

import asyncio
import json
import time

import pytest
import pytest_asyncio

from qtpilot.connection import ProbeConnection
from qtpilot.event_recorder import (
    FALLBACK_SIGNALS,
    INTERACTIVE_SIGNALS,
    EventRecorder,
    RecordedEvent,
    TargetSpec,
)


pytestmark = pytest.mark.asyncio


# ---------------------------------------------------------------------------
# Phase 1: Notification routing in ProbeConnection
# ---------------------------------------------------------------------------


class TestNotificationRouting:
    async def test_handler_receives_signal_notification(self, mock_probe):
        """Notification with method field is routed to the handler."""
        probe, mock_ws = mock_probe
        received = []

        probe.add_notification_handler(lambda method, params: received.append((method, params)))

        await mock_ws.inject_notification({
            "jsonrpc": "2.0",
            "method": "qtpilot.signalEmitted",
            "params": {"objectId": "btn", "signal": "clicked"},
        })
        # Give the recv loop time to process
        await asyncio.sleep(0.1)

        assert len(received) == 1
        assert received[0][0] == "qtpilot.signalEmitted"
        assert received[0][1]["signal"] == "clicked"

    async def test_handler_receives_lifecycle_notification(self, mock_probe):
        """Lifecycle notifications are routed to the handler."""
        probe, mock_ws = mock_probe
        received = []

        probe.add_notification_handler(lambda method, params: received.append((method, params)))

        await mock_ws.inject_notification({
            "jsonrpc": "2.0",
            "method": "qtpilot.objectCreated",
            "params": {"objectId": "dialog", "className": "QDialog"},
        })
        await asyncio.sleep(0.1)

        assert len(received) == 1
        assert received[0][0] == "qtpilot.objectCreated"

    async def test_no_handler_ignores_notification(self, mock_probe):
        """Without a handler, notifications are silently ignored (no crash)."""
        probe, mock_ws = mock_probe

        await mock_ws.inject_notification({
            "jsonrpc": "2.0",
            "method": "qtpilot.signalEmitted",
            "params": {},
        })
        await asyncio.sleep(0.1)
        # No crash = pass

    async def test_handler_exception_does_not_crash_recv_loop(self, mock_probe):
        """A crashing handler doesn't kill the recv loop."""
        probe, mock_ws = mock_probe

        def bad_handler(method, params):
            raise RuntimeError("handler crash")

        probe.add_notification_handler(bad_handler)

        await mock_ws.inject_notification({
            "jsonrpc": "2.0",
            "method": "qtpilot.signalEmitted",
            "params": {},
        })
        await asyncio.sleep(0.1)

        # Recv loop should still be alive -- verify by doing a normal call
        mock_ws.responses[probe._next_id] = {
            "jsonrpc": "2.0",
            "result": {"pong": True},
            "id": probe._next_id,
        }
        result = await probe.call("qt.ping")
        assert result == {"pong": True}

    async def test_unregister_handler(self, mock_probe):
        """Removing handler stops routing."""
        probe, mock_ws = mock_probe
        received = []

        received_handler = lambda m, p: received.append(m)
        probe.add_notification_handler(received_handler)

        await mock_ws.inject_notification({
            "jsonrpc": "2.0",
            "method": "qtpilot.signalEmitted",
            "params": {},
        })
        await asyncio.sleep(0.1)
        assert len(received) == 1

        probe.remove_notification_handler(received_handler)

        await mock_ws.inject_notification({
            "jsonrpc": "2.0",
            "method": "qtpilot.signalEmitted",
            "params": {},
        })
        await asyncio.sleep(0.1)
        assert len(received) == 1  # no new events


# ---------------------------------------------------------------------------
# Phase 2: EventRecorder
# ---------------------------------------------------------------------------


class TestEventRecorder:
    async def test_start_stop_basic(self, mock_probe):
        """Start and stop recording with a target."""
        probe, mock_ws = mock_probe
        recorder = EventRecorder()

        # Call sequence: setLifecycle(id=1), objects.info(id=2),
        # subscribe clicked(id=3), subscribe toggled(id=4)
        # Probe wraps responses: {"meta":{...}, "result": {actual data}}
        base = probe._next_id  # should be 1
        mock_ws.responses[base] = {
            "jsonrpc": "2.0", "result": {}, "id": base,
        }
        mock_ws.responses[base + 1] = {
            "jsonrpc": "2.0",
            "result": {"meta": {}, "result": {"className": "QPushButton", "children": []}},
            "id": base + 1,
        }
        mock_ws.responses[base + 2] = {
            "jsonrpc": "2.0",
            "result": {"meta": {}, "result": {"subscriptionId": "sub_0"}},
            "id": base + 2,
        }
        mock_ws.responses[base + 3] = {
            "jsonrpc": "2.0",
            "result": {"meta": {}, "result": {"subscriptionId": "sub_1"}},
            "id": base + 3,
        }

        target = TargetSpec(object_id="btn")
        result = await recorder.start(probe, [target])

        assert result["recording"] is True
        assert result["targets"] == 1
        assert result["subscriptions"] == 2  # clicked + toggled
        assert recorder.is_recording is True

    async def test_stop_without_start(self, mock_probe):
        """Stopping when not recording returns empty result."""
        probe, mock_ws = mock_probe
        recorder = EventRecorder()

        result = await recorder.stop(probe)

        assert result["recording"] is False
        assert result["event_count"] == 0
        assert result["events"] == []

    async def test_status_not_recording(self):
        """Status when idle."""
        recorder = EventRecorder()
        status = recorder.status()
        assert status["recording"] is False
        assert status["event_count"] == 0

    async def test_event_capture(self):
        """Notification handler captures events with correct timestamps."""
        recorder = EventRecorder()
        recorder._recording = True
        recorder._start_time = time.monotonic() - 1.0  # started 1 second ago
        recorder._subscriptions = ["sub_1"]
        recorder._include_lifecycle = True

        recorder._handle_notification("qtpilot.signalEmitted", {
            "subscriptionId": "sub_1",
            "objectId": "MainWindow/QPushButton#okBtn",
            "objectName": "okBtn",
            "signal": "clicked",
            "args": [],
        })

        assert recorder.event_count == 1
        event = recorder._events[0]
        assert event.event_type == "signal"
        assert event.object_id == "MainWindow/QPushButton#okBtn"
        assert event.object_name == "okBtn"
        assert event.detail == "clicked"
        assert event.timestamp >= 1.0  # at least 1 second since start

    async def test_event_capture_lifecycle(self):
        """Lifecycle notifications are captured."""
        recorder = EventRecorder()
        recorder._recording = True
        recorder._start_time = time.monotonic()
        recorder._subscriptions = []
        recorder._include_lifecycle = True

        recorder._handle_notification("qtpilot.objectCreated", {
            "objectId": "MainWindow/QDialog#confirm",
            "objectName": "confirm",
            "className": "QDialog",
        })

        recorder._handle_notification("qtpilot.objectDestroyed", {
            "objectId": "MainWindow/QDialog#confirm",
            "objectName": "confirm",
        })

        assert recorder.event_count == 1
        assert recorder._events[0].event_type == "object_created"
        assert recorder._events[0].detail == "QDialog"

    async def test_captures_all_subscriptions(self):
        """Notifications from any subscription are captured."""
        recorder = EventRecorder()
        recorder._recording = True
        recorder._start_time = time.monotonic()
        recorder._subscriptions = ["sub_1"]
        recorder._include_lifecycle = False

        recorder._handle_notification("qtpilot.signalEmitted", {
            "subscriptionId": "foreign_sub",
            "objectId": "btn",
            "signal": "clicked",
        })

        assert recorder.event_count == 1

    async def test_lifecycle_disabled(self):
        """Lifecycle events are not captured when include_lifecycle=False."""
        recorder = EventRecorder()
        recorder._recording = True
        recorder._start_time = time.monotonic()
        recorder._subscriptions = []
        recorder._include_lifecycle = False

        recorder._handle_notification("qtpilot.objectCreated", {
            "objectId": "obj",
            "className": "QWidget",
        })

        assert recorder.event_count == 0

    async def test_not_recording_ignores(self):
        """Handler does nothing when not recording."""
        recorder = EventRecorder()
        # _recording is False by default

        recorder._handle_notification("qtpilot.signalEmitted", {
            "subscriptionId": "sub_1",
            "objectId": "btn",
            "signal": "clicked",
        })

        assert recorder.event_count == 0


class TestRecordedEvent:
    def test_signal_to_dict(self):
        """Signal event serializes with signal-specific fields."""
        event = RecordedEvent(
            timestamp=1.234,
            event_type="signal",
            object_id="MainWindow/QPushButton#okBtn",
            object_name="okBtn",
            detail="clicked",
            arguments=[],
        )
        d = event.to_dict()
        assert d == {
            "t": 1.234,
            "type": "signal",
            "object": "MainWindow/QPushButton#okBtn",
            "name": "okBtn",
            "signal": "clicked",
            "args": [],
        }

    def test_created_to_dict(self):
        """Object created event serializes with class field."""
        event = RecordedEvent(
            timestamp=2.0,
            event_type="object_created",
            object_id="MainWindow/QDialog#dlg",
            object_name="dlg",
            detail="QDialog",
        )
        d = event.to_dict()
        assert d == {
            "t": 2.0,
            "type": "object_created",
            "object": "MainWindow/QDialog#dlg",
            "name": "dlg",
            "class": "QDialog",
        }

    def test_destroyed_to_dict_no_name(self):
        """Destroyed event without objectName omits the name field."""
        event = RecordedEvent(
            timestamp=3.5,
            event_type="object_destroyed",
            object_id="MainWindow/QObject~42",
        )
        d = event.to_dict()
        assert d == {
            "t": 3.5,
            "type": "object_destroyed",
            "object": "MainWindow/QObject~42",
        }
        assert "name" not in d

    def test_timestamp_rounded(self):
        """Timestamp is rounded to 3 decimal places."""
        event = RecordedEvent(
            timestamp=1.23456789,
            event_type="signal",
            object_id="btn",
            detail="clicked",
        )
        d = event.to_dict()
        assert d["t"] == 1.235


class TestSmartSignals:
    def test_interactive_signals_map(self):
        """Known classes map to expected signals."""
        assert "clicked" in INTERACTIVE_SIGNALS["QPushButton"]
        assert "textChanged" in INTERACTIVE_SIGNALS["QLineEdit"]
        assert "currentChanged" in INTERACTIVE_SIGNALS["QTabWidget"]
        assert "valueChanged" in INTERACTIVE_SIGNALS["QSlider"]

    def test_fallback_signals(self):
        """Fallback has basic interaction signals."""
        assert "clicked" in FALLBACK_SIGNALS
        assert "toggled" in FALLBACK_SIGNALS
        assert "triggered" in FALLBACK_SIGNALS
