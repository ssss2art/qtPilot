"""Blocking until a Qt signal fires, without losing one that fires first."""

from __future__ import annotations

import asyncio
from collections.abc import Callable
from typing import Any
from unittest.mock import AsyncMock, MagicMock, patch

import pytest
from fastmcp import Client

from qtpilot.fluent import expect_signal_wait
from qtpilot.server import create_server
from qtpilot.signal_wait import SIGNAL_EMITTED, SignalWaiter

NotificationHandler = Callable[[str, dict[str, Any]], None]


def emission(sub: str, signal: str = "clicked", *arguments: object) -> dict[str, Any]:
    return {
        "subscriptionId": sub,
        "objectId": "form/okButton",
        "signal": signal,
        "arguments": list(arguments),
    }


# -- SignalWaiter -------------------------------------------------------------


@pytest.mark.asyncio
async def test_wait_returns_an_emission_that_arrived_before_the_wait() -> None:
    waiter = SignalWaiter()
    waiter.handle_notification(SIGNAL_EMITTED, emission("sub_1", "toggled", True))

    result = await waiter.wait("sub_1", timeout=1.0)

    expect_signal_wait(result).to_emit(signal="toggled", arguments=[True])


@pytest.mark.asyncio
async def test_wait_resolves_when_the_emission_arrives_while_waiting() -> None:
    waiter = SignalWaiter()
    pending = asyncio.create_task(waiter.wait("sub_1", timeout=1.0))
    await asyncio.sleep(0)

    waiter.handle_notification(SIGNAL_EMITTED, emission("sub_1"))

    expect_signal_wait(await pending).to_emit(signal="clicked")


@pytest.mark.asyncio
async def test_wait_times_out_as_an_err_not_an_exception() -> None:
    result = await SignalWaiter().wait("sub_1", timeout=0.05)

    expect_signal_wait(result).to_time_out()


@pytest.mark.asyncio
async def test_wait_ignores_other_subscriptions_and_other_notifications() -> None:
    waiter = SignalWaiter()
    waiter.handle_notification(SIGNAL_EMITTED, emission("sub_2"))
    waiter.handle_notification("qtpilot.objectAdded", {"subscriptionId": "sub_1"})

    expect_signal_wait(await waiter.wait("sub_1", timeout=0.05)).to_time_out()
    expect_signal_wait(await waiter.wait("sub_2", timeout=0.05)).to_emit(signal="clicked")


@pytest.mark.asyncio
async def test_each_wait_consumes_one_emission_in_order() -> None:
    waiter = SignalWaiter()
    for value in (1, 2):
        waiter.handle_notification(SIGNAL_EMITTED, emission("sub_1", "valueChanged", value))

    expect_signal_wait(await waiter.wait("sub_1", timeout=0.1)).to_emit(arguments=[1])
    expect_signal_wait(await waiter.wait("sub_1", timeout=0.1)).to_emit(arguments=[2])
    expect_signal_wait(await waiter.wait("sub_1", timeout=0.05)).to_time_out()


@pytest.mark.asyncio
async def test_a_noisy_subscription_keeps_only_its_latest_emissions() -> None:
    waiter = SignalWaiter(max_buffered=2)
    for value in (1, 2, 3):
        waiter.handle_notification(SIGNAL_EMITTED, emission("sub_1", "valueChanged", value))

    expect_signal_wait(await waiter.wait("sub_1", timeout=0.1)).to_emit(arguments=[2])


@pytest.mark.asyncio
async def test_forget_discards_what_was_buffered() -> None:
    waiter = SignalWaiter()
    waiter.handle_notification(SIGNAL_EMITTED, emission("sub_1"))

    waiter.forget("sub_1")

    expect_signal_wait(await waiter.wait("sub_1", timeout=0.05)).to_time_out()


# -- qt_signals_wait tool -----------------------------------------------------


class FakeProbe:
    """A probe whose subscriptions emit on cue, through the real notification path."""

    def __init__(self, emit_on_subscribe: bool) -> None:
        self.handlers: list[NotificationHandler] = []
        self.emit_on_subscribe = emit_on_subscribe
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
            return {"subscriptionId": "sub_7"}
        return {"ok": True}

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

    assert result == {"emitted": False, "timedOut": True, "subscriptionId": "sub_7", "timeout": 0.05}
    assert probe.methods() == ["qt.signals.subscribe", "qt.signals.unsubscribe"]


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
                {"subscriptionId": subscribed.data["subscriptionId"], "timeout": 0.5},
            )

    assert waited.data["emitted"] is True
    # The caller owns a subscription it made itself; waiting must not end it.
    assert probe.methods() == ["qt.signals.subscribe"]


@pytest.mark.asyncio
async def test_unsubscribing_discards_emissions_nobody_waited_for() -> None:
    probe = FakeProbe(emit_on_subscribe=True)
    with patch("qtpilot.server.require_probe", return_value=probe):
        async with Client(create_server(mode="native")) as client:
            await client.call_tool(
                "qt_signals_subscribe", {"objectId": "form/okButton", "signal": "clicked"}
            )
            await client.call_tool("qt_signals_unsubscribe", {"subscriptionId": "sub_7"})
            waited = await client.call_tool(
                "qt_signals_wait", {"subscriptionId": "sub_7", "timeout": 0.05}
            )

    assert waited.data["timedOut"] is True


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
