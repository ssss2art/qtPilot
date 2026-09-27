"""Qt owns emission/lifetime; asyncio owns bounded waiting and cleanup."""

from __future__ import annotations

import asyncio
from collections.abc import Callable

import pytest

from qtpilot.checkpoints import CheckpointFailure, arm_checkpoint
from qtpilot.replay_contract import Checkpoint, Json


class SignalProbe:
    is_connected = True

    def __init__(self) -> None:
        self.handlers: list[Callable[[str, dict[str, Json]], None]] = []
        self.subscriptions: dict[str, str] = {}
        self.calls: list[str] = []
        self.next_id = 0
        self.exclusive = True
        self.on_subscribe: Callable[[str], None] = lambda sub: None
        self.on_unsubscribe: Callable[[], None] = lambda: None

    async def call(self, method: str, params: dict[str, Json] | None = None,
                   timeout: float | None = None) -> Json:
        self.calls.append(method)
        params = params or {}
        if method == "qt.signals.subscribe":
            self.next_id += 1
            sub = f"sub_{self.next_id}"
            self.subscriptions[sub] = str(params["signal"])
            self.on_subscribe(sub)
            return {"result": {"subscriptionId": sub, "exclusive": self.exclusive}}
        if method == "qt.signals.unsubscribe":
            self.subscriptions.pop(str(params["subscriptionId"]))
            self.on_unsubscribe()
        return {"result": {"synced": True}}

    def add_notification_handler(self, handler: Callable[[str, dict[str, Json]], None]) -> None:
        self.handlers.append(handler)

    def remove_notification_handler(self, handler: Callable[[str, dict[str, Json]], None]) -> None:
        self.handlers.remove(handler)

    def emit(self, signal: str, arguments: list[Json] | None = None) -> None:
        for sub, name in list(self.subscriptions.items()):
            if name == signal:
                self.emit_for(sub, name, arguments or [])

    def emit_for(self, sub: str, name: str, arguments: list[Json]) -> None:
        for handler in list(self.handlers):
            handler("qtpilot.signalEmitted", {"subscriptionId": sub, "objectId": "form",
                    "signal": name, "arguments": arguments})


@pytest.mark.asyncio
@pytest.mark.parametrize("queued", [False, True], ids=["immediate", "queued"])
async def test_armed_checkpoint_keeps_completion_before_wait(queued: bool) -> None:
    probe = SignalProbe()
    async with asyncio.timeout(1):
        async with arm_checkpoint(probe, Checkpoint("form", "saved", ("request-7",))) as pending:
            assert set(probe.subscriptions.values()) == {"destroyed", "saved"}
            if queued:
                asyncio.get_running_loop().call_soon(probe.emit, "saved", ["request-7"])
            else:
                probe.emit("saved", ["request-7"])
            result = await pending.wait()
            assert result["arguments"] == ["request-7"]
    assert not probe.handlers and not probe.subscriptions


@pytest.mark.asyncio
async def test_pre_action_emission_and_wrong_correlation_do_not_complete_checkpoint() -> None:
    probe = SignalProbe()
    probe.on_subscribe = lambda sub: probe.emit("saved", ["request-7"])
    async with arm_checkpoint(probe, Checkpoint("form", "saved", ("request-7",))) as pending:
        probe.emit("saved", ["another-request"])
        with pytest.raises(TimeoutError):
            async with asyncio.timeout(0.01):
                await pending.wait()
    assert not probe.handlers and not probe.subscriptions


@pytest.mark.asyncio
@pytest.mark.parametrize("fault", ["destroyed", "disconnected", "overflow"])
async def test_checkpoint_cannot_complete_with_missing_or_lost_evidence(fault: str) -> None:
    probe = SignalProbe()
    with pytest.raises(CheckpointFailure, match=fault):
        async with arm_checkpoint(probe, Checkpoint("form", "saved"), capacity=1) as pending:
            if fault == "destroyed":
                probe.emit("destroyed")
            elif fault == "disconnected":
                probe.is_connected = False
            else:
                probe.emit("saved")
                probe.emit("saved")
            await pending.wait()
    assert not probe.handlers and not probe.subscriptions


@pytest.mark.asyncio
async def test_cancellation_releases_both_owned_qt_subscriptions_and_handler() -> None:
    probe = SignalProbe()
    armed = asyncio.Event()

    async def run() -> None:
        async with arm_checkpoint(probe, Checkpoint("form", "saved")) as pending:
            armed.set()
            await pending.wait()

    task = asyncio.create_task(run())
    await armed.wait()
    task.cancel()
    with pytest.raises(asyncio.CancelledError):
        await task
    assert not probe.handlers and not probe.subscriptions


@pytest.mark.asyncio
async def test_old_probe_cannot_claim_exclusive_ownership() -> None:
    probe = SignalProbe()
    probe.exclusive = False
    with pytest.raises(CheckpointFailure, match="exclusive"):
        async with arm_checkpoint(probe, Checkpoint("form", "saved")):
            pytest.fail("Probe did not confirm independent Qt ownership")
    assert not probe.handlers
    assert "qt.signals.unsubscribe" not in probe.calls, "Must not remove an unowned subscription"
