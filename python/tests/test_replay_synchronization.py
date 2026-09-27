"""A broken completion barrier must never be mistaken for an unavailable feature."""

from __future__ import annotations

import asyncio

import pytest

from qtpilot.connection import ProbeError
from qtpilot.fluent import expect_replay
from qtpilot.replay import Action, Scenario, Step, run_scenario
from tests.test_replay_driver import FakeProbe


class BarrierProbe(FakeProbe):
    def __init__(self, failure: Exception) -> None:
        super().__init__()
        self.failure = failure

    async def call(self, method: str, params: dict | None = None, timeout: float | None = None) -> dict:
        self.calls.append((method, params or {}))
        if method == "qt.sync":
            raise self.failure
        return {"ok": True}


def two_actions() -> Scenario:
    return Scenario(steps=[Step(index=0),
        Step(index=1, action=Action("qt.ui.click", {"objectId": "first"})),
        Step(index=2, action=Action("qt.ui.click", {"objectId": "must-not-run"})),
    ])


@pytest.mark.asyncio
@pytest.mark.parametrize("failure", [
    ProbeError("not implemented", code=-32601), ProbeError("handler failed", code=-32000),
    TimeoutError("barrier timed out"), ConnectionError("disconnected"),
], ids=["unsupported", "handler-error", "timeout", "disconnected"])
async def test_required_barrier_failure_stops_before_next_action(failure: Exception) -> None:
    probe = BarrierProbe(failure)
    result = await run_scenario(two_actions(), probe, settle=0)
    expect_replay(result).to_abort_at(1, "qt.sync").to_have_driven(1)
    assert result.failure_kind == "synchronization"
    assert probe.calls[-1] == ("qt.sync", {}), "Replay drove past a failed barrier"
    assert probe.handlers == []


@pytest.mark.asyncio
async def test_explicitly_disabled_sync_does_not_call_the_barrier() -> None:
    probe = BarrierProbe(ProbeError("unsupported", code=-32601))
    result = await run_scenario(two_actions(), probe, settle=0, sync=False)
    expect_replay(result).to_pass().to_have_driven(2)
    assert all(method != "qt.sync" for method, _ in probe.calls)


@pytest.mark.asyncio
async def test_cancellation_detaches_collector_and_never_drives_next_action() -> None:
    entered = asyncio.Event()

    class PendingBarrier(FakeProbe):
        async def call(self, method: str, params: dict | None = None, timeout: float | None = None) -> dict:
            self.calls.append((method, params or {}))
            if method == "qt.sync":
                entered.set()
                await asyncio.Future()
            return {"ok": True}

    probe = PendingBarrier()
    task = asyncio.create_task(run_scenario(two_actions(), probe, settle=0))
    await asyncio.wait_for(entered.wait(), timeout=1)
    task.cancel()
    with pytest.raises(asyncio.CancelledError):
        await task
    assert probe.handlers == []
    assert len([method for method, _ in probe.calls if method == "qt.ui.click"]) == 1
