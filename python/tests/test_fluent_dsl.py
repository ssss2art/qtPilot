"""Unit tests for the fluent test DSL and matchers."""

from __future__ import annotations

import pytest

from qtpilot.fluent import expect_replay
from qtpilot.replay import Divergence, ReplayResult, Scenario, Step


class TestFluentDsl:
    def test_expect_replay_to_pass_succeeds_on_clean_result(self):
        scenario = Scenario(steps=[Step(index=0)])
        result = ReplayResult(scenario=scenario, steps=[], divergences=[])

        # Fluent assertion returns expectation for chaining
        expect_replay(result).to_pass().to_have_divergence_count(0)

    def test_expect_replay_to_pass_fails_on_divergence(self):
        scenario = Scenario(steps=[Step(index=0)])
        div = Divergence(step=1, kind="observation", method="qt.properties.get", expected="a", actual="b")
        result = ReplayResult(scenario=scenario, steps=[], divergences=[div])

        with pytest.raises(AssertionError, match="Expected replay to pass"):
            expect_replay(result).to_pass()

    def test_expect_replay_to_diverge_at_matches_step_and_kind(self):
        scenario = Scenario(steps=[Step(index=0)])
        div = Divergence(step=2, kind="notification", method="qt.signals.emitted", expected={}, actual=None)
        result = ReplayResult(scenario=scenario, steps=[], divergences=[div])

        expect_replay(result).to_diverge_at(step=2, kind="notification", method="qt.signals.emitted")
        expect_replay(result).to_have_divergence_count(1)

    def test_expect_replay_to_diverge_at_raises_when_no_match(self):
        scenario = Scenario(steps=[Step(index=0)])
        div = Divergence(step=1, kind="observation", method="qt.properties.get", expected=1, actual=2)
        result = ReplayResult(scenario=scenario, steps=[], divergences=[div])

        with pytest.raises(AssertionError, match="No divergence found at step 2"):
            expect_replay(result).to_diverge_at(step=2)

    def test_expect_replay_to_abort_at(self):
        scenario = Scenario(steps=[Step(index=0)])
        result = ReplayResult(
            scenario=scenario,
            steps=[],
            divergences=[],
            aborted_at=3,
            abort_reason="qt.ui.click: widget not found",
        )

        expect_replay(result).to_abort_at(step=3, reason_contains="widget not found")

        with pytest.raises(AssertionError, match="Expected abort at step 2"):
            expect_replay(result).to_abort_at(step=2)
