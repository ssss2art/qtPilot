"""Owned Qt signal checkpoints coordinated with asyncio, without nested Qt waits."""

from __future__ import annotations

import asyncio
import json
from collections.abc import AsyncIterator, Callable
from contextlib import AsyncExitStack, asynccontextmanager
from dataclasses import dataclass
from typing import Protocol

from qtpilot.connection import ProbeError
from qtpilot.replay_contract import Checkpoint, Json, incomplete_value


class CheckpointProbe(Protocol):
    @property
    def is_connected(self) -> bool: ...
    async def call(self, method: str, params: dict[str, Json] | None = None,
                   timeout: float | None = None) -> Json: ...
    def add_notification_handler(self, handler: Callable[[str, dict[str, Json]], None]) -> None: ...
    def remove_notification_handler(self, handler: Callable[[str, dict[str, Json]], None]) -> None: ...


class CheckpointFailure(Exception):
    pass


@dataclass(frozen=True, slots=True)
class ReceivedSignal:
    sequence: int
    params: dict[str, Json]


class ArmedCheckpoint:
    def __init__(self, probe: CheckpointProbe, checkpoint: Checkpoint, capacity: int) -> None:
        if type(capacity) is not int or capacity < 1:
            raise ValueError("checkpoint capacity must be a positive integer")
        self.probe = probe
        self.checkpoint = checkpoint
        self.queue: asyncio.Queue[ReceivedSignal] = asyncio.Queue(maxsize=capacity)
        self.subscriptions: dict[str, str] = {}
        self.sequence = 0
        self.cursor = 0
        self.dropped = 0

    def receive(self, method: str, params: dict[str, Json]) -> None:
        if method != "qtpilot.signalEmitted":
            return
        # During subscribe, a signal can arrive before its ID is returned. Keep
        # those too; the cursor established before driving discards old emissions.
        if len(self.subscriptions) == 2 and params.get("subscriptionId") not in self.subscriptions:
            return
        self.sequence += 1
        try:
            self.queue.put_nowait(ReceivedSignal(self.sequence, params))
        except asyncio.QueueFull:
            self.dropped += 1

    def before_action(self) -> None:
        while not self.queue.empty():
            received = self.queue.get_nowait()
            self.queue.task_done()
            if self.subscriptions.get(str(received.params.get("subscriptionId"))) == "destroyed":
                raise CheckpointFailure("target destroyed before action")
        self.cursor = self.sequence
        self.check_loss()

    def check_loss(self) -> None:
        if self.dropped:
            raise CheckpointFailure(f"checkpoint buffer overflow: {self.dropped} emissions dropped")

    async def wait(self) -> dict[str, Json]:
        while True:
            self.check_loss()
            if not self.probe.is_connected:
                raise CheckpointFailure("probe disconnected during checkpoint")
            try:
                # A connection check, not a completion heuristic or settling delay.
                async with asyncio.timeout(0.25):
                    received = await self.queue.get()
            except TimeoutError:
                continue
            self.queue.task_done()
            params = received.params
            signal = self.subscriptions.get(str(params.get("subscriptionId")))
            if signal == "destroyed":
                raise CheckpointFailure("checkpoint target destroyed")
            if received.sequence <= self.cursor or signal != self.checkpoint.signal:
                continue
            if params.get("objectId") != self.checkpoint.objectId or params.get("signal") != signal:
                raise CheckpointFailure("unsupported signal identity evidence")
            arguments = params.get("arguments")
            if not isinstance(arguments, list) or incomplete_value(arguments):
                raise CheckpointFailure("unsupported or truncated signal argument evidence")
            expected = self.checkpoint.arguments
            if expected is None or json.dumps(arguments, sort_keys=True, allow_nan=False) == json.dumps(list(expected), sort_keys=True, allow_nan=False):
                return params


async def _unsubscribe(probe: CheckpointProbe, sub: str) -> None:
    try:
        async with asyncio.timeout(5):
            await probe.call("qt.signals.unsubscribe", {"subscriptionId": sub}, timeout=5)
    except (ProbeError, OSError) as exc:
        # The probe releases all subscriptions when the session disconnects.
        if probe.is_connected:
            raise CheckpointFailure(f"subscription cleanup failed: {exc}") from exc


@asynccontextmanager
async def arm_checkpoint(probe: CheckpointProbe, checkpoint: Checkpoint,
                         capacity: int = 256) -> AsyncIterator[ArmedCheckpoint]:
    pending = ArmedCheckpoint(probe, checkpoint, capacity)
    async with AsyncExitStack() as cleanup:
        probe.add_notification_handler(pending.receive)
        cleanup.callback(probe.remove_notification_handler, pending.receive)
        # QObject::destroyed comes first, so deletion cannot fall between the
        # completion subscription and installation of the lifetime observer.
        for signal in ("destroyed", checkpoint.signal):
            response = await probe.call("qt.signals.subscribe", {
                "objectId": checkpoint.objectId, "signal": signal, "exclusive": True,
            }, timeout=5)
            inner = response.get("result") if isinstance(response, dict) else None
            if not isinstance(inner, dict) or inner.get("exclusive") is not True:
                raise CheckpointFailure("probe does not confirm exclusive signal subscriptions")
            sub = inner.get("subscriptionId")
            if not isinstance(sub, str) or not sub or sub in pending.subscriptions:
                raise CheckpointFailure("probe returned an invalid exclusive subscription ID")
            pending.subscriptions[sub] = signal
            cleanup.push_async_callback(_unsubscribe, probe, sub)
        pending.before_action()
        yield pending
        pending.check_loss()
