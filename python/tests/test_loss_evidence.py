"""Unknown and cross-session counters cannot establish complete replay evidence."""

from __future__ import annotations

import pytest

from qtpilot.evidence import BufferEvidence


def test_known_zero_and_unknown_counters_are_distinct() -> None:
    known = BufferEvidence.from_wire("probe", "session-1", {"dropped": 0, "capacity": 2, "queued": 0})
    assert known.known
    assert known.to_dict()["dropped"] == 0
    unknown = BufferEvidence.from_wire("probe", None, None)
    assert not unknown.known
    assert unknown.to_dict()["dropped"] is None


@pytest.mark.parametrize("payload", [
    {"dropped": False, "capacity": 2, "queued": 0},
    {"dropped": -1, "capacity": 2, "queued": 0},
    {"dropped": 0, "capacity": 0, "queued": 0},
    {"dropped": 0, "capacity": 2, "queued": 3},
    {"dropped": 0},
])
def test_invalid_wire_counters_are_unknown(payload: dict) -> None:
    assert not BufferEvidence.from_wire("probe", "session-1", payload).known


def test_same_session_delta_counts_only_new_loss() -> None:
    before = BufferEvidence.from_wire("probe", "session-1", {"dropped": 3, "capacity": 2, "queued": 0})
    after = BufferEvidence.from_wire("probe", "session-1", {"dropped": 5, "capacity": 2, "queued": 0})
    assert after.loss_since(before).unwrap() == 2


@pytest.mark.parametrize("epoch,dropped", [("session-2", 3), ("session-1", 1), (None, 3)])
def test_reset_or_unknown_session_cannot_prove_a_lossless_interval(epoch: str | None, dropped: int) -> None:
    before = BufferEvidence.from_wire("probe", "session-1", {"dropped": 3, "capacity": 2, "queued": 0})
    after = BufferEvidence.from_wire("probe", epoch, {"dropped": dropped, "capacity": 2, "queued": 0})
    assert after.loss_since(before).is_err()
