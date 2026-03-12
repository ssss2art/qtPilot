"""Record and buffer Qt signal notifications for later retrieval."""

from __future__ import annotations

import logging
import time
from dataclasses import dataclass, field

from qtmcp.connection import ProbeConnection

logger = logging.getLogger(__name__)

# Maps Qt class names to their most useful interactive signals.
# When a target specifies signals=None, these defaults are used.
INTERACTIVE_SIGNALS: dict[str, list[str]] = {
    "QPushButton": ["clicked", "toggled"],
    "QToolButton": ["clicked", "toggled", "triggered"],
    "QAction": ["triggered", "toggled"],
    "QLineEdit": ["textChanged", "textEdited"],
    "QTextEdit": ["textChanged"],
    "QPlainTextEdit": ["textChanged"],
    "QCheckBox": ["stateChanged", "toggled"],
    "QRadioButton": ["toggled"],
    "QComboBox": ["currentIndexChanged", "currentTextChanged"],
    "QSlider": ["valueChanged"],
    "QSpinBox": ["valueChanged"],
    "QDoubleSpinBox": ["valueChanged"],
    "QDial": ["valueChanged"],
    "QTabWidget": ["currentChanged"],
    "QTabBar": ["currentChanged"],
    "QListView": ["clicked", "doubleClicked", "activated"],
    "QTreeView": ["clicked", "doubleClicked", "activated", "expanded", "collapsed"],
    "QTableView": ["clicked", "doubleClicked", "activated"],
    "QListWidget": ["currentItemChanged", "itemClicked", "itemDoubleClicked"],
    "QTreeWidget": ["currentItemChanged", "itemClicked", "itemDoubleClicked"],
    "QTableWidget": ["currentCellChanged", "cellClicked", "cellDoubleClicked"],
    "QMenu": ["triggered"],
    "QMenuBar": ["triggered"],
}

# Fallback signals to try when the class isn't in INTERACTIVE_SIGNALS.
FALLBACK_SIGNALS = ["clicked", "toggled", "triggered"]


@dataclass
class TargetSpec:
    """Specifies which object to record and optionally which signals."""

    object_id: str
    signals: list[str] | None = None  # None = use smart defaults
    recursive: bool = False


@dataclass
class RecordedEvent:
    """A single captured event with a timestamp relative to recording start."""

    timestamp: float  # seconds since recording started
    event_type: str  # "signal", "object_created", "object_destroyed"
    object_id: str  # hierarchical object ID
    object_name: str | None = None  # QObject::objectName if non-empty
    detail: str = ""  # signal name or class name
    arguments: list = field(default_factory=list)

    def to_dict(self) -> dict:
        """Convert to compact output format matching the spec."""
        d: dict = {
            "t": round(self.timestamp, 3),
            "type": self.event_type,
            "object": self.object_id,
        }
        if self.object_name:
            d["name"] = self.object_name
        if self.event_type == "signal":
            d["signal"] = self.detail
            d["args"] = self.arguments
        elif self.event_type == "object_created":
            d["class"] = self.detail
        return d


@dataclass
class RecordedInputEvent:
    """A single captured QEvent from the global event filter."""

    timestamp: float  # seconds since recording started
    event_type: str  # e.g. "MouseButtonPress", "KeyPress", "FocusIn"
    object_id: str  # hierarchical object ID
    object_name: str | None = None
    class_name: str = ""
    detail: dict = field(default_factory=dict)  # event-specific fields

    def to_dict(self) -> dict:
        """Convert to compact output format."""
        d: dict = {
            "t": round(self.timestamp, 3),
            "type": "event",
            "event": self.event_type,
            "object": self.object_id,
            "class": self.class_name,
        }
        if self.object_name:
            d["name"] = self.object_name
        d.update(self.detail)
        return d


class EventRecorder:
    """Buffers probe notifications between start() and stop() calls."""

    def __init__(self) -> None:
        self._recording: bool = False
        self._start_time: float = 0.0
        self._events: list[RecordedEvent | RecordedInputEvent] = []
        self._subscriptions: list[str] = []  # subscription IDs for cleanup
        self._include_lifecycle: bool = True
        self._capture_events: bool = False

    @property
    def is_recording(self) -> bool:
        return self._recording

    @property
    def event_count(self) -> int:
        return len(self._events)

    def status(self) -> dict:
        """Return current recording state."""
        result: dict = {
            "recording": self._recording,
            "event_count": len(self._events),
        }
        if self._recording:
            result["duration"] = round(time.monotonic() - self._start_time, 3)
        return result

    async def start(
        self,
        probe: ProbeConnection,
        targets: list[TargetSpec],
        include_lifecycle: bool = True,
        capture_events: bool = False,
    ) -> dict:
        """Start recording. Subscribe to signals on targets.

        If already recording, stops the previous session first (discarding events).

        Args:
            probe: Connection to the Qt probe.
            targets: Objects to watch for signal subscriptions.
            include_lifecycle: Record object creation/destruction events.
            capture_events: Enable global event capture (mouse, keyboard, focus).
                When True, the probe installs a global event filter on QApplication
                so no per-widget subscription is needed for input events.
        """
        if self._recording:
            await self._cleanup_subscriptions(probe)

        self._recording = True
        self._events = []
        self._subscriptions = []
        self._include_lifecycle = include_lifecycle
        self._capture_events = capture_events
        self._start_time = time.monotonic()

        # Install notification handler
        probe.add_notification_handler(self._handle_notification)

        # Enable lifecycle notifications if requested
        if include_lifecycle:
            try:
                await probe.call("qt.signals.setLifecycle", {"enabled": True})
            except Exception:
                logger.debug("Failed to enable lifecycle notifications", exc_info=True)

        # Start global event capture if requested
        if capture_events:
            try:
                await probe.call("qt.events.startCapture")
            except Exception:
                logger.debug("Failed to start event capture", exc_info=True)

        # Subscribe to signals for each target
        total_subs = 0
        for target in targets:
            count = await self._subscribe_target(probe, target)
            total_subs += count

        return {
            "recording": True,
            "subscriptions": total_subs,
            "targets": len(targets),
            "capture_events": capture_events,
        }

    async def stop(self, probe: ProbeConnection) -> dict:
        """Stop recording. Unsubscribe all, return events."""
        if not self._recording:
            return {
                "recording": False,
                "duration": 0.0,
                "event_count": 0,
                "events": [],
            }

        duration = round(time.monotonic() - self._start_time, 3)

        # Unsubscribe and clean up
        await self._cleanup_subscriptions(probe)

        # Unregister notification handler
        probe.remove_notification_handler(self._handle_notification)

        # Disable lifecycle if we enabled it
        if self._include_lifecycle:
            try:
                await probe.call("qt.signals.setLifecycle", {"enabled": False})
            except Exception:
                logger.debug("Failed to disable lifecycle notifications", exc_info=True)

        # Stop event capture if we enabled it
        if self._capture_events:
            try:
                await probe.call("qt.events.stopCapture")
            except Exception:
                logger.debug("Failed to stop event capture", exc_info=True)

        self._recording = False

        events = [e.to_dict() for e in self._events]
        event_count = len(events)
        self._events = []

        return {
            "recording": False,
            "duration": duration,
            "event_count": event_count,
            "events": events,
        }

    def _handle_notification(self, method: str, params: dict) -> None:
        """Synchronous handler called by ProbeConnection for each notification."""
        if not self._recording:
            return

        timestamp = time.monotonic() - self._start_time

        if method == "qtmcp.signalEmitted":
            self._events.append(RecordedEvent(
                timestamp=timestamp,
                event_type="signal",
                object_id=params.get("objectId", ""),
                object_name=params.get("objectName") or None,
                detail=params.get("signal", ""),
                arguments=params.get("arguments", params.get("args", [])),
            ))
        elif method == "qtmcp.objectCreated" and self._include_lifecycle:
            self._events.append(RecordedEvent(
                timestamp=timestamp,
                event_type="object_created",
                object_id=params.get("objectId", ""),
                object_name=params.get("objectName") or None,
                detail=params.get("className", ""),
            ))
        elif method == "qtmcp.objectDestroyed" and self._include_lifecycle:
            obj_id = params.get("objectId", "")
            # Don't record destroyed events — they have empty IDs (object
            # is already partially destructed) and generate massive noise.
            # Clean up any subscriptions for the destroyed object
            # (probe will have already unsubscribed, but we remove from our list)
        elif method == "qtmcp.eventCaptured":
            # Build detail dict with event-specific fields
            detail: dict = {}
            event_type = params.get("type", "")
            if event_type.startswith("Mouse"):
                detail["button"] = params.get("button", "")
                pos = params.get("pos", {})
                detail["pos"] = [pos.get("x", 0), pos.get("y", 0)]
            elif event_type.startswith("Key"):
                detail["key"] = params.get("key", 0)
                detail["text"] = params.get("text", "")
                detail["modifiers"] = params.get("modifiers", "")
            elif event_type.startswith("Focus"):
                detail["reason"] = params.get("reason", "")

            self._events.append(RecordedInputEvent(
                timestamp=timestamp,
                event_type=event_type,
                object_id=params.get("objectId", ""),
                object_name=params.get("objectName") or None,
                class_name=params.get("className", ""),
                detail=detail,
            ))

    async def _subscribe_target(
        self, probe: ProbeConnection, target: TargetSpec
    ) -> int:
        """Subscribe to signals on a single target. Returns subscription count."""
        objects_to_subscribe = [target.object_id]

        if target.recursive:
            try:
                resp = await probe.call(
                    "qt.objects.info", {"objectId": target.object_id}
                )
                # Probe wraps: {"meta":{...}, "result": {"children":[...]}}
                info = resp.get("result", resp)
                children = info.get("children", [])
                objects_to_subscribe.extend(
                    c.get("objectId", c) if isinstance(c, dict) else c
                    for c in children
                )
            except Exception:
                logger.debug(
                    "Failed to get children for %s", target.object_id, exc_info=True
                )

        count = 0
        for obj_id in objects_to_subscribe:
            signals = target.signals
            if signals is None:
                signals = await self._resolve_smart_signals(probe, obj_id)

            for signal in signals:
                try:
                    result = await probe.call(
                        "qt.signals.subscribe",
                        {"objectId": obj_id, "signal": signal},
                    )
                    # Probe wraps result: {"meta":{...}, "result": {"subscriptionId":...}}
                    inner = result.get("result", result)
                    sub_id = inner.get("subscriptionId")
                    if sub_id:
                        self._subscriptions.append(sub_id)
                        count += 1
                except Exception:
                    logger.debug(
                        "Failed to subscribe %s.%s", obj_id, signal, exc_info=True
                    )

        return count

    async def _resolve_smart_signals(
        self, probe: ProbeConnection, object_id: str
    ) -> list[str]:
        """Determine default signals for an object based on its class."""
        try:
            resp = await probe.call("qt.objects.info", {"objectId": object_id})
            # Probe wraps: {"meta":{...}, "result": {"className":...}}
            info = resp.get("result", resp)
            class_name = info.get("className", "")
        except Exception:
            return FALLBACK_SIGNALS

        if class_name in INTERACTIVE_SIGNALS:
            return INTERACTIVE_SIGNALS[class_name]

        # Check superclasses by trying common base patterns
        for known_class, signals in INTERACTIVE_SIGNALS.items():
            if class_name.endswith(known_class) or known_class.endswith(class_name):
                return signals

        return FALLBACK_SIGNALS

    async def _cleanup_subscriptions(self, probe: ProbeConnection) -> None:
        """Unsubscribe all tracked subscriptions."""
        for sub_id in self._subscriptions:
            try:
                await probe.call(
                    "qt.signals.unsubscribe", {"subscriptionId": sub_id}
                )
            except Exception:
                logger.debug("Failed to unsubscribe %s", sub_id, exc_info=True)
        self._subscriptions = []
