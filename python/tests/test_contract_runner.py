"""A strict pass requires state, completion and complete session-scoped evidence."""

from __future__ import annotations

import asyncio
import json
from dataclasses import replace

import pytest

from qtpilot.contract_runner import ContractResult, run_contract
from qtpilot.evidence import BufferEvidence
from qtpilot.fluent import expect_contract_replay
from qtpilot.replay_contract import Json, ReplayContract, parse_contract
from tests.test_checkpoints import SignalProbe
from tests.test_replay_contract import contract_document


class ContractProbe(SignalProbe):
    def __init__(self) -> None:
        super().__init__()
        self.value: Json = "ready"
        self.protocol = 1
        self.loss = (BufferEvidence("probe", "native-1", 0, 10, 0),
                     BufferEvidence("controller", "client-1", 0, 10, 0))
        self.after_action_loss: tuple[BufferEvidence, BufferEvidence] | None = None
        self.action_error = False
        self.emit_completion = True
        self.literal_params: dict[str, Json] = {}

    async def loss_evidence(self) -> tuple[BufferEvidence, BufferEvidence]:
        return self.loss

    async def call(self, method: str, params: dict[str, Json] | None = None,
                   timeout: float | None = None) -> Json:
        if method in {"getVersion", "qt.properties.get", "qt.properties.set"}:
            self.calls.append(method)
            if method == "getVersion":
                return {"protocolVersion": self.protocol}
            if method == "qt.properties.get":
                return {"result": {"value": self.value}, "timestamp": 9876, "objectId": "form~7"}
            self.literal_params = params or {}
            if self.action_error:
                raise ConnectionError("target disconnected")
            self.value = (params or {}).get("value")
            if self.emit_completion:
                self.emit("saved", [self.value])
            if self.after_action_loss:
                self.loss = self.after_action_loss
            return {"ok": True, "timestamp": 1234}
        return await super().call(method, params, timeout)


def ready_contract(*, checkpoint: bool = False) -> ReplayContract:
    document = contract_document()
    if checkpoint:
        document["steps"][0]["checkpoint"] = {"objectId": "form", "signal": "saved", "arguments": ["user~1"]}
    return parse_contract(json.dumps(document)).unwrap()


@pytest.mark.asyncio
async def test_exact_scoped_contract_retains_raw_values_and_literal_action_parameters() -> None:
    probe = ContractProbe()
    result = await run_contract(ready_contract(), probe)
    expect_contract_replay(result).to_pass_strictly().to_have_driven(1).to_have_no_evidence_loss().to_have_evidence(exact=3, normalized=0, wildcard=0)
    assert result.comparisons[-1].raw == {"result": {"value": "user~1"}, "timestamp": 9876, "objectId": "form~7"}
    assert result.comparisons[-1].path == ("result", "value")
    assert probe.literal_params == {"objectId": "form~7", "name": "text", "value": "user~1"}


@pytest.mark.asyncio
@pytest.mark.parametrize("fault,kind", [("state", "precondition"), ("protocol", "requirement"), ("unknown", "evidence")])
async def test_unverified_readiness_drives_zero_actions(fault: str, kind: str) -> None:
    probe = ContractProbe()
    if fault == "state":
        probe.value = "wrong-state"
    elif fault == "protocol":
        probe.protocol = 99
    else:
        probe.loss = (BufferEvidence("probe", None), probe.loss[1])
    result = await run_contract(ready_contract(), probe)
    expect_contract_replay(result).to_fail_as(kind).to_have_driven(0)
    assert "qt.properties.set" not in probe.calls


@pytest.mark.asyncio
@pytest.mark.parametrize("after", [
    BufferEvidence("probe", "native-1", 1, 10, 0),
    BufferEvidence("probe", None),
    BufferEvidence("probe", "native-2", 0, 10, 0),
], ids=["lost-notification", "unknown-counter", "reconnected"])
async def test_changed_or_incomplete_evidence_blocks_the_next_action(after: BufferEvidence) -> None:
    probe = ContractProbe()
    probe.after_action_loss = (after, probe.loss[1])
    contract = ready_contract()
    contract = replace(contract, steps=(*contract.steps, replace(contract.steps[0], name="next")))
    result = await run_contract(contract, probe)
    expect_contract_replay(result).to_fail_as("evidence", "edit").to_have_driven(1)


@pytest.mark.asyncio
async def test_action_failure_is_not_an_observation_divergence() -> None:
    probe = ContractProbe()
    probe.action_error = True
    expect_contract_replay(await run_contract(ready_contract(), probe)).to_fail_as("action", "edit").to_have_driven(1)


@pytest.mark.asyncio
async def test_signal_checkpoint_completes_before_postcondition_sampling() -> None:
    probe = ContractProbe()
    result = await run_contract(ready_contract(checkpoint=True), probe)
    expect_contract_replay(result).to_pass_strictly().to_complete_checkpoint("edit").to_have_evidence(exact=4)
    assert not probe.handlers and not probe.subscriptions


@pytest.mark.asyncio
async def test_checkpoint_deadline_and_cancellation_release_resources() -> None:
    probe = ContractProbe()
    probe.emit_completion = False
    contract = replace(ready_contract(checkpoint=True), timeout=0.01)
    result = await run_contract(contract, probe)
    expect_contract_replay(result).to_fail_as("checkpoint", "edit").to_have_driven(1)
    assert not probe.handlers and not probe.subscriptions


def test_fluent_matchers_reject_missing_evidence_with_domain_diagnostics() -> None:
    result = ContractResult("form", 1, failure_kind="precondition", reason="wrong initial value")
    with pytest.raises(AssertionError, match="Expected strict pass"):
        expect_contract_replay(result).to_pass_strictly()
    with pytest.raises(AssertionError, match="Expected action failure"):
        expect_contract_replay(result).to_fail_as("action")
    with pytest.raises(AssertionError, match="Expected 1 actions driven, got 0"):
        expect_contract_replay(result).to_have_driven(1)
    with pytest.raises(AssertionError, match="Expected checkpoint saved"):
        expect_contract_replay(result).to_complete_checkpoint("saved")
    with pytest.raises(AssertionError, match="Expected 1 exact comparisons, got 0"):
        expect_contract_replay(result).to_have_evidence(exact=1)
    with pytest.raises(AssertionError, match="Expected verified lossless evidence"):
        expect_contract_replay(result).to_have_no_evidence_loss()


@pytest.mark.asyncio
@pytest.mark.parametrize("fault", ["none", "postcondition", "cleanup-overflow"])
async def test_checkpoint_evidence_includes_cleanup_even_when_a_step_fails(fault: str) -> None:
    probe = ContractProbe()
    contract = ready_contract(checkpoint=True)
    if fault == "postcondition":
        step = contract.steps[0]
        contract = replace(contract, steps=(replace(step, postconditions=(
            replace(step.postconditions[0], expected="wrong"),)),))
    if fault == "cleanup-overflow":
        def overflow() -> None:
            for _ in range(257):
                probe.emit("destroyed")
        probe.on_unsubscribe = overflow
    result = await run_contract(contract, probe)
    if fault == "none":
        expect_contract_replay(result).to_pass_strictly().to_have_no_evidence_loss()
    else:
        expect_contract_replay(result).to_fail_as("cleanup" if fault == "cleanup-overflow" else "postcondition")
    evidence = result.checkpoint_evidence
    assert len(evidence) == 1
    assert evidence[0].known and evidence[0].source == "checkpoint:edit"
    assert evidence[0].capacity == 256
    assert evidence[0].dropped == (1 if fault == "cleanup-overflow" else 0)
    assert result.to_dict()["checkpoint_evidence"][0]["known"] is True
    assert not probe.handlers and not probe.subscriptions


@pytest.mark.asyncio
async def test_nested_unsupported_native_value_cannot_match_exactly() -> None:
    value: Json = {"nested": [{"_type": "UnregisteredValue", "value": None}]}
    probe = ContractProbe()
    probe.value = value
    contract = ready_contract()
    contract = replace(contract, preconditions=(replace(contract.preconditions[0], expected=value),))
    expect_contract_replay(await run_contract(contract, probe)).to_fail_as("precondition").to_have_driven(0).to_have_evidence(unsupported=1)
