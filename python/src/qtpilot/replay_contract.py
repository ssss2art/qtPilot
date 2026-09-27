"""Explicit acceptance contracts, separate from exploratory JSONL transcripts."""

from __future__ import annotations

import json
import math
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import TypeAlias, cast

from qtpilot.replay import MUTATING_METHODS, OBSERVING_METHODS, _is_truncated
from qtpilot.result import Result

Json: TypeAlias = None | bool | int | float | str | list["Json"] | dict[str, "Json"]
MAX_CONTRACT_BYTES = 4 * 1024 * 1024


@dataclass(frozen=True, slots=True)
class ContractCall:
    method: str
    params: dict[str, Json]


@dataclass(frozen=True, slots=True)
class Assertion:
    name: str
    method: str
    params: dict[str, Json]
    path: tuple[str | int, ...]
    expected: Json


@dataclass(frozen=True, slots=True)
class Checkpoint:
    objectId: str
    signal: str
    arguments: tuple[Json, ...] | None = None


@dataclass(frozen=True, slots=True)
class ContractStep:
    name: str
    action: ContractCall
    postconditions: tuple[Assertion, ...]
    checkpoint: Checkpoint | None = None


@dataclass(frozen=True, slots=True)
class ReplayContract:
    fixture: str
    timeout: float
    requirements: tuple[Assertion, ...]
    preconditions: tuple[Assertion, ...]
    steps: tuple[ContractStep, ...]
    format: int = 2

    def to_dict(self) -> dict[str, Json]:
        # asdict retains tuples; the JSON round trip is an owned, JSON-only snapshot.
        document = json.loads(json.dumps(asdict(self), allow_nan=False))
        for step in document["steps"]:
            if step["checkpoint"] is None:
                del step["checkpoint"]
            elif step["checkpoint"]["arguments"] is None:
                del step["checkpoint"]["arguments"]
        return document


def incomplete_value(value: Json) -> bool:
    """Legacy placeholders cannot establish exact evidence, even when nested."""
    if isinstance(value, dict):
        return any(incomplete_value(item) for item in value.values())
    if isinstance(value, list):
        return any(incomplete_value(item) for item in value)
    return _is_truncated(value) or (isinstance(value, str) and value.endswith("~*"))


def _object(value: Json, required: set[str], optional: set[str], where: str) -> dict[str, Json]:
    if not isinstance(value, dict):
        raise ValueError(f"{where}: expected an object")
    if unknown := value.keys() - required - optional:
        raise ValueError(f"{where}: unknown fields {sorted(unknown)}")
    if missing := required - value.keys():
        raise ValueError(f"{where}: missing fields {sorted(missing)}")
    return value


def _text(value: Json, where: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise ValueError(f"{where}: expected a nonempty string")
    return value


def _items(value: Json, where: str, minimum: int = 0) -> list[Json]:
    if not isinstance(value, list) or not minimum <= len(value) <= 1000:
        raise ValueError(f"{where}: expected {minimum}..1000 entries")
    return value


def _params(value: Json) -> dict[str, Json]:
    if not isinstance(value, dict):
        raise ValueError("params: expected an object")
    return value


def _assertion(value: Json) -> Assertion:
    obj = _object(value, {"name", "method", "params", "path", "expected"}, set(), "assertion")
    method = _text(obj["method"], "method")
    if method not in OBSERVING_METHODS | {"getModes"}:
        raise ValueError(f"assertion: {method} is not a supported read-only method")
    path = _items(obj["path"], "path")
    if any(not isinstance(p, str) and not (type(p) is int and p >= 0) for p in path):
        raise ValueError("path: expected string keys or nonnegative integer indices")
    if incomplete_value(obj["expected"]):
        raise ValueError("expected: truncated or wildcard evidence requires a fresh exact baseline")
    return Assertion(_text(obj["name"], "name"), method, _params(obj["params"]),
                     tuple(cast(list[str | int], path)), obj["expected"])


def _checkpoint(value: Json) -> Checkpoint:
    obj = _object(value, {"objectId", "signal"}, {"arguments"}, "checkpoint")
    args = tuple(_items(obj["arguments"], "arguments")) if "arguments" in obj else None
    if args is not None and incomplete_value(list(args)):
        raise ValueError("checkpoint: truncated or wildcard arguments")
    return Checkpoint(_text(obj["objectId"], "objectId"), _text(obj["signal"], "signal"), args)


def _step(value: Json) -> ContractStep:
    obj = _object(value, {"name", "action", "postconditions"}, {"checkpoint"}, "step")
    action = _object(obj["action"], {"method", "params"}, set(), "action")
    method = _text(action["method"], "method")
    if method not in MUTATING_METHODS:
        raise ValueError(f"action: unsupported method {method}")
    post = tuple(_assertion(v) for v in _items(obj["postconditions"], "postconditions"))
    checkpoint = _checkpoint(obj["checkpoint"]) if "checkpoint" in obj else None
    if not post and checkpoint is None:
        raise ValueError("step: completion requires postconditions or a signal checkpoint")
    return ContractStep(_text(obj["name"], "name"), ContractCall(method, _params(action["params"])), post, checkpoint)


def _unique_object(pairs: list[tuple[str, Json]]) -> dict[str, Json]:
    result: dict[str, Json] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"JSON: duplicate key {key}")
        result[key] = value
    return result


def _reject_constant(value: str) -> Json:
    raise ValueError(f"JSON: non-finite number {value}")


def _parse(text: str) -> ReplayContract:
    if len(text.encode("utf-8")) > MAX_CONTRACT_BYTES:
        raise ValueError("contract exceeds 4 MiB")
    value = json.loads(text, object_pairs_hook=_unique_object, parse_constant=_reject_constant)
    obj = _object(value, {"format", "fixture", "timeout", "requirements", "preconditions", "steps"}, set(), "contract")
    if type(obj["format"]) is not int or obj["format"] != 2:
        raise ValueError("format: expected contract version 2; create an explicit contract or use exploratory replay")
    timeout = obj["timeout"]
    if type(timeout) not in (int, float) or not math.isfinite(timeout) or not 0 < timeout <= 300:
        raise ValueError("timeout: expected seconds in (0, 300]")
    requirements = tuple(_assertion(v) for v in _items(obj["requirements"], "requirements", 1))
    preconditions = tuple(_assertion(v) for v in _items(obj["preconditions"], "preconditions", 1))
    steps = tuple(_step(v) for v in _items(obj["steps"], "steps", 1))
    names = [step.name for step in steps]
    if len(set(names)) != len(names):
        raise ValueError("steps: names must be unique")
    return ReplayContract(_text(obj["fixture"], "fixture"), float(timeout), requirements, preconditions, steps)


def parse_contract(text: str) -> Result[ReplayContract, str]:
    """Validate the whole artifact before any connection or application mutation."""
    return Result.from_callable(_parse, text).map_err(lambda error: str(error))


def load_contract(path: str | Path) -> Result[ReplayContract, str]:
    def read() -> str:
        with Path(path).open(encoding="utf-8") as stream:
            return stream.read(MAX_CONTRACT_BYTES + 1)
    return Result.from_callable(read).map_err(str).flat_map(parse_contract)
