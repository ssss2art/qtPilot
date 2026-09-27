"""Contract validation pins assertion scope without rewriting raw JSON values."""

from __future__ import annotations

import json
from collections.abc import Callable

import pytest

from qtpilot.replay_contract import parse_contract


def observation(expected: object = "ready") -> dict:
    return {"name": "field", "method": "qt.properties.get",
            "params": {"objectId": "form~7", "name": "text"},
            "path": ["result", "value"], "expected": expected}


def contract_document() -> dict:
    return {
        "format": 2, "fixture": "prepared-form", "timeout": 2,
        "requirements": [{"name": "protocol", "method": "getVersion", "params": {},
                          "path": ["protocolVersion"], "expected": 1}],
        "preconditions": [observation()],
        "steps": [{"name": "edit", "action": {"method": "qt.properties.set",
                   "params": {"objectId": "form~7", "name": "text", "value": "user~1"}},
                   "postconditions": [observation("user~1")]}],
    }


def test_contract_preserves_parameters_and_exact_assertion_scope() -> None:
    parsed = parse_contract(json.dumps(contract_document()))
    assert parsed.is_ok(), parsed.unwrap_err()
    contract = parsed.unwrap()
    assert contract.fixture == "prepared-form"
    assert contract.steps[0].action.params["objectId"] == "form~7"
    assert contract.steps[0].action.params["value"] == "user~1"
    assert contract.preconditions[0].path == ("result", "value")
    assert contract.to_dict() == contract_document()


@pytest.mark.parametrize("change,diagnostic", [
    (lambda d: d.update(format=1), "format"),
    (lambda d: d.update(format=True), "format"),
    (lambda d: d.update(reset="shell command"), "unknown"),
    (lambda d: d.update(preconditions=[]), "preconditions"),
    (lambda d: d.update(requirements=[]), "requirements"),
    (lambda d: d.update(timeout=False), "timeout"),
    (lambda d: d.update(timeout=float("nan")), "JSON"),
    (lambda d: d["preconditions"][0].update(method="qt.properties.set"), "read-only"),
    (lambda d: d["preconditions"][0].update(path=[True]), "path"),
    (lambda d: d["preconditions"][0].update(expected="payload...<truncated 100c>"), "truncated"),
    (lambda d: d["steps"][0].update(postconditions=[]), "completion"),
    (lambda d: d["steps"][0]["action"].update(method="unknown.drive"), "action"),
], ids=["old-format", "bool-version", "unknown-field", "no-initial-state", "no-environment",
        "bool-deadline", "non-finite", "mutating-read", "bool-index", "truncated", "no-completion", "unsupported-action"])
def test_invalid_contract_has_an_actionable_diagnosis(change: Callable[[dict], None], diagnostic: str) -> None:
    document = contract_document()
    change(document)
    result = parse_contract(json.dumps(document))
    assert result.is_err()
    assert diagnostic in result.unwrap_err()


def test_duplicate_json_keys_are_rejected() -> None:
    result = parse_contract('{"format": 1, "format": 2}')
    assert result.is_err() and "duplicate" in result.unwrap_err()


def test_checkpoint_can_supply_completion_without_a_postcondition() -> None:
    document = contract_document()
    document["steps"][0].update(postconditions=[], checkpoint={
        "objectId": "form~7", "signal": "textChanged", "arguments": ["user~1"],
    })
    result = parse_contract(json.dumps(document))
    assert result.is_ok(), result.unwrap_err()
    assert result.unwrap().steps[0].checkpoint.arguments == ("user~1",)
