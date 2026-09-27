"""A bounded capture reports missing evidence instead of presenting a complete trace."""

from __future__ import annotations

from dataclasses import dataclass
from unittest.mock import AsyncMock, MagicMock

import pytest

from qtpilot.event_recorder import EventRecorder


@dataclass(frozen=True)
class CaptureExpectation:
    report: dict

    def to_have_loss(self, *, retained: int, dropped: int, capacity: int) -> CaptureExpectation:
        observed = (self.report.get("event_count"), self.report.get("buffer_dropped"), self.report.get("buffer_capacity"))
        expected = (retained, dropped, capacity)
        assert observed == expected, f"Capture retained/dropped/capacity: expected {expected}, got {observed}"
        return self


@pytest.mark.asyncio
async def test_overflow_is_bounded_and_visible_through_stop() -> None:
    recorder = EventRecorder(max_events=2)
    probe = MagicMock()
    probe.call = AsyncMock(return_value={})
    await recorder.start(probe, [], include_lifecycle=False)
    for number in range(5):
        recorder._handle_notification("qtpilot.signalEmitted", {"objectId": "counter", "signal": "changed", "arguments": [number]})
    CaptureExpectation(recorder.status()).to_have_loss(retained=2, dropped=3, capacity=2)
    stopped = await recorder.stop(probe)
    CaptureExpectation(stopped).to_have_loss(retained=2, dropped=3, capacity=2)
    assert [event["args"] for event in stopped["events"]] == [[3], [4]]
    await recorder.start(probe, [], include_lifecycle=False)
    CaptureExpectation(recorder.status()).to_have_loss(retained=0, dropped=0, capacity=2)
    await recorder.stop(probe)


@pytest.mark.parametrize("capacity", [0, -1, True])
def test_invalid_capture_capacity_cannot_disable_the_bound(capacity: int) -> None:
    with pytest.raises(ValueError, match="positive integer"):
        EventRecorder(max_events=capacity)


def test_capture_matcher_rejects_unknown_loss_as_zero() -> None:
    with pytest.raises(AssertionError, match="retained/dropped/capacity"):
        CaptureExpectation({"event_count": 2, "buffer_capacity": 2}).to_have_loss(retained=2, dropped=0, capacity=2)
