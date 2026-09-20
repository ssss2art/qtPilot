"""Unit tests for the Result monad and monadic replay composition."""

from __future__ import annotations

import pytest

from qtpilot.result import Err, Ok, Result


class TestResultMonad:
    def test_ok_creation_and_properties(self):
        res: Result[int, str] = Ok(42)
        assert res.is_ok() is True
        assert res.is_err() is False
        assert res.unwrap() == 42
        with pytest.raises(ValueError, match="Called unwrap_err on Ok"):
            res.unwrap_err()

    def test_err_creation_and_properties(self):
        res: Result[int, str] = Err("boom")
        assert res.is_ok() is False
        assert res.is_err() is True
        assert res.unwrap_err() == "boom"
        with pytest.raises(ValueError, match="Called unwrap on Err"):
            res.unwrap()

    def test_map(self):
        ok: Result[int, str] = Ok(10)
        mapped = ok.map(lambda x: x * 2)
        assert mapped == Ok(20)

        err: Result[int, str] = Err("failed")
        mapped_err = err.map(lambda x: x * 2)
        assert mapped_err == Err("failed")

    def test_map_err(self):
        ok: Result[int, str] = Ok(10)
        assert ok.map_err(lambda e: f"E: {e}") == Ok(10)

        err: Result[int, str] = Err("failed")
        assert err.map_err(lambda e: f"E: {e}") == Err("E: failed")

    def test_flat_map(self):
        def divide(n: int, d: int) -> Result[float, str]:
            if d == 0:
                return Err("division by zero")
            return Ok(n / d)

        assert Ok(10).flat_map(lambda x: divide(x, 2)) == Ok(5.0)
        assert Ok(10).flat_map(lambda x: divide(x, 0)) == Err("division by zero")
        assert Err("prev error").flat_map(lambda x: divide(x, 2)) == Err("prev error")

    def test_fold(self):
        ok: Result[int, str] = Ok(10)
        assert ok.fold(on_ok=lambda x: f"ok:{x}", on_err=lambda e: f"err:{e}") == "ok:10"

        err: Result[int, str] = Err("bad")
        assert err.fold(on_ok=lambda x: f"ok:{x}", on_err=lambda e: f"err:{e}") == "err:bad"

    def test_collect_results(self):
        """Monadic sequence/collect: all Ok -> Ok([values]), any Err -> Err([errors])."""
        all_ok = [Ok(1), Ok(2), Ok(3)]
        assert Result.collect(all_ok) == Ok([1, 2, 3])

        some_err = [Ok(1), Err("e1"), Ok(2), Err("e2")]
        assert Result.collect(some_err) == Err(["e1", "e2"])

    def test_replay_result_to_result(self):
        from qtpilot.replay import Divergence, ReplayResult, Scenario, Step

        scenario = Scenario(steps=[Step(index=0)])
        clean = ReplayResult(scenario=scenario, steps=[Step(index=0)], divergences=[])
        res_ok = clean.to_result()
        assert res_ok.is_ok()
        assert res_ok.unwrap() == [Step(index=0)]

        div = Divergence(1, "observation", "qt.properties.get", "a", "b")
        diverged = ReplayResult(scenario=scenario, steps=[], divergences=[div])
        res_err = diverged.to_result()
        assert res_err.is_err()
        assert res_err.unwrap_err() == [div]

