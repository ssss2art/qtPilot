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

    def test_parse_entries_expected(self):
        from qtpilot.replay import parse_entries_expected

        valid_entries = [
            {"dir": "req", "id": 1, "method": "qt.ui.click", "params": {"objectId": "btn"}},
            {"dir": "res", "id": 1, "method": "qt.ui.click", "result": {"ok": True}},
        ]
        res_ok = parse_entries_expected(valid_entries)
        assert res_ok.is_ok()
        assert len(res_ok.unwrap().steps) == 2

        invalid_entries = [
            {"dir": "res", "id": 99, "method": "qt.ui.click", "result": {"ok": True}},
        ]
        res_err = parse_entries_expected(invalid_entries)
        assert res_err.is_err()
        assert "no matching request" in res_err.unwrap_err()

    def test_load_scenario_expected(self, tmp_path):
        from qtpilot.replay import load_scenario_expected

        bad_file = tmp_path / "bad.jsonl"
        bad_file.write_text("not json", encoding="utf-8")
        res_bad = load_scenario_expected(bad_file)
        assert res_bad.is_err()
        assert "is not valid JSON" in res_bad.unwrap_err()

        good_file = tmp_path / "good.jsonl"
        good_file.write_text(
            '{"dir":"req","id":1,"method":"qt.ui.click","params":{}}\n{"dir":"res","id":1,"method":"qt.ui.click","result":{}}\n',
            encoding="utf-8",
        )
        res_good = load_scenario_expected(good_file)
        assert res_good.is_ok()
        assert len(res_good.unwrap().steps) == 2

    @pytest.mark.asyncio
    async def test_run_scenario_expected(self):
        from qtpilot.replay import Scenario, Step, run_scenario_expected

        empty_scenario = Scenario(steps=[Step(index=0)])
        res = await run_scenario_expected(empty_scenario, None)
        assert res.is_err()
        assert "nothing to replay" in res.unwrap_err()

    def test_and_then_alias(self):
        """and_then provides 1:1 parity with C++23 std::expected::and_then and Rust."""
        ok = Ok(10)
        assert ok.and_then(lambda x: Ok(x + 5)) == Ok(15)
        assert ok.and_then(lambda x: Err("failed")) == Err("failed")
        err = Err("already failed")
        assert err.and_then(lambda x: Ok(x + 5)) == Err("already failed")

    def test_or_else(self):
        """or_else provides error recovery fallback chains."""
        ok = Ok(10)
        assert ok.or_else(lambda e: Ok(99)) == Ok(10)

        err: Result[int, str] = Err("primary failed")
        recovered = err.or_else(lambda e: Ok(42))
        assert recovered == Ok(42)

        still_failed = err.or_else(lambda e: Err(f"secondary: {e}"))
        assert still_failed == Err("secondary: primary failed")

    def test_unwrap_or_and_unwrap_or_else(self):
        ok = Ok("value")
        assert ok.unwrap_or("fallback") == "value"
        assert ok.unwrap_or_else(lambda e: f"fallback {e}") == "value"

        err = Err("timeout")
        assert err.unwrap_or("fallback") == "fallback"
        assert err.unwrap_or_else(lambda e: f"fallback on {e}") == "fallback on timeout"

    def test_tap_and_tap_err(self):
        side_effects: list[str] = []

        ok = Ok("success")
        ret_ok = ok.tap(lambda v: side_effects.append(f"ok:{v}"))
        assert ret_ok == Ok("success")
        assert side_effects == ["ok:success"]

        side_effects.clear()
        ret_ok2 = ok.tap_err(lambda e: side_effects.append(f"err:{e}"))
        assert ret_ok2 == Ok("success")
        assert side_effects == []

        err = Err("failure")
        ret_err = err.tap(lambda v: side_effects.append(f"ok:{v}"))
        assert ret_err == Err("failure")
        assert side_effects == []

        ret_err2 = err.tap_err(lambda e: side_effects.append(f"err:{e}"))
        assert ret_err2 == Err("failure")
        assert side_effects == ["err:failure"]

    def test_from_callable(self):
        def parse_int(s: str) -> int:
            return int(s)

        res_ok = Result.from_callable(parse_int, "42")
        assert res_ok == Ok(42)

        res_err = Result.from_callable(parse_int, "not_a_number")
        assert res_err.is_err()
        assert isinstance(res_err.unwrap_err(), ValueError)

    def test_structural_pattern_matching(self):
        def inspect_res(r: Result[int, str]) -> str:
            match r:
                case Ok(val):
                    return f"value:{val}"
                case Err(err):
                    return f"error:{err}"

        assert inspect_res(Ok(100)) == "value:100"
        assert inspect_res(Err("oops")) == "error:oops"

    def test_monad_laws_left_identity(self):
        """Monad Law 1: Ok(x).flat_map(f) == f(x)."""
        f = lambda x: Ok(x * 3)
        x = 7
        assert Ok(x).flat_map(f) == f(x)
        assert Ok(x).and_then(f) == f(x)

    def test_monad_laws_right_identity(self):
        """Monad Law 2: m.flat_map(Ok) == m."""
        ok_m = Ok(42)
        assert ok_m.flat_map(Ok) == ok_m
        assert ok_m.and_then(Ok) == ok_m

        err_m: Result[int, str] = Err("law2_err")
        assert err_m.flat_map(Ok) == err_m
        assert err_m.and_then(Ok) == err_m

    def test_monad_laws_associativity(self):
        """Monad Law 3: (m >>= f) >>= g == m >>= (lambda x: f(x) >>= g)."""
        m = Ok(10)
        f = lambda x: Ok(x + 2)
        g = lambda y: Ok(y * 5)

        lhs = m.flat_map(f).flat_map(g)
        rhs = m.flat_map(lambda x: f(x).flat_map(g))
        assert lhs == rhs == Ok(60)

