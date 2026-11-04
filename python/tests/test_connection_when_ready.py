"""connect_when_ready(): connect and handshake, retrying while the app is still starting.

The defect it replaces: every session driving a tablet wrote its own retry loop round
connect(), because a probe in an app that is still starting stalls the WebSocket opening
handshake ("timed out during opening handshake") rather than refusing it.
"""

from __future__ import annotations

import pytest

from qtpilot.connection import ProbeConnection


def _conn(outcomes: list) -> tuple[ProbeConnection, list[str]]:
    """A connection whose connect()/handshake() follow `outcomes`, one entry per attempt."""
    conn = ProbeConnection("ws://127.0.0.1:9222")
    log: list[str] = []
    attempts = iter(outcomes)

    async def connect() -> None:
        log.append("connect")
        outcome = next(attempts)
        if isinstance(outcome, Exception):
            raise outcome
        conn._pending_handshake = outcome  # type: ignore[attr-defined]

    async def handshake() -> dict:
        log.append("handshake")
        return conn._pending_handshake  # type: ignore[attr-defined]

    async def disconnect() -> None:
        log.append("disconnect")

    conn.connect = connect  # type: ignore[method-assign]
    conn.handshake = handshake  # type: ignore[method-assign]
    conn.disconnect = disconnect  # type: ignore[method-assign]
    return conn, log


@pytest.mark.asyncio
async def test_it_retries_until_the_probe_answers():
    conn, log = _conn([TimeoutError("timed out during opening handshake"), ConnectionRefusedError(), {"version": "1"}])

    info = await conn.connect_when_ready(ready_timeout=5, interval=0)

    assert info == {"version": "1"}
    assert log.count("connect") == 3
    assert log[-1] == "handshake", "a ready connection is kept open"


@pytest.mark.asyncio
async def test_a_silent_handshake_counts_as_not_ready():
    conn, log = _conn([{}, {"version": "1"}])

    assert await conn.connect_when_ready(ready_timeout=5, interval=0) == {"version": "1"}
    assert log == ["connect", "handshake", "disconnect", "connect", "handshake"]


@pytest.mark.asyncio
async def test_it_gives_up_naming_the_last_failure():
    conn, _ = _conn([ConnectionRefusedError("refused")] * 100)

    with pytest.raises(TimeoutError, match="ConnectionRefusedError: refused"):
        await conn.connect_when_ready(ready_timeout=0.05, interval=0.01)
