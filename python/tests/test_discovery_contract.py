"""LAN announcements identify instances by address, process and port."""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from unittest.mock import patch

import pytest

from qtpilot.discovery import DiscoveryListener, DiscoveryProtocol


@dataclass
class DiscoveryScenario:
    listener: DiscoveryListener = field(default_factory=DiscoveryListener)

    def announces(self, address: str, pid: int, port: int = 9222) -> DiscoveryScenario:
        DiscoveryProtocol(self.listener).datagram_received(json.dumps({
            "protocol": "qtPilot-discovery", "type": "announce", "pid": pid,
            "wsPort": port, "appName": "synthetic-fixture",
        }).encode(), (address, 9221))
        return self

    def to_contain(self, *identities: str) -> DiscoveryScenario:
        actual = set(self.listener.probes)
        assert actual == set(identities), f"Discovered instances {actual}; expected {set(identities)}"
        return self


def test_two_hosts_and_two_processes_remain_independent() -> None:
    DiscoveryScenario().announces("192.0.2.1", 100).announces("192.0.2.2", 100).announces(
        "192.0.2.1", 101).announces("192.0.2.1", 100, 9223).to_contain(
        "192.0.2.1:100:9222", "192.0.2.2:100:9222", "192.0.2.1:101:9222", "192.0.2.1:100:9223")


def test_goodbye_removes_only_the_matching_instance() -> None:
    scenario = DiscoveryScenario().announces("192.0.2.1", 100).announces("192.0.2.2", 100)
    DiscoveryProtocol(scenario.listener).datagram_received(json.dumps({
        "protocol": "qtPilot-discovery", "type": "goodbye", "pid": 100, "wsPort": 9222,
    }).encode(), ("192.0.2.1", 9221))
    scenario.to_contain("192.0.2.2:100:9222")


def test_stale_expiry_does_not_remove_a_refreshed_peer() -> None:
    with patch("qtpilot.discovery.time.monotonic", return_value=100):
        scenario = DiscoveryScenario().announces("192.0.2.1", 100)
    with patch("qtpilot.discovery.time.monotonic", return_value=120):
        scenario.announces("192.0.2.2", 100)
        assert scenario.listener.prune_stale(timeout=15) == ["192.0.2.1:100:9222"]
    scenario.to_contain("192.0.2.2:100:9222")


@pytest.mark.parametrize("packet", [b"\xff", b"{", b"[]", b"null", b"42", b'"text"', b'{"protocol":"other"}'])
def test_malformed_or_unrelated_datagrams_do_not_discover_anything(packet: bytes) -> None:
    scenario = DiscoveryScenario()
    DiscoveryProtocol(scenario.listener).datagram_received(packet, ("192.0.2.1", 9221))
    scenario.to_contain()


def test_discovery_matcher_rejects_a_collapsed_identity() -> None:
    with pytest.raises(AssertionError, match="Discovered instances"):
        DiscoveryScenario().announces("192.0.2.1", 100).to_contain("192.0.2.2:100:9222")


@pytest.mark.parametrize("tls,scheme", [(False, "ws"), (True, "wss")])
def test_discovery_preserves_the_advertised_transport(tls: bool, scheme: str) -> None:
    scenario = DiscoveryScenario()
    scenario.listener.on_announce({"pid": 100, "tls": tls, "authRequired": tls}, "192.0.2.1")
    probe = next(iter(scenario.listener.probes.values()))
    assert probe.ws_url == f"{scheme}://192.0.2.1:9222", "Discovery weakened the advertised transport"
    assert probe.auth_required is tls


@pytest.mark.parametrize("tls", ["false", "true", 1, None, []])
def test_invalid_transport_hints_cannot_create_a_plaintext_endpoint(tls: object) -> None:
    scenario = DiscoveryScenario()
    scenario.listener.on_announce({"pid": 100, "tls": tls}, "192.0.2.1")
    scenario.to_contain()
