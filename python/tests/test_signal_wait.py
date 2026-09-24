"""Blocking until a Qt signal fires, without losing one that fires first."""

from __future__ import annotations

import asyncio
import time
from collections.abc import Callable
from typing import Any
from unittest.mock import AsyncMock, MagicMock, PropertyMock, patch

import pytest
from fastmcp import Client

from qtpilot.connection import ProbeConnection
from qtpilot.fluent import expect_signal_wait
from qtpilot.server import create_server
from qtpilot.signal_wait import SIGNAL_EMITTED, SignalWaiter, signal_waiter_for

NotificationHandler = Callable[[str, dict[str, Any]], None]


def emission(sub: str, signal: str = "clicked", *arguments: object) -> dict[str, Any]:
    return {
        "subscriptionId": sub,
        "objectId": "form/okButton",
        "signal": signal,
        "arguments": list(arguments),
    }


def tracked(*subs: str, **kwargs: Any) -> SignalWaiter:
    waiter = SignalWaiter(**kwargs)
    for sub in subs:
        waiter.track(sub)
    return waiter


# -- SignalWaiter -------------------------------------------------------------


@pytest.mark.asyncio
async def test_wait_returns_an_emission_that_arrived_before_the_wait() -> None:
    waiter = tracked("sub_1")
    waiter.handle_notification(SIGNAL_EMITTED, emission("sub_1", "toggled", True))

    result = await waiter.wait("sub_1", timeout=1.0)

    expect_signal_wait(result).to_emit(signal="toggled", arguments=[True])


@pytest.mark.asyncio
async def test_wait_resolves_when_the_emission_arrives_while_waiting() -> None:
    waiter = tracked("sub_1")
    pending = asyncio.create_task(waiter.wait("sub_1", timeout=1.0))
    await asyncio.sleep(0)

    waiter.handle_notification(SIGNAL_EMITTED, emission("sub_1"))

    expect_signal_wait(await pending).to_emit(signal="clicked")


@pytest.mark.asyncio
async def test_wait_times_out_as_an_err_not_an_exception() -> None:
    result = await tracked("sub_1").wait("sub_1", timeout=0.05)

    expect_signal_wait(result).to_time_out()


@pytest.mark.asyncio
async def test_wait_ignores_other_subscriptions_and_other_notifications() -> None:
    waiter = tracked("sub_1", "sub_2")
    waiter.handle_notification(SIGNAL_EMITTED, emission("sub_2"))
    waiter.handle_notification("qtpilot.objectAdded", {"subscriptionId": "sub_1"})

    expect_signal_wait(await waiter.wait("sub_1", timeout=0.05)).to_time_out()
    expect_signal_wait(await waiter.wait("sub_2", timeout=0.05)).to_emit(signal="clicked")


@pytest.mark.asyncio
async def test_each_wait_consumes_one_emission_in_order_and_says_how_many_remain() -> None:
    waiter = tracked("sub_1")
    for value in (1, 2):
        waiter.handle_notification(SIGNAL_EMITTED, emission("sub_1", "valueChanged", value))

    expect_signal_wait(await waiter.wait("sub_1", timeout=0.1)).to_emit(arguments=[1])
    assert waiter.pending("sub_1") == 1
    expect_signal_wait(await waiter.wait("sub_1", timeout=0.1)).to_emit(arguments=[2])
    assert waiter.pending("sub_1") == 0


@pytest.mark.asyncio
async def test_a_fresh_wait_ignores_what_fired_before_it() -> None:
    # subscribe once, then act -> wait in a loop: a wait must be able to insist on
    # an emission the latest action caused, not one left over from the last.
    waiter = tracked("sub_1")
    waiter.handle_notification(SIGNAL_EMITTED, emission("sub_1", "textChanged", "stale"))

    expect_signal_wait(await waiter.wait("sub_1", timeout=0.05, fresh=True)).to_time_out()


@pytest.mark.asyncio
async def test_a_noisy_subscription_keeps_its_latest_emissions_and_counts_the_rest() -> None:
    waiter = tracked("sub_1", max_buffered=2)
    for value in (1, 2, 3):
        waiter.handle_notification(SIGNAL_EMITTED, emission("sub_1", "valueChanged", value))

    expect_signal_wait(await waiter.wait("sub_1", timeout=0.1)).to_emit(arguments=[2])
    assert waiter.dropped("sub_1") == 1


@pytest.mark.asyncio
async def test_an_emission_landing_on_the_timeout_is_not_lost() -> None:
    # The emission is delivered, then the loop is held past the deadline, so the
    # wait wakes with both its result and its timeout due.
    waiter = tracked("sub_1")
    loop = asyncio.get_running_loop()
    pending = asyncio.create_task(waiter.wait("sub_1", timeout=0.05))
    await asyncio.sleep(0)

    loop.call_soon(waiter.handle_notification, SIGNAL_EMITTED, emission("sub_1"))
    loop.call_soon(time.sleep, 0.1)

    expect_signal_wait(await pending).to_emit(signal="clicked")


@pytest.mark.asyncio
async def test_forget_discards_what_was_buffered() -> None:
    waiter = tracked("sub_1")
    waiter.handle_notification(SIGNAL_EMITTED, emission("sub_1"))

    waiter.forget("sub_1")

    expect_signal_wait(await waiter.wait("sub_1", timeout=0.05)).to_fail_as("unknownSubscription")


@pytest.mark.asyncio
async def test_waiting_on_a_subscription_nobody_made_fails_at_once() -> None:
    result = await asyncio.wait_for(SignalWaiter().wait("sub_typo", timeout=30.0), timeout=1.0)

    expect_signal_wait(result).to_fail_as("unknownSubscription")


@pytest.mark.asyncio
async def test_a_wait_ends_when_the_probe_disconnects() -> None:
    connected = True
    waiter = tracked("sub_1", is_connected=lambda: connected)
    pending = asyncio.create_task(waiter.wait("sub_1", timeout=30.0))
    await asyncio.sleep(0.05)

    connected = False

    result = await asyncio.wait_for(pending, timeout=2.0)
    expect_signal_wait(result).to_fail_as("disconnected")


@pytest.mark.asyncio
async def test_emissions_for_subscriptions_nobody_tracks_are_bounded() -> None:
    waiter = SignalWaiter(max_untracked=2)
    for n in range(5):
        waiter.handle_notification(SIGNAL_EMITTED, emission(f"sub_{n}"))

    assert waiter.buffered_subscriptions() == 2


@pytest.mark.asyncio
async def test_the_real_connection_delivers_through_its_dispatcher() -> None:
    with patch.object(ProbeConnection, "is_connected", new_callable=PropertyMock, return_value=True):
        probe = ProbeConnection("ws://localhost:9222")
        waiter = signal_waiter_for(probe)
        waiter.track("sub_1")
        dispatcher = asyncio.create_task(probe._notification_dispatcher())
        try:
            probe._notification_queue.put_nowait((SIGNAL_EMITTED, emission("sub_1")))
            expect_signal_wait(await waiter.wait("sub_1", timeout=1.0)).to_emit(signal="clicked")
        finally:
            dispatcher.cancel()


# -- qt_signals_wait tool -----------------------------------------------------


def envelope(result: dict[str, Any]) -> dict[str, Any]:
    """The probe wraps every native result: {"result": ..., "meta": ...}."""
    return {"result": result, "meta": {"timestamp": 0}}


class FakeProbe:
    """A probe whose subscriptions emit on cue, through the real notification path."""

    def __init__(self, emit_on_subscribe: bool, unsubscribe_fails: bool = False) -> None:
        self.handlers: list[NotificationHandler] = []
        self.emit_on_subscribe = emit_on_subscribe
        self.unsubscribe_fails = unsubscribe_fails
        self.is_connected = True
        self.ws_url = "ws://localhost:9222"
        self.calls: list[tuple[str, dict[str, Any]]] = []
        self.add_notification_handler = MagicMock(side_effect=self.handlers.append)
        self.call = AsyncMock(side_effect=self._call)

    async def _call(self, method: str, params: dict[str, Any], **_: Any) -> dict[str, Any]:
        self.calls.append((method, params))
        if method == "qt.signals.subscribe":
            # Emitted before the caller has even seen the subscription id: the
            # race a wait that starts listening late would lose.
            if self.emit_on_subscribe:
                self.push(emission("sub_7", params["signal"]))
            return envelope({"subscriptionId": "sub_7"})
        if method == "qt.signals.unsubscribe" and self.unsubscribe_fails:
            raise ConnectionError("gone")
        return envelope({"ok": True})

    def push(self, params: dict[str, Any]) -> None:
        for handler in list(self.handlers):
            handler(SIGNAL_EMITTED, params)

    def methods(self) -> list[str]:
        return [method for method, _ in self.calls]


async def _tool(probe: FakeProbe, name: str, arguments: dict[str, Any]) -> dict[str, Any]:
    with patch("qtpilot.server.require_probe", return_value=probe):
        async with Client(create_server(mode="native")) as client:
            result = await client.call_tool(name, arguments)
    return result.data


@pytest.mark.asyncio
async def test_wait_tool_subscribes_waits_and_unsubscribes() -> None:
    probe = FakeProbe(emit_on_subscribe=True)

    result = await _tool(
        probe, "qt_signals_wait", {"objectId": "form/okButton", "signal": "clicked"}
    )

    assert result["emitted"] is True
    assert result["signal"] == "clicked"
    assert probe.methods() == ["qt.signals.subscribe", "qt.signals.unsubscribe"]


@pytest.mark.asyncio
async def test_wait_tool_unsubscribes_after_a_timeout_too() -> None:
    probe = FakeProbe(emit_on_subscribe=False)

    result = await _tool(
        probe,
        "qt_signals_wait",
        {"objectId": "form/okButton", "signal": "clicked", "timeout": 0.05},
    )

    assert result["emitted"] is False
    assert result["timedOut"] is True
    assert probe.methods() == ["qt.signals.subscribe", "qt.signals.unsubscribe"]


@pytest.mark.asyncio
async def test_a_failed_unsubscribe_does_not_hide_the_emission() -> None:
    probe = FakeProbe(emit_on_subscribe=True, unsubscribe_fails=True)

    result = await _tool(
        probe, "qt_signals_wait", {"objectId": "form/okButton", "signal": "clicked"}
    )

    assert result["emitted"] is True
    assert result["unsubscribed"] is False


@pytest.mark.asyncio
async def test_wait_on_an_existing_subscription_sees_an_emission_from_before_the_wait() -> None:
    # subscribe -> act -> wait: the signal may fire before the wait is issued.
    probe = FakeProbe(emit_on_subscribe=True)
    with patch("qtpilot.server.require_probe", return_value=probe):
        async with Client(create_server(mode="native")) as client:
            subscribed = await client.call_tool(
                "qt_signals_subscribe", {"objectId": "form/okButton", "signal": "clicked"}
            )
            waited = await client.call_tool(
                "qt_signals_wait",
                {"subscriptionId": subscribed.data["result"]["subscriptionId"], "timeout": 0.5},
            )

    assert waited.data["emitted"] is True
    assert waited.data["pending"] == 0
    # The caller owns a subscription it made itself; waiting must not end it.
    assert probe.methods() == ["qt.signals.subscribe"]


@pytest.mark.asyncio
async def test_unsubscribing_ends_the_subscription_for_waits_too() -> None:
    probe = FakeProbe(emit_on_subscribe=True)
    with patch("qtpilot.server.require_probe", return_value=probe):
        async with Client(create_server(mode="native")) as client:
            await client.call_tool(
                "qt_signals_subscribe", {"objectId": "form/okButton", "signal": "clicked"}
            )
            await client.call_tool("qt_signals_unsubscribe", {"subscriptionId": "sub_7"})
            waited = await client.call_tool(
                "qt_signals_wait",
                {"subscriptionId": "sub_7", "timeout": 0.05},
                raise_on_error=False,
            )

    assert waited.is_error


@pytest.mark.asyncio
@pytest.mark.parametrize("timeout", [0, -1, 301, float("inf")])
async def test_wait_tool_rejects_an_unreasonable_timeout(timeout: float) -> None:
    probe = FakeProbe(emit_on_subscribe=False)
    with patch("qtpilot.server.require_probe", return_value=probe):
        async with Client(create_server(mode="native")) as client:
            result = await client.call_tool(
                "qt_signals_wait",
                {"objectId": "form/okButton", "signal": "clicked", "timeout": timeout},
                raise_on_error=False,
            )

    assert result.is_error
    assert probe.calls == []


@pytest.mark.asyncio
async def test_wait_tool_needs_a_subscription_or_an_object_and_signal() -> None:
    probe = FakeProbe(emit_on_subscribe=False)
    with patch("qtpilot.server.require_probe", return_value=probe):
        async with Client(create_server(mode="native")) as client:
            result = await client.call_tool(
                "qt_signals_wait", {"objectId": "form/okButton"}, raise_on_error=False
            )

    assert result.is_error
    assert probe.calls == []
