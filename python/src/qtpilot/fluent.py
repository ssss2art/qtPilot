"""Fluent matchers and assertions for scenario replay."""

from __future__ import annotations

from typing import TYPE_CHECKING, Any

if TYPE_CHECKING:
    from qtpilot.replay import ReplayResult
    from qtpilot.signal_wait import SignalWaitResult


class ReplayExpectation:
    """Fluent expectation wrapper for ReplayResult assertions."""

    def __init__(self, result: ReplayResult) -> None:
        self._result = result

    def to_pass(self) -> ReplayExpectation:
        """Assert that the replay succeeded with zero divergences and no abort."""
        assert self._result.passed, (
            f"Expected replay to pass, but had {len(self._result.divergences)} "
            f"divergence(s): {self._result.summary()}"
        )
        return self

    def to_diverge(self) -> ReplayExpectation:
        """Assert that the replay detected at least one divergence."""
        assert not self._result.passed, "Expected replay to diverge, but it passed"
        return self

    def to_have_divergence_count(self, count: int) -> ReplayExpectation:
        """Assert exact divergence count."""
        actual = len(self._result.divergences)
        assert actual == count, f"Expected {count} divergences, got {actual}"
        return self

    def to_diverge_at(
        self,
        step: int,
        kind: str | None = None,
        method: str | None = None,
    ) -> ReplayExpectation:
        """Assert that a divergence occurred at `step`, optionally matching kind and method."""
        assert not self._result.passed, "Expected replay to diverge, but it passed"
        matching = [
            d
            for d in self._result.divergences
            if d.step == step
            and (kind is None or d.kind == kind)
            and (method is None or d.method == method)
        ]
        assert matching, (
            f"No divergence found at step {step} (kind={kind}, method={method}). "
            f"Divergences: {self._result.divergences}"
        )
        return self

    def to_abort_at(
        self, step: int, reason_contains: str | None = None
    ) -> ReplayExpectation:
        """Assert that the replay aborted at `step`."""
        assert self._result.aborted_at == step, (
            f"Expected abort at step {step}, got {self._result.aborted_at}"
        )
        if reason_contains:
            assert reason_contains in (self._result.abort_reason or ""), (
                f"Expected abort reason containing {reason_contains!r}, "
                f"got {self._result.abort_reason!r}"
            )
        return self


def expect_replay(result: ReplayResult) -> ReplayExpectation:
    """Entry point for fluent replay assertions."""
    return ReplayExpectation(result)


class SignalWaitExpectation:
    """Fluent expectation wrapper for the outcome of waiting on a signal."""

    def __init__(self, result: SignalWaitResult) -> None:
        self._result = result

    def to_emit(
        self, signal: str | None = None, arguments: list[Any] | None = None
    ) -> SignalWaitExpectation:
        """Assert the signal fired, optionally with this name and these arguments."""
        assert self._result.is_ok(), f"Expected an emission, got {self._result.unwrap_err()}"
        emitted = self._result.unwrap()
        if signal is not None:
            assert emitted.signal == signal, f"Expected signal {signal!r}, got {emitted.signal!r}"
        if arguments is not None:
            assert list(emitted.arguments) == arguments, (
                f"Expected arguments {arguments!r}, got {list(emitted.arguments)!r}"
            )
        return self

    def to_time_out(self) -> SignalWaitExpectation:
        """Assert that nothing was emitted before the timeout."""
        return self.to_fail_as("timedOut")

    def to_fail_as(self, reason: str) -> SignalWaitExpectation:
        """Assert the wait ended without an emission, for this reason."""
        assert self._result.is_err(), f"Expected {reason}, got {self._result.unwrap()}"
        actual = self._result.unwrap_err().reason
        assert actual == reason, f"Expected {reason}, got {actual}"
        return self


def expect_signal_wait(result: SignalWaitResult) -> SignalWaitExpectation:
    """Entry point for fluent signal-wait assertions."""
    return SignalWaitExpectation(result)


class WireMethodsExpectation:
    """Fluent expectation over the JSON-RPC method names a client sends."""

    def __init__(self, sent: set[str]) -> None:
        self._sent = sent

    def to_be_registered_by(self, registered: set[str]) -> WireMethodsExpectation:
        """Assert the probe registers every method sent, so none fails as method-not-found."""
        unknown = sorted(self._sent - registered)
        assert not unknown, f"Sent but never registered by the probe: {unknown}"
        return self


def expect_wire_methods(sent: set[str]) -> WireMethodsExpectation:
    """Entry point for fluent assertions on sent JSON-RPC method names."""
    return WireMethodsExpectation(sent)
