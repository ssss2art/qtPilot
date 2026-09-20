"""Fluent matchers and assertions for scenario replay."""

from __future__ import annotations

from typing import TYPE_CHECKING, Any

if TYPE_CHECKING:
    from qtpilot.replay import ReplayResult


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
