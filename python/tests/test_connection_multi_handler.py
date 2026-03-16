"""Unit tests for ProbeConnection multi-handler and call observer support."""

from __future__ import annotations

import asyncio

import pytest

pytestmark = pytest.mark.asyncio


class TestMultiNotificationHandlers:
    async def test_multiple_handlers_receive_notification(self, mock_probe):
        """All registered handlers receive the same notification."""
        probe, mock_ws = mock_probe
        received_a = []
        received_b = []

        probe.add_notification_handler(lambda m, p: received_a.append(m))
        probe.add_notification_handler(lambda m, p: received_b.append(m))

        await mock_ws.inject_notification({
            "jsonrpc": "2.0",
            "method": "qtpilot.signalEmitted",
            "params": {"objectId": "btn", "signal": "clicked"},
        })
        await asyncio.sleep(0.1)

        assert len(received_a) == 1
        assert len(received_b) == 1
        assert received_a[0] == "qtpilot.signalEmitted"

    async def test_remove_handler(self, mock_probe):
        """Removed handler stops receiving notifications."""
        probe, mock_ws = mock_probe
        received = []

        handler = lambda m, p: received.append(m)
        probe.add_notification_handler(handler)

        await mock_ws.inject_notification({
            "jsonrpc": "2.0",
            "method": "qtpilot.signalEmitted",
            "params": {},
        })
        await asyncio.sleep(0.1)
        assert len(received) == 1

        probe.remove_notification_handler(handler)

        await mock_ws.inject_notification({
            "jsonrpc": "2.0",
            "method": "qtpilot.signalEmitted",
            "params": {},
        })
        await asyncio.sleep(0.1)
        assert len(received) == 1  # no new

    async def test_remove_unregistered_handler_is_noop(self, mock_probe):
        """Removing a handler that was never added does not raise."""
        probe, mock_ws = mock_probe
        probe.remove_notification_handler(lambda m, p: None)  # no error

    async def test_crashing_handler_does_not_kill_others(self, mock_probe):
        """One handler raising does not prevent other handlers from running."""
        probe, mock_ws = mock_probe
        received = []

        def bad_handler(m, p):
            raise RuntimeError("boom")

        probe.add_notification_handler(bad_handler)
        probe.add_notification_handler(lambda m, p: received.append(m))

        await mock_ws.inject_notification({
            "jsonrpc": "2.0",
            "method": "qtpilot.signalEmitted",
            "params": {},
        })
        await asyncio.sleep(0.1)

        assert len(received) == 1  # second handler still ran

    async def test_on_notification_backward_compat(self, mock_probe):
        """Legacy on_notification() still works — clears list, sets one handler."""
        probe, mock_ws = mock_probe
        received = []

        with pytest.warns(DeprecationWarning):
            probe.on_notification(lambda m, p: received.append(m))

        await mock_ws.inject_notification({
            "jsonrpc": "2.0",
            "method": "qtpilot.signalEmitted",
            "params": {},
        })
        await asyncio.sleep(0.1)
        assert len(received) == 1

    async def test_on_notification_clears_add_handlers(self, mock_probe):
        """Calling on_notification() destroys handlers added via add_notification_handler."""
        probe, mock_ws = mock_probe
        received_add = []
        received_on = []

        probe.add_notification_handler(lambda m, p: received_add.append(m))
        with pytest.warns(DeprecationWarning):
            probe.on_notification(lambda m, p: received_on.append(m))

        await mock_ws.inject_notification({
            "jsonrpc": "2.0",
            "method": "qtpilot.signalEmitted",
            "params": {},
        })
        await asyncio.sleep(0.1)

        assert len(received_add) == 0  # cleared by on_notification
        assert len(received_on) == 1

    async def test_on_notification_emits_deprecation_warning(self, mock_probe):
        """on_notification() emits a DeprecationWarning."""
        probe, mock_ws = mock_probe
        with pytest.warns(DeprecationWarning, match="add_notification_handler"):
            probe.on_notification(lambda m, p: None)

    async def test_on_notification_none_clears_all(self, mock_probe):
        """on_notification(None) clears the handler list."""
        probe, mock_ws = mock_probe
        received = []

        probe.add_notification_handler(lambda m, p: received.append(m))
        with pytest.warns(DeprecationWarning):
            probe.on_notification(None)

        await mock_ws.inject_notification({
            "jsonrpc": "2.0",
            "method": "qtpilot.signalEmitted",
            "params": {},
        })
        await asyncio.sleep(0.1)
        assert len(received) == 0


class TestCallObservers:
    async def test_observer_receives_successful_call(self, mock_probe):
        """Call observer fires with request, result, and duration on success."""
        probe, mock_ws = mock_probe
        observed = []

        probe.add_call_observer(lambda req, res, dur: observed.append((req, res, dur)))

        mock_ws.responses[probe._next_id] = {
            "jsonrpc": "2.0",
            "result": {"pong": True},
            "id": probe._next_id,
        }
        result = await probe.call("qt.ping")

        assert result == {"pong": True}
        assert len(observed) == 1
        req, res, dur = observed[0]
        assert req["method"] == "qt.ping"
        assert res == {"pong": True}
        assert dur >= 0

    async def test_observer_receives_error_call(self, mock_probe):
        """Call observer fires with the exception on probe error."""
        probe, mock_ws = mock_probe
        observed = []

        probe.add_call_observer(lambda req, res, dur: observed.append((req, res, dur)))

        mock_ws.responses[probe._next_id] = {
            "jsonrpc": "2.0",
            "error": {"code": -1, "message": "Not found"},
            "id": probe._next_id,
        }
        with pytest.raises(Exception, match="Not found"):
            await probe.call("qt.objects.info", {"objectId": "gone"})

        assert len(observed) == 1
        req, res_or_exc, dur = observed[0]
        assert req["method"] == "qt.objects.info"
        assert isinstance(res_or_exc, Exception)
        assert dur >= 0

    async def test_remove_call_observer(self, mock_probe):
        """Removed observer stops being called."""
        probe, mock_ws = mock_probe
        observed = []

        observer = lambda req, res, dur: observed.append(1)
        probe.add_call_observer(observer)
        probe.remove_call_observer(observer)

        mock_ws.responses[probe._next_id] = {
            "jsonrpc": "2.0",
            "result": {},
            "id": probe._next_id,
        }
        await probe.call("qt.ping")

        assert len(observed) == 0

    async def test_crashing_observer_does_not_break_call(self, mock_probe):
        """A crashing observer does not prevent the call from returning."""
        probe, mock_ws = mock_probe
        received = []

        def bad_observer(req, res, dur):
            raise RuntimeError("observer crash")

        probe.add_call_observer(bad_observer)
        probe.add_call_observer(lambda req, res, dur: received.append(1))

        mock_ws.responses[probe._next_id] = {
            "jsonrpc": "2.0",
            "result": {"ok": True},
            "id": probe._next_id,
        }
        result = await probe.call("qt.ping")

        assert result == {"ok": True}
        assert len(received) == 1  # second observer still ran


class TestSendObservers:
    async def test_send_observer_fires_on_call(self, mock_probe):
        """Send observer fires with the request dict when a call is sent."""
        probe, mock_ws = mock_probe
        sent = []

        probe.add_send_observer(lambda req: sent.append(req))

        mock_ws.responses[probe._next_id] = {
            "jsonrpc": "2.0",
            "result": {"pong": True},
            "id": probe._next_id,
        }
        await probe.call("qt.ping")

        assert len(sent) == 1
        assert sent[0]["method"] == "qt.ping"
        assert "id" in sent[0]

    async def test_remove_send_observer(self, mock_probe):
        """Removed send observer stops being called."""
        probe, mock_ws = mock_probe
        sent = []

        observer = lambda req: sent.append(1)
        probe.add_send_observer(observer)
        probe.remove_send_observer(observer)

        mock_ws.responses[probe._next_id] = {
            "jsonrpc": "2.0",
            "result": {},
            "id": probe._next_id,
        }
        await probe.call("qt.ping")

        assert len(sent) == 0

    async def test_crashing_send_observer_does_not_break_call(self, mock_probe):
        """A crashing send observer does not prevent the call from completing."""
        probe, mock_ws = mock_probe

        def bad_observer(req):
            raise RuntimeError("send observer crash")

        probe.add_send_observer(bad_observer)

        mock_ws.responses[probe._next_id] = {
            "jsonrpc": "2.0",
            "result": {"ok": True},
            "id": probe._next_id,
        }
        result = await probe.call("qt.ping")
        assert result == {"ok": True}
