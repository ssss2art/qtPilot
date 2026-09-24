"""Waiting for a Qt signal to fire, without losing one that fired first.

The probe reports each emission on a subscribed signal as a
``qtpilot.signalEmitted`` notification. Waiting only from the moment a wait is
issued would miss the common case: subscribe, perform the action (a deferred
invoke, a queued click), then wait -- by which time the signal may already have
fired. So a SignalWaiter buffers every emission per subscription from the
moment it is attached, and a wait consumes the oldest buffered one first --
unless it asks for a fresh one.

It has to wait here rather than in the probe: a probe handler that blocks spins
a nested event loop inside the WebSocket dispatch, which is exactly the
re-entrancy the deferred modes exist to avoid.
"""

from __future__ import annotations

import asyncio
import time
from collections import OrderedDict, deque
from collections.abc import Callable
from dataclasses import dataclass
from typing import Any, Literal, Protocol
from weakref import WeakKeyDictionary, ref

from qtpilot.result import Err, Ok, Result

SIGNAL_EMITTED = "qtpilot.signalEmitted"

# How often a wait looks at whether the probe is still connected.
_CONNECTION_CHECK_S = 0.25


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
class SignalWaitFailure:
    """Why a wait ended without an emission."""

    subscription_id: str
    reason: Literal["timedOut", "disconnected", "unknownSubscription"]
    timeout: float

    def to_dict(self) -> dict[str, Any]:
        return {
            "emitted": False,
            self.reason: True,
            "subscriptionId": self.subscription_id,
            "timeout": self.timeout,
        }


SignalWaitResult = Result[SignalEmission, SignalWaitFailure]


class NotificationSource(Protocol):
    @property
    def is_connected(self) -> bool: ...

    def add_notification_handler(
        self, handler: Callable[[str, dict[str, Any]], None]
    ) -> None: ...


class SignalWaiter:
    """Buffers emissions per subscription and hands them to waits in order.

    Every emission is buffered, including one that lands before its subscription
    is tracked -- the probe may deliver it before the subscribe call returns.
    Buffers for subscriptions nobody tracks are kept only for the most recent
    few, so another client's subscriptions cannot grow them without bound.
    """

    def __init__(
        self,
        max_buffered: int = 256,
        max_untracked: int = 16,
        is_connected: Callable[[], bool] = lambda: True,
    ) -> None:
        self._max_buffered = max_buffered
        self._max_untracked = max_untracked
        self._is_connected = is_connected
        self._tracked: set[str] = set()
        self._buffered: OrderedDict[str, deque[SignalEmission]] = OrderedDict()
        self._dropped: dict[str, int] = {}
        self._waiting: dict[str, deque[asyncio.Future[SignalEmission]]] = {}

    def track(self, subscription_id: str) -> None:
        """Start treating a subscription as live: waits on it are accepted."""
        self._tracked.add(subscription_id)

    def forget(self, subscription_id: str) -> None:
        """End a subscription: drop what was buffered, and refuse waits on it."""
        self._tracked.discard(subscription_id)
        self._buffered.pop(subscription_id, None)
        self._dropped.pop(subscription_id, None)

    def pending(self, subscription_id: str) -> int:
        """Emissions buffered for a subscription and not yet consumed by a wait."""
        return len(self._buffered.get(subscription_id, ()))

    def dropped(self, subscription_id: str) -> int:
        """Emissions discarded because the buffer was full."""
        return self._dropped.get(subscription_id, 0)

    def buffered_subscriptions(self) -> int:
        return len(self._buffered)

    def handle_notification(self, method: str, params: dict[str, Any]) -> None:
        if method != SIGNAL_EMITTED:
            return
        emitted = SignalEmission.from_notification(params)
        sub = emitted.subscription_id
        waiting = self._waiting.get(sub)
        while waiting:
            future = waiting.popleft()
            if not future.done():
                future.set_result(emitted)
                return

        buffer = self._buffered.get(sub)
        if buffer is None:
            buffer = self._buffered[sub] = deque(maxlen=self._max_buffered)
            self._evict_untracked()
        if len(buffer) == buffer.maxlen:
            self._dropped[sub] = self._dropped.get(sub, 0) + 1
        buffer.append(emitted)

    async def wait(
        self, subscription_id: str, timeout: float, fresh: bool = False
    ) -> SignalWaitResult:
        """The next emission on a subscription, or why there was none in time.

        With ``fresh``, emissions buffered before this call are discarded first,
        so only one the caller's latest action caused can satisfy the wait.
        """
        buffered = self._buffered.get(subscription_id)
        if fresh and buffered:
            buffered.clear()
        if buffered:
            return Ok(buffered.popleft())
        if subscription_id not in self._tracked:
            return Err(SignalWaitFailure(subscription_id, "unknownSubscription", timeout))

        future: asyncio.Future[SignalEmission] = asyncio.get_running_loop().create_future()
        self._waiting.setdefault(subscription_id, deque()).append(future)
        deadline = time.monotonic() + timeout
        try:
            # asyncio.wait, not wait_for: a timeout must never cancel a future the
            # dispatcher has just resolved in the same pass, or that emission is lost.
            while not future.done():
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    break
                await asyncio.wait({future}, timeout=min(remaining, _CONNECTION_CHECK_S))
                if not future.done() and not self._is_connected():
                    return Err(SignalWaitFailure(subscription_id, "disconnected", timeout))
            if future.done():
                return Ok(future.result())
            return Err(SignalWaitFailure(subscription_id, "timedOut", timeout))
        finally:
            waiting = self._waiting.get(subscription_id)
            if waiting and future in waiting:
                waiting.remove(future)
            if not future.done():
                future.cancel()

    def _evict_untracked(self) -> None:
        untracked = [sub for sub in self._buffered if sub not in self._tracked]
        for sub in untracked[: max(0, len(untracked) - self._max_untracked)]:
            del self._buffered[sub]
            self._dropped.pop(sub, None)


_waiters: WeakKeyDictionary[NotificationSource, SignalWaiter] = WeakKeyDictionary()


def signal_waiter_for(source: NotificationSource) -> SignalWaiter:
    """The waiter listening on ``source``, attached on first use.

    Call it before subscribing, so the first emission is already being buffered.
    """
    waiter = _waiters.get(source)
    if waiter is None:
        weak_source = ref(source)  # a strong one would keep its own key alive

        def still_connected() -> bool:
            live = weak_source()
            return live is not None and live.is_connected

        waiter = SignalWaiter(is_connected=still_connected)
        source.add_notification_handler(waiter.handle_notification)
        _waiters[source] = waiter
    return waiter


def subscription_id_of(response: dict[str, Any]) -> str:
    """The subscription id in a ``qt.signals.subscribe`` response.

    The probe wraps native results as ``{"result": {...}, "meta": {...}}``.
    """
    inner = response.get("result", response)
    subscription = inner.get("subscriptionId") if isinstance(inner, dict) else None
    if not isinstance(subscription, str) or not subscription:
        raise ValueError(f"qt.signals.subscribe returned no subscriptionId: {response!r}")
    return subscription
