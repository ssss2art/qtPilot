"""Waiting for a Qt signal to fire, without losing one that fired first.

The probe reports each emission on a subscribed signal as a
``qtpilot.signalEmitted`` notification. Waiting only from the moment a wait is
issued would miss the common case: subscribe, perform the action (a deferred
invoke, a queued click), then wait -- by which time the signal may already have
fired. So a SignalWaiter buffers every emission per subscription from the
moment it is attached, and a wait consumes the oldest buffered one first.

It has to wait here rather than in the probe: a probe handler that blocks spins
a nested event loop inside the WebSocket dispatch, which is exactly the
re-entrancy the deferred modes exist to avoid.
"""

from __future__ import annotations

import asyncio
from collections import deque
from collections.abc import Callable
from dataclasses import dataclass
from typing import Any, Protocol
from weakref import WeakKeyDictionary

from qtpilot.result import Err, Ok, Result

SIGNAL_EMITTED = "qtpilot.signalEmitted"


@dataclass(frozen=True, slots=True)
class SignalEmission:
    """One emission of a subscribed signal."""

    subscription_id: str
    object_id: str
    signal: str
    arguments: tuple[Any, ...]

    @classmethod
    def from_notification(cls, params: dict[str, Any]) -> SignalEmission:
        return cls(
            subscription_id=str(params.get("subscriptionId", "")),
            object_id=str(params.get("objectId", "")),
            signal=str(params.get("signal", "")),
            arguments=tuple(params.get("arguments") or ()),
        )

    def to_dict(self) -> dict[str, Any]:
        return {
            "emitted": True,
            "subscriptionId": self.subscription_id,
            "objectId": self.object_id,
            "signal": self.signal,
            "arguments": list(self.arguments),
        }


@dataclass(frozen=True, slots=True)
class SignalWaitTimeout:
    """Nothing was emitted on the subscription within the timeout."""

    subscription_id: str
    timeout: float

    def to_dict(self) -> dict[str, Any]:
        return {
            "emitted": False,
            "timedOut": True,
            "subscriptionId": self.subscription_id,
            "timeout": self.timeout,
        }


SignalWaitResult = Result[SignalEmission, SignalWaitTimeout]


class NotificationSource(Protocol):
    def add_notification_handler(
        self, handler: Callable[[str, dict[str, Any]], None]
    ) -> None: ...


class SignalWaiter:
    """Buffers emissions per subscription and hands them to waits in order."""

    def __init__(self, max_buffered: int = 256) -> None:
        self._max_buffered = max_buffered
        self._buffered: dict[str, deque[SignalEmission]] = {}
        self._waiting: dict[str, deque[asyncio.Future[SignalEmission]]] = {}

    def handle_notification(self, method: str, params: dict[str, Any]) -> None:
        if method != SIGNAL_EMITTED:
            return
        emitted = SignalEmission.from_notification(params)
        waiting = self._waiting.get(emitted.subscription_id)
        while waiting:
            future = waiting.popleft()
            if not future.done():
                future.set_result(emitted)
                return
        # A noisy signal nobody is waiting on keeps only its latest emissions.
        self._buffered.setdefault(
            emitted.subscription_id, deque(maxlen=self._max_buffered)
        ).append(emitted)

    async def wait(self, subscription_id: str, timeout: float) -> SignalWaitResult:
        buffered = self._buffered.get(subscription_id)
        if buffered:
            return Ok(buffered.popleft())

        future: asyncio.Future[SignalEmission] = asyncio.get_running_loop().create_future()
        self._waiting.setdefault(subscription_id, deque()).append(future)
        try:
            return Ok(await asyncio.wait_for(future, timeout))
        except TimeoutError:
            return Err(SignalWaitTimeout(subscription_id, timeout))
        finally:
            waiting = self._waiting.get(subscription_id)
            if waiting and future in waiting:
                waiting.remove(future)

    def forget(self, subscription_id: str) -> None:
        """Drop everything buffered for a subscription that has ended."""
        self._buffered.pop(subscription_id, None)


_waiters: WeakKeyDictionary[Any, SignalWaiter] = WeakKeyDictionary()


def signal_waiter_for(source: NotificationSource) -> SignalWaiter:
    """The waiter listening on ``source``, attached on first use.

    Call it before subscribing, so the first emission is already being buffered.
    """
    waiter = _waiters.get(source)
    if waiter is None:
        waiter = SignalWaiter()
        source.add_notification_handler(waiter.handle_notification)
        _waiters[source] = waiter
    return waiter
