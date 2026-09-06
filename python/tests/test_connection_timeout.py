"""Request-deadline behaviour for ProbeConnection.call().

The defect these cover: call() awaited its response future with no timeout, so a
probe handler that blocked hung the MCP call forever. Every test here fails
against that version -- `test_call_times_out_when_probe_never_responds` hangs
rather than failing, which is precisely the bug.
"""

from __future__ import annotations

import asyncio
import time

import pytest

from qtpilot.connection import (
    DEFAULT_CALL_TIMEOUT,
    ProbeConnection,
    ProbeError,
    ProbeTimeoutError,
    _default_call_timeout,
)


class SilentSocket:
    """A socket that accepts sends and never delivers a response."""

    def __init__(self) -> None:
        self.sent: list[str] = []

    async def send(self, payload: str) -> None:
        self.sent.append(payload)

    async def close(self) -> None:
        pass


def _wire_up(conn: ProbeConnection, ws: object) -> None:
    """Put a connection into the 'connected' state without a real socket."""
    conn._ws = ws
    conn._connected = True


@pytest.mark.asyncio
async def test_call_times_out_when_probe_never_responds():
    conn = ProbeConnection("ws://localhost:9222")
    _wire_up(conn, SilentSocket())

    with pytest.raises(ProbeTimeoutError) as excinfo:
        await conn.call("qt.objects.tree", timeout=0.05)

    assert excinfo.value.method == "qt.objects.tree"
    assert excinfo.value.timeout == 0.05


@pytest.mark.asyncio
async def test_timeout_error_is_a_probe_error():
    """Existing callers catch ProbeError; the new type must not slip past them."""
    conn = ProbeConnection("ws://localhost:9222")
    _wire_up(conn, SilentSocket())

    with pytest.raises(ProbeError):
        await conn.call("ping", timeout=0.05)


@pytest.mark.asyncio
async def test_timeout_message_names_the_method_and_the_deadline():
    conn = ProbeConnection("ws://localhost:9222")
    _wire_up(conn, SilentSocket())

    with pytest.raises(ProbeTimeoutError) as excinfo:
        await conn.call("qt.ui.click", timeout=0.05)

    message = str(excinfo.value)
    assert "qt.ui.click" in message
    assert "0.05" in message


@pytest.mark.asyncio
async def test_timed_out_request_is_dropped_from_pending():
    """A late response must not resolve a future nobody awaits, and _pending
    must not grow for the life of the session."""
    conn = ProbeConnection("ws://localhost:9222")
    _wire_up(conn, SilentSocket())

    with pytest.raises(ProbeTimeoutError):
        await conn.call("ping", timeout=0.05)

    assert conn._pending == {}


@pytest.mark.asyncio
async def test_explicit_none_timeout_waits_indefinitely():
    """The escape hatch: timeout=None must not be confused with 'use default'."""
    conn = ProbeConnection("ws://localhost:9222")
    _wire_up(conn, SilentSocket())

    task = asyncio.create_task(conn.call("ping", timeout=None))
    await asyncio.sleep(0.15)
    assert not task.done(), "timeout=None should not impose a deadline"

    task.cancel()
    with pytest.raises(asyncio.CancelledError):
        await task


@pytest.mark.asyncio
async def test_successful_call_is_unaffected_by_the_deadline():
    conn = ProbeConnection("ws://localhost:9222")
    _wire_up(conn, SilentSocket())

    async def respond() -> None:
        await asyncio.sleep(0.01)
        future = conn._pending.pop(1)
        future.set_result({"pong": True})

    asyncio.create_task(respond())
    result = await conn.call("ping", timeout=5.0)
    assert result == {"pong": True}


@pytest.mark.asyncio
async def test_timeout_is_reported_to_call_observers():
    """Observability: the logging middleware must see the failure, not a silent gap."""
    conn = ProbeConnection("ws://localhost:9222")
    _wire_up(conn, SilentSocket())

    seen: list[object] = []
    conn._call_observers.append(lambda req, res, ms: seen.append(res))

    with pytest.raises(ProbeTimeoutError):
        await conn.call("ping", timeout=0.05)

    assert len(seen) == 1
    assert isinstance(seen[0], ProbeTimeoutError)


class TestDefaultTimeoutResolution:
    def test_default_when_unset(self, monkeypatch):
        monkeypatch.delenv("QTPILOT_CALL_TIMEOUT", raising=False)
        assert _default_call_timeout() == DEFAULT_CALL_TIMEOUT

    def test_env_override(self, monkeypatch):
        monkeypatch.setenv("QTPILOT_CALL_TIMEOUT", "2.5")
        assert _default_call_timeout() == 2.5

    def test_zero_disables_the_deadline(self, monkeypatch):
        monkeypatch.setenv("QTPILOT_CALL_TIMEOUT", "0")
        assert _default_call_timeout() is None

    def test_negative_disables_the_deadline(self, monkeypatch):
        monkeypatch.setenv("QTPILOT_CALL_TIMEOUT", "-1")
        assert _default_call_timeout() is None

    def test_garbage_falls_back_to_the_default(self, monkeypatch):
        """Fail safe, not open: an unparseable value must not mean 'no deadline'."""
        monkeypatch.setenv("QTPILOT_CALL_TIMEOUT", "soon")
        assert _default_call_timeout() == DEFAULT_CALL_TIMEOUT

    def test_empty_is_treated_as_unset(self, monkeypatch):
        monkeypatch.setenv("QTPILOT_CALL_TIMEOUT", "")
        assert _default_call_timeout() == DEFAULT_CALL_TIMEOUT


# ---------------------------------------------------------------------------
# Defect characterization
# ---------------------------------------------------------------------------
# Imports only symbols that existed BEFORE the fix, so this module collects and
# runs against the unpatched connection.py. That makes it a genuine red/green
# demonstration rather than an ImportError: on the old code call() has no
# deadline of its own, so it hangs until this test's outer guard fires and the
# test fails with the message below.


@pytest.mark.asyncio
async def test_call_imposes_its_own_deadline(monkeypatch):
    """call() must fail on its own, not hang until the caller gives up."""
    monkeypatch.setenv("QTPILOT_CALL_TIMEOUT", "0.2")

    conn = ProbeConnection("ws://localhost:9222")
    _wire_up(conn, SilentSocket())

    started = time.monotonic()
    try:
        # The guard is deliberately far larger than the configured deadline, so
        # tripping it means call() imposed no deadline at all.
        await asyncio.wait_for(conn.call("ping"), timeout=5.0)
    except asyncio.TimeoutError:
        pytest.fail(
            "call() never returned: it has no request deadline of its own and "
            "hung until this test's 5s guard fired. A blocked probe handler "
            "would hang the MCP call indefinitely."
        )
    except ProbeError:
        pass  # Correct: the connection surfaced its own deadline.
    else:
        pytest.fail("a silent probe should not have produced a successful result")

    elapsed = time.monotonic() - started
    assert elapsed < 2.0, f"deadline was not honoured promptly (took {elapsed:.2f}s)"
