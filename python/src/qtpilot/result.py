"""Monadic Result types for functional error handling and deterministic pipelines."""

from __future__ import annotations

from abc import ABC, abstractmethod
from collections.abc import Callable, Sequence
from dataclasses import dataclass
from typing import Any, Generic, Never, TypeVar

T_co = TypeVar("T_co", covariant=True)
E_co = TypeVar("E_co", covariant=True)
U = TypeVar("U")
F = TypeVar("F")


class Result(ABC, Generic[T_co, E_co]):
    """Monadic container representing either success (Ok) or failure (Err)."""

    @classmethod
    def ok(cls, value: T_co) -> Result[T_co, Never]:
        return Ok(value)

    @classmethod
    def err(cls, error: E_co) -> Result[Never, E_co]:
        return Err(error)

    @classmethod
    def from_callable(
        cls, fn: Callable[..., U], *args: Any, **kwargs: Any
    ) -> Result[U, Exception]:
        """Execute fn(*args, **kwargs), lifting result into Ok or caught Exception into Err."""
        try:
            return Ok(fn(*args, **kwargs))
        except Exception as ex:
            return Err(ex)

    @classmethod
    def collect(
        cls, results: Sequence[Result[T_co, E_co]]
    ) -> Result[list[T_co], list[E_co]]:
        """Sequence a list of Results into a Result containing either all values or all errors."""
        values: list[T_co] = []
        errors: list[E_co] = []
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
    def unwrap(self) -> T_co:
        """Return the inner value if Ok, otherwise raise ValueError."""

    @abstractmethod
    def unwrap_err(self) -> E_co:
        """Return the inner error if Err, otherwise raise ValueError."""

    @abstractmethod
    def unwrap_or(self, default: Any) -> Any:
        """Return the inner value if Ok, otherwise default."""

    @abstractmethod
    def unwrap_or_else(self, fn: Callable[[E_co], Any]) -> Any:
        """Return the inner value if Ok, otherwise call fn(error)."""

    @abstractmethod
    def map(self, fn: Callable[[T_co], U]) -> Result[U, E_co]:
        """Apply fn to the inner value if Ok, leaving Err unchanged."""

    @abstractmethod
    def map_err(self, fn: Callable[[E_co], F]) -> Result[T_co, F]:
        """Apply fn to the inner error if Err, leaving Ok unchanged."""

    @abstractmethod
    def flat_map(self, fn: Callable[[T_co], Result[U, Any]]) -> Result[U, Any]:
        """Bind fn over the inner value if Ok, returning the new Result."""

    def and_then(self, fn: Callable[[T_co], Result[U, Any]]) -> Result[U, Any]:
        """Alias for flat_map, matching C++23 std::expected and Rust naming."""
        return self.flat_map(fn)

    @abstractmethod
    def or_else(self, fn: Callable[[E_co], Result[Any, F]]) -> Result[Any, F]:
        """Monadic error recovery: call fn if Err, returning the recovery Result."""

    @abstractmethod
    def tap(self, fn: Callable[[T_co], None]) -> Result[T_co, E_co]:
        """Execute a side effect on the inner value if Ok, returning self."""

    @abstractmethod
    def tap_err(self, fn: Callable[[E_co], None]) -> Result[T_co, E_co]:
        """Execute a side effect on the inner error if Err, returning self."""

    @abstractmethod
    def fold(self, on_ok: Callable[[T_co], U], on_err: Callable[[E_co], U]) -> U:
        """Unwrap and transform the result using the corresponding callback."""


@dataclass(frozen=True, slots=True)
class Ok(Result[T_co, Never]):
    """Successful result wrapping a value."""

    value: T_co
    __match_args__ = ("value",)

    def is_ok(self) -> bool:
        return True

    def is_err(self) -> bool:
        return False

    def unwrap(self) -> T_co:
        return self.value

    def unwrap_err(self) -> Never:
        raise ValueError(f"Called unwrap_err on Ok({self.value!r})")

    def unwrap_or(self, default: Any) -> T_co:
        return self.value

    def unwrap_or_else(self, fn: Callable[[Never], Any]) -> T_co:
        return self.value

    def map(self, fn: Callable[[T_co], U]) -> Result[U, Never]:
        return Ok(fn(self.value))

    def map_err(self, fn: Callable[[Never], F]) -> Result[T_co, F]:
        return self

    def flat_map(self, fn: Callable[[T_co], Result[U, Any]]) -> Result[U, Any]:
        return fn(self.value)

    def or_else(self, fn: Callable[[Never], Result[Any, F]]) -> Result[T_co, F]:
        return self

    def tap(self, fn: Callable[[T_co], None]) -> Result[T_co, Never]:
        fn(self.value)
        return self

    def tap_err(self, fn: Callable[[Never], None]) -> Result[T_co, Never]:
        return self

    def fold(self, on_ok: Callable[[T_co], U], on_err: Callable[[Never], U]) -> U:
        return on_ok(self.value)


@dataclass(frozen=True, slots=True)
class Err(Result[Never, E_co]):
    """Failed result wrapping an error."""

    error: E_co
    __match_args__ = ("error",)

    def is_ok(self) -> bool:
        return False

    def is_err(self) -> bool:
        return True

    def unwrap(self) -> Never:
        raise ValueError(f"Called unwrap on Err({self.error!r})")

    def unwrap_err(self) -> E_co:
        return self.error

    def unwrap_or(self, default: Any) -> Any:
        return default

    def unwrap_or_else(self, fn: Callable[[E_co], Any]) -> Any:
        return fn(self.error)

    def map(self, fn: Callable[[Never], U]) -> Result[U, E_co]:
        return self

    def map_err(self, fn: Callable[[E_co], F]) -> Result[Never, F]:
        return Err(fn(self.error))

    def flat_map(self, fn: Callable[[Never], Result[U, Any]]) -> Result[U, E_co]:
        return self

    def or_else(self, fn: Callable[[E_co], Result[Any, F]]) -> Result[Any, F]:
        return fn(self.error)

    def tap(self, fn: Callable[[Never], None]) -> Result[Never, E_co]:
        return self

    def tap_err(self, fn: Callable[[E_co], None]) -> Result[Never, E_co]:
        fn(self.error)
        return self

    def fold(self, on_ok: Callable[[Never], U], on_err: Callable[[E_co], U]) -> U:
        return on_err(self.error)
