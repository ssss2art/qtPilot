"""Domain diagnostics must reject wrong targets, missing objects and no-op effects."""

from __future__ import annotations

import pytest

from tests.probe_expectations import expect_probe


def test_process_contract_rejects_the_wrong_target() -> None:
    with pytest.raises(AssertionError, match="Wrong process"):
        expect_probe({"result": {"pong": True, "pid": 1, "appName": "other", "qtVersion": "6.10.0"}}).to_identify(
            application="synthetic-fixture", qt_version="6.10.0")


def test_process_contract_rejects_the_wrong_runtime() -> None:
    with pytest.raises(AssertionError, match="Wrong Qt runtime"):
        expect_probe({"result": {"pong": True, "pid": 1, "appName": "synthetic-fixture", "qtVersion": "6.9.0"}}).to_identify(
            application="synthetic-fixture", qt_version="6.10.0")


def test_wire_failure_cannot_be_treated_as_a_result() -> None:
    with pytest.raises(AssertionError, match="result envelope"):
        expect_probe({"error": {"message": "method not found"}}).to_have_value(42)


def test_discovery_cannot_pass_without_the_expected_object() -> None:
    with pytest.raises(AssertionError, match="Expected one 'fixtureRoot'"):
        expect_probe({"result": {"objects": []}}).to_find("fixtureRoot")


def test_no_op_property_write_cannot_pass_effect_assertion() -> None:
    with pytest.raises(AssertionError, match="Expected application value 42"):
        expect_probe({"result": {"value": 1}}).to_have_value(42)


def test_missing_property_cannot_pass_as_null() -> None:
    with pytest.raises(AssertionError, match="Missing application value"):
        expect_probe({"result": {}}).to_have_value(None)


def test_expectations_accept_real_shaped_results() -> None:
    expect_probe({"result": {"pong": True, "pid": 1, "appName": "synthetic-fixture", "qtVersion": "6.10.0"}}).to_identify(
        application="synthetic-fixture", qt_version="6.10.0")
    expect_probe({"result": {"value": 42}}).to_have_value(42)
    assert expect_probe({"result": {"objects": [{"objectName": "fixtureRoot", "objectId": "fixtureRoot"}]}}).to_find("fixtureRoot") == "fixtureRoot"
