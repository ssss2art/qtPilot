"""Monadic Result types for functional error handling and deterministic pipelines."""

from __future__ import annotations

from abc import ABC, abstractmethod
from collections.abc import Callable, Sequence
from dataclasses import dataclass
from typing import Any, Generic, TypeVar

T = TypeVar("T")
E = TypeVar("E")
U = TypeVar("U")
F = TypeVar("F")


class Result(Generic[T, E], ABC):
    """Monadic container representing either success (Ok) or failure (Err)."""

    @classmethod
    def ok(cls, value: T) -> Result[T, E]:
        return Ok(value)

    @classmethod
    def err(cls, error: E) -> Result[T, E]:
        return Err(error)

    @classmethod
    def collect(cls, results: Sequence[Result[T, E]]) -> Result[list[T], list[E]]:
        """Sequence a list of Results into a Result containing either all values or all errors."""
        values: list[T] = []
        errors: list[E] = []
        for r in results:
            if r.is_ok():
                values.append(r.unwrap())
            else:
                errors.append(r.unwrap_err())
        if errors:
            return Err(errors)
        return Ok(values)

    @abstractmethod
    def is_ok(self) -> bool:
        """True if this result is Ok."""

    @abstractmethod
    def is_err(self) -> bool:
        """True if this result is Err."""

    @abstractmethod
    def unwrap(self) -> T:
        """Return the inner value if Ok, otherwise raise ValueError."""

    @abstractmethod
    def unwrap_err(self) -> E:
        """Return the inner error if Err, otherwise raise ValueError."""

    @abstractmethod
    def map(self, fn: Callable[[T], U]) -> Result[U, E]:
        """Apply fn to the inner value if Ok, leaving Err unchanged."""

    @abstractmethod
    def map_err(self, fn: Callable[[E], F]) -> Result[T, F]:
        """Apply fn to the inner error if Err, leaving Ok unchanged."""

    @abstractmethod
    def flat_map(self, fn: Callable[[T], Result[U, E]]) -> Result[U, E]:
        """Bind fn over the inner value if Ok, returning the new Result."""

    @abstractmethod
    def fold(self, on_ok: Callable[[T], U], on_err: Callable[[E], U]) -> U:
        """Unwrap and transform the result using the corresponding callback."""


@dataclass(frozen=True)
class Ok(Result[T, Any]):
    """Successful result wrapping a value."""

    value: T

    def is_ok(self) -> bool:
        return True

    def is_err(self) -> bool:
        return False

    def unwrap(self) -> T:
        return self.value

    def unwrap_err(self) -> Any:
        raise ValueError(f"Called unwrap_err on Ok({self.value!r})")

    def map(self, fn: Callable[[T], U]) -> Result[U, Any]:
        return Ok(fn(self.value))

    def map_err(self, fn: Callable[[Any], F]) -> Result[T, F]:
        return self

    def flat_map(self, fn: Callable[[T], Result[U, Any]]) -> Result[U, Any]:
        return fn(self.value)

    def fold(self, on_ok: Callable[[T], U], on_err: Callable[[Any], U]) -> U:
        return on_ok(self.value)


@dataclass(frozen=True)
class Err(Result[Any, E]):
    """Failed result wrapping an error."""

    error: E

    def is_ok(self) -> bool:
        return False

    def is_err(self) -> bool:
        return True

    def unwrap(self) -> Any:
        raise ValueError(f"Called unwrap on Err({self.error!r})")

    def unwrap_err(self) -> E:
        return self.error

    def map(self, fn: Callable[[Any], U]) -> Result[U, E]:
        return self

    def map_err(self, fn: Callable[[E], F]) -> Result[Any, F]:
        return Err(fn(self.error))

    def flat_map(self, fn: Callable[[Any], Result[U, E]]) -> Result[U, E]:
        return self

    def fold(self, on_ok: Callable[[Any], U], on_err: Callable[[E], U]) -> U:
        return on_err(self.error)
