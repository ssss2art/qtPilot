"""Execute a declared replay contract and retain exact, scoped evidence."""

from __future__ import annotations

import asyncio
import json
from contextlib import AsyncExitStack
from dataclasses import asdict, dataclass, replace
from typing import Literal, Protocol

from qtpilot.checkpoints import ArmedCheckpoint, CheckpointFailure, CheckpointProbe, arm_checkpoint
from qtpilot.connection import ProbeError
from qtpilot.evidence import BufferEvidence
from qtpilot.replay_contract import Assertion, Json, MAX_CONTRACT_BYTES, ReplayContract, incomplete_value, parse_contract
from qtpilot.result import Err, Ok, Result

EvidenceKind = Literal["exact", "normalized", "wildcard", "unavailable", "unsupported"]


class ContractProbe(CheckpointProbe, Protocol):
    async def loss_evidence(self) -> tuple[BufferEvidence, BufferEvidence]: ...


@dataclass(frozen=True, slots=True)
class Comparison:
    step: str
    name: str
    method: str
    path: tuple[str | int, ...]
    expected: Json
    actual: Json
    raw: Json
    kind: EvidenceKind
    matched: bool


@dataclass(frozen=True, slots=True)
class ContractResult:
    fixture: str
    total_actions: int
    actions_driven: int = 0
    comparisons: tuple[Comparison, ...] = ()
    completed_checkpoints: tuple[str, ...] = ()
    loss_verified: bool = False
    failure_kind: str | None = None
    failed_step: str | None = None
    reason: str | None = None
    raw_actions: tuple[Json, ...] = ()
    loss_before: tuple[BufferEvidence, ...] = ()
    loss_after: tuple[BufferEvidence, ...] = ()
    checkpoint_evidence: tuple[BufferEvidence, ...] = ()

    @property
    def passed(self) -> bool:
        return (self.failure_kind is None and self.loss_verified
                and self.actions_driven == self.total_actions and bool(self.comparisons)
                and all(item.known and item.dropped == 0 for item in self.checkpoint_evidence)
                and all(c.matched and c.kind == "exact" for c in self.comparisons))

    def to_dict(self) -> dict[str, Json]:
        report = json.loads(json.dumps(asdict(self), allow_nan=False))
        report.update(mode="strict", passed=self.passed, strict_passed=self.passed,
                      comparison_units="assertions", evidence_counts={
                          kind: sum(c.kind == kind for c in self.comparisons)
                          for kind in ("exact", "normalized", "wildcard", "unavailable", "unsupported")
                      })
        for name in ("loss_before", "loss_after", "checkpoint_evidence"):
            report[name] = [item.to_dict() for item in getattr(self, name)]
        return report


def _selected(raw: Json, path: tuple[str | int, ...]) -> Json:
    value = raw
    for part in path:
        if isinstance(part, str) and isinstance(value, dict):
            value = value[part]
        elif type(part) is int and isinstance(value, list):
            value = value[part]
        else:
            raise ValueError(f"path {path!r} does not select a JSON value")
    return value


def _exact(expected: Json, actual: Json) -> bool:
    return json.dumps(expected, sort_keys=True, allow_nan=False) == json.dumps(actual, sort_keys=True, allow_nan=False)


def _unsupported(value: Json) -> bool:
    if isinstance(value, dict):
        return bool(value.get("_type") and value.get("value", object()) is None) or any(
            _unsupported(child) for child in value.values())
    return isinstance(value, list) and any(_unsupported(child) for child in value)


def verify_loss(before: tuple[BufferEvidence, ...], after: tuple[BufferEvidence, ...]) -> Result[None, str]:
    if {item.source for item in before} != {"probe", "controller"} or len(before) != 2:
        return Err("both probe and controller loss evidence are required")
    if [item.source for item in before] != [item.source for item in after]:
        return Err("loss evidence sources changed")
    for start, end in zip(before, after, strict=True):
        loss = end.loss_since(start)
        if loss.is_err():
            return Err(loss.unwrap_err())
        if loss.unwrap():
            return Err(f"{end.source}: {loss.unwrap()} notifications dropped during replay")
    return Ok(None)


async def run_contract(contract: ReplayContract, probe: ContractProbe) -> ContractResult:
    """Drive declared actions only after exact readiness and known loss counters.

    Each readiness phase/step has an asyncio deadline. Postconditions are sampled
    after the action response (or declared Qt signal); there is no settling sleep.
    The fixture label describes caller-prepared state and executes no reset code.
    """
    result = ContractResult(contract.fixture, len(contract.steps))
    # Own the contract snapshot even when the library caller built mutable JSON
    # parameters directly. The same validation gate applies to CLI, MCP and API.
    validated = Result.from_callable(lambda: json.dumps(contract.to_dict(), allow_nan=False)).map_err(str).flat_map(parse_contract)
    if validated.is_err():
        return replace(result, failure_kind="contract", reason=validated.unwrap_err())
    contract = validated.unwrap()
    phase = "evidence"
    step_name = "initial"
    retained_bytes = 0

    def retain(raw: Json) -> Json:
        nonlocal retained_bytes
        serialized = json.dumps(raw, allow_nan=False)
        retained_bytes += len(serialized.encode("utf-8"))
        if retained_bytes > MAX_CONTRACT_BYTES:
            raise ValueError("raw evidence exceeds 4 MiB; strict replay cannot truncate it")
        return json.loads(serialized)

    async def compare(assertions: tuple[Assertion, ...]) -> bool:
        nonlocal result
        for assertion in assertions:
            try:
                raw = retain(await probe.call(assertion.method, assertion.params, timeout=contract.timeout))
                selection = Result.from_callable(_selected, raw, assertion.path)
                actual = selection.unwrap_or(None)
                kind: EvidenceKind = "exact"
                if selection.is_err():
                    kind = "unavailable"
                elif incomplete_value(actual):
                    kind = "wildcard"
                elif _unsupported(actual):
                    kind = "unsupported"
                matched = kind == "exact" and _exact(assertion.expected, actual)
            except (ProbeError, OSError) as exc:
                raw, actual, kind, matched = str(exc), None, "unavailable", False
            comparison = Comparison(step_name, assertion.name, assertion.method, assertion.path,
                                    assertion.expected, actual, raw, kind, matched)
            result = replace(result, comparisons=(*result.comparisons, comparison))
            if not matched:
                return False
        return True

    def fail(reason: str) -> ContractResult:
        return replace(result, failure_kind=phase, failed_step=step_name, reason=reason)

    def retain_checkpoint(pending: ArmedCheckpoint) -> None:
        nonlocal result
        evidence = replace(pending.evidence(), source=f"checkpoint:{step_name}")
        result = replace(result, checkpoint_evidence=(*result.checkpoint_evidence, evidence))

    try:
        async with asyncio.timeout(contract.timeout):
            before = await probe.loss_evidence()
            result = replace(result, loss_before=before)
            verified = verify_loss(before, before)
            if verified.is_err():
                return fail(verified.unwrap_err())
            for phase, assertions in (("requirement", contract.requirements), ("precondition", contract.preconditions)):
                if not await compare(assertions):
                    return fail(f"{phase} {result.comparisons[-1].name!r} did not match exact evidence")
            phase = "evidence"
            after = await probe.loss_evidence()
            verified = verify_loss(before, after)
            result = replace(result, loss_after=after)
            if verified.is_err():
                return fail(verified.unwrap_err())

        for step in contract.steps:
            step_name, phase = step.name, "setup"
            postcondition_failed = False
            # Cleanup sits outside the step deadline. Each unsubscribe has its
            # own bounded deadline and still runs after timeout or cancellation.
            async with AsyncExitStack() as cleanup:
                async with asyncio.timeout(contract.timeout):
                    pending = None
                    if step.checkpoint:
                        owned = AsyncExitStack()
                        pending = await owned.enter_async_context(arm_checkpoint(probe, step.checkpoint))
                        cleanup.callback(retain_checkpoint, pending)
                        await cleanup.enter_async_context(owned)
                    phase = "action"
                    result = replace(result, actions_driven=result.actions_driven + 1)
                    response = retain(await probe.call(step.action.method, step.action.params, timeout=contract.timeout))
                    result = replace(result, raw_actions=(*result.raw_actions, response))
                    if pending is not None:
                        phase = "checkpoint"
                        raw = retain(await pending.wait())
                        if not isinstance(raw, dict) or step.checkpoint is None:
                            raise CheckpointFailure("checkpoint evidence was not a valid dictionary")
                        expected: Json = list(step.checkpoint.arguments) if step.checkpoint.arguments is not None else step.checkpoint.signal
                        path: tuple[str | int, ...] = ("arguments",) if step.checkpoint.arguments is not None else ("signal",)
                        result = replace(result, completed_checkpoints=(*result.completed_checkpoints, step.name),
                                         comparisons=(*result.comparisons, Comparison(step.name, "checkpoint", "qtpilot.signalEmitted", path,
                                         expected, _selected(raw, path), raw, "exact", True)))
                    phase = "postcondition"
                    if not await compare(step.postconditions):
                        postcondition_failed = True
                phase = "cleanup"
            if postcondition_failed:
                phase = "postcondition"
                return fail(f"postcondition {result.comparisons[-1].name!r} did not match exact evidence")
            phase = "evidence"
            async with asyncio.timeout(contract.timeout):
                after = await probe.loss_evidence()
            result = replace(result, loss_after=after)
            verified = verify_loss(before, after)
            if verified.is_err():
                return fail(verified.unwrap_err())
        return replace(result, loss_verified=True)
    except (CheckpointFailure, ProbeError, OSError, ValueError, AssertionError) as exc:
        return fail(str(exc) or f"{phase} exceeded {contract.timeout:g}s deadline")
