"""Transport loss is measured in one admitted session, never across reconnects."""

from __future__ import annotations

from unittest.mock import AsyncMock, patch

import pytest

from qtpilot.connection import ProbeConnection, ProbeError
from tests.conftest import MockWebSocket


@pytest.mark.asyncio
async def test_disconnected_evidence_is_unknown() -> None:
    evidence = await ProbeConnection("ws://127.0.0.1:9222").loss_evidence()
    assert all(not counter.known for counter in evidence)


@pytest.mark.asyncio
async def test_connected_evidence_preserves_native_and_python_scopes(mock_probe: tuple[ProbeConnection, MockWebSocket]) -> None:
    probe, _ = mock_probe
    with patch.object(probe, "call", AsyncMock(return_value={
        "sessionId": "native-session", "notifications": {"dropped": 4, "capacity": 10, "queued": 2},
    })):
        native, controller = await probe.loss_evidence()
    assert native.known and native.epoch == "native-session" and native.dropped == 4
    assert controller.known and controller.epoch != native.epoch and controller.dropped == 0


@pytest.mark.asyncio
async def test_unsupported_native_counter_is_unknown_without_erasing_client_loss(mock_probe: tuple[ProbeConnection, MockWebSocket]) -> None:
    probe, _ = mock_probe
    probe._notification_drops = 3
    with patch.object(probe, "call", AsyncMock(side_effect=ProbeError("unknown method", code=-32601))):
        native, controller = await probe.loss_evidence()
    assert not native.known
    assert controller.dropped == 3


@pytest.mark.asyncio
@pytest.mark.parametrize("payload", [None, [], "unavailable"])
async def test_malformed_native_diagnostics_are_unknown(mock_probe: tuple[ProbeConnection, MockWebSocket], payload: object) -> None:
    probe, _ = mock_probe
    with patch.object(probe, "call", AsyncMock(return_value=payload)):
        native, controller = await probe.loss_evidence()
    assert not native.known
    assert controller.known


@pytest.mark.asyncio
async def test_reconnect_discards_previous_session_queue_and_changes_scope() -> None:
    connection = ProbeConnection("ws://127.0.0.1:9222")
    with patch("qtpilot.connection.connect", AsyncMock(side_effect=[MockWebSocket(), MockWebSocket()])):
        await connection.connect()
        with patch.object(connection, "call", AsyncMock(return_value={})):
            _, before = await connection.loss_evidence()
        await connection.disconnect()
        connection._notification_queue.put_nowait(("stale", {}))
        connection._notification_drops = 5
        try:
            await connection.connect()
            with patch.object(connection, "call", AsyncMock(return_value={})):
                _, after = await connection.loss_evidence()
            assert after.epoch != before.epoch
            assert after.dropped == 0 and after.queued == 0
            assert after.loss_since(before).is_err()
        finally:
            await connection.disconnect()


@pytest.mark.asyncio
async def test_reconnect_closes_the_previous_transport_and_dispatcher() -> None:
    connection = ProbeConnection("ws://127.0.0.1:9222")
    previous = MockWebSocket()
    with patch("qtpilot.connection.connect", AsyncMock(side_effect=[previous, MockWebSocket()])):
        await connection.connect()
        receiver, dispatcher = connection._recv_task, connection._notification_task
        try:
            await connection.connect()
            assert previous._closed, "Reconnect left the previous transport alive"
            assert receiver is not None and receiver.done()
            assert dispatcher is not None and dispatcher.done()
        finally:
            await connection.disconnect()
