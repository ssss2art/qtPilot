"""Typed, session-scoped evidence counters; unavailable is never reported as zero."""

from __future__ import annotations

from dataclasses import asdict, dataclass
from collections.abc import Mapping

from qtpilot.result import Err, Ok, Result


@dataclass(frozen=True, slots=True)
class BufferEvidence:
    source: str
    epoch: str | None
    dropped: int | None = None
    capacity: int | None = None
    queued: int | None = None

    @property
    def known(self) -> bool:
        return (
            isinstance(self.epoch, str) and bool(self.epoch)
            and type(self.dropped) is int and self.dropped >= 0
            and type(self.capacity) is int and self.capacity > 0
            and type(self.queued) is int and 0 <= self.queued <= self.capacity
        )

    @classmethod
    def from_wire(cls, source: str, epoch: str | None, value: object) -> BufferEvidence:
        if isinstance(value, Mapping):
            fields = [value.get(name) for name in ("dropped", "capacity", "queued")]
            if all(type(item) is int for item in fields):
                candidate = cls(source, epoch, *fields)
                if candidate.known:
                    return candidate
        return cls(source, epoch)

    def loss_since(self, earlier: BufferEvidence) -> Result[int, str]:
        if not self.known or not earlier.known:
            return Err(f"{self.source}: loss counter unavailable")
        if self.source != earlier.source or self.epoch != earlier.epoch:
            return Err(f"{self.source}: evidence belongs to a different session")
        assert self.dropped is not None and earlier.dropped is not None
        if self.dropped < earlier.dropped:
            return Err(f"{self.source}: loss counter reset during collection")
        return Ok(self.dropped - earlier.dropped)

    def to_dict(self) -> dict[str, str | int | bool | None]:
        return {**asdict(self), "known": self.known}
