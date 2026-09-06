"""Probe version / protocol handshake.

The defect: the probe reported a hardcoded "0.1.0" while the project was at
0.3.1, and nothing on the client side ever read a version, so skew surfaced as
an opaque "method not found" much later. These tests pin both halves -- the
client reads what the probe reports, and it says something useful when the two
sides disagree.
"""

from __future__ import annotations

import logging

import pytest

from qtpilot.connection import SUPPORTED_PROTOCOL_VERSION, ProbeConnection, ProbeError


class FakeProbe:
    """Stands in for call(), returning a canned getVersion result."""

    def __init__(self, result: dict | Exception) -> None:
        self._result = result
        self.calls: list[str] = []

    async def __call__(self, method: str, params: dict | None = None, **kwargs) -> dict:
        self.calls.append(method)
        if isinstance(self._result, Exception):
            raise self._result
        return self._result


def _conn_with(result: dict | Exception) -> tuple[ProbeConnection, FakeProbe]:
    conn = ProbeConnection("ws://localhost:9222")
    fake = FakeProbe(result)
    conn.call = fake  # type: ignore[assignment]
    return conn, fake


@pytest.mark.asyncio
async def test_handshake_records_version_and_protocol():
    conn, fake = _conn_with(
        {"version": "0.3.1", "protocolVersion": SUPPORTED_PROTOCOL_VERSION, "name": "qtPilot"}
    )
    await conn.handshake()

    assert fake.calls == ["getVersion"]
    assert conn.probe_version == "0.3.1"
    assert conn.probe_protocol_version == SUPPORTED_PROTOCOL_VERSION


@pytest.mark.asyncio
async def test_matching_protocol_logs_no_warning(caplog):
    conn, _ = _conn_with({"version": "0.3.1", "protocolVersion": SUPPORTED_PROTOCOL_VERSION})
    with caplog.at_level(logging.WARNING, logger="qtpilot.connection"):
        await conn.handshake()
    assert caplog.records == []


@pytest.mark.asyncio
async def test_mismatched_protocol_warns_and_names_both_sides(caplog):
    conn, _ = _conn_with(
        {"version": "9.9.9", "protocolVersion": SUPPORTED_PROTOCOL_VERSION + 1}
    )
    with caplog.at_level(logging.WARNING, logger="qtpilot.connection"):
        await conn.handshake()

    assert len(caplog.records) == 1
    message = caplog.records[0].getMessage()
    assert str(SUPPORTED_PROTOCOL_VERSION + 1) in message
    assert str(SUPPORTED_PROTOCOL_VERSION) in message
    assert "9.9.9" in message


@pytest.mark.asyncio
async def test_probe_without_protocol_version_is_flagged_as_stale(caplog):
    """A pre-fix probe reports a version but no protocolVersion at all. Absence
    is the signal that it predates negotiation."""
    conn, _ = _conn_with({"version": "0.1.0", "protocol": "jsonrpc-2.0"})
    with caplog.at_level(logging.WARNING, logger="qtpilot.connection"):
        await conn.handshake()

    assert len(caplog.records) == 1
    assert "protocolVersion" in caplog.records[0].getMessage()
    assert conn.probe_protocol_version is None


@pytest.mark.asyncio
async def test_non_integer_protocol_version_is_not_trusted(caplog):
    conn, _ = _conn_with({"version": "0.3.1", "protocolVersion": "1"})
    with caplog.at_level(logging.WARNING, logger="qtpilot.connection"):
        await conn.handshake()
    assert conn.probe_protocol_version is None


@pytest.mark.asyncio
async def test_handshake_failure_is_not_fatal(caplog):
    """A probe already running inside someone's app must stay usable even if the
    handshake call fails outright."""
    conn, _ = _conn_with(ProbeError("Method not found", code=-32601))
    with caplog.at_level(logging.WARNING, logger="qtpilot.connection"):
        result = await conn.handshake()

    assert result == {}
    assert conn.probe_version is None
    assert any("handshake failed" in r.getMessage().lower() for r in caplog.records)


@pytest.mark.asyncio
async def test_properties_are_none_before_handshake():
    conn = ProbeConnection("ws://localhost:9222")
    assert conn.probe_version is None
    assert conn.probe_protocol_version is None
