"""Executable specifications for validation scope, cost, and required evidence."""

from __future__ import annotations

import importlib.util
import sys
from dataclasses import dataclass
from pathlib import Path

import pytest

_SPEC = importlib.util.spec_from_file_location(
    "ci_policy", Path(__file__).resolve().parents[2] / "scripts" / "ci_policy.py"
)
assert _SPEC is not None and _SPEC.loader is not None
policy = importlib.util.module_from_spec(_SPEC)
sys.modules[_SPEC.name] = policy
_SPEC.loader.exec_module(policy)


@dataclass(frozen=True)
class PipelineExpectation:
    plan: policy.ValidationPlan

    def to_run_only(self, *jobs: str) -> PipelineExpectation:
        actual = self.plan.selected_jobs()
        assert actual == set(jobs), f"Selected {actual}; wanted {set(jobs)}: {self.plan.reason}"
        return self

    def to_build_on(self, scope: str) -> PipelineExpectation:
        assert self.plan.desktop == scope, f"Desktop scope {self.plan.desktop}, wanted {scope}"
        return self

    def to_exercise_real_probe(self) -> PipelineExpectation:
        assert self.plan.e2e, f"Wire behavior lacks real-probe evidence: {self.plan.reason}"
        return self


def expect_pipeline(*paths: str, ready: bool = True, integration: bool = False) -> PipelineExpectation:
    return PipelineExpectation(policy.select_validation(paths, ready=ready, integration=integration))


@pytest.mark.parametrize("path", ["README.md", "AGENTS.md", "docs/BUILDING.md", ".gitignore", "logs/example/reviewed.log"])
def test_documentation_needs_no_qt_or_python_matrix(path: str) -> None:
    expect_pipeline(path).to_run_only().to_build_on("none")


def test_python_dsl_runs_interpreter_checks_without_native_builds() -> None:
    expect_pipeline("python/src/qtpilot/fluent.py").to_run_only("python").to_build_on("none")
    assert policy.select_validation(("python/src/qtpilot/fluent.py",)).python == "core"


@pytest.mark.parametrize("path", ["python/src/qtpilot/connection.py", "python/src/qtpilot/tools/native.py", "python/tests/test_replay_driver.py"])
def test_python_wire_behavior_has_linux_probe_evidence(path: str) -> None:
    expect_pipeline(path).to_run_only("python", "build").to_build_on("linux").to_exercise_real_probe()


@pytest.mark.parametrize("path", ["python/uv.lock", "python/pyproject.toml", "python/src/qtpilot/_mcp_compat.py"])
def test_sdk_changes_cover_every_python_stack(path: str) -> None:
    plan = policy.select_validation((path,))
    assert plan.python == "all"
    assert plan.e2e


def test_draft_native_work_starts_with_linux_evidence() -> None:
    expect_pipeline("src/probe/transport/websocket_server.cpp", ready=False).to_build_on("linux").to_exercise_real_probe()


@pytest.mark.parametrize("path", ["src/probe/transport/websocket_server.cpp", "src/probe/core/object_id.h", "CMakeLists.txt"])
def test_ready_shared_native_changes_keep_supported_platforms(path: str) -> None:
    expect_pipeline(path).to_build_on("full").to_run_only("lint", "python", "build", "static-probe", "mobile", "sanitizers")


def test_main_push_avoids_repeating_every_ready_native_platform() -> None:
    expect_pipeline("src/probe/core/object_id.cpp", integration=True).to_build_on("linux")


@pytest.mark.parametrize("path", ["unexpected/location.dat", ".github/workflows/ci.yml", "scripts/ci_policy.py"])
def test_unknown_and_policy_changes_select_full_validation_even_in_draft(path: str) -> None:
    expect_pipeline(path, ready=False).to_build_on("full").to_run_only("lint", "python", "build", "static-probe", "mobile", "sanitizers", "benchmarks")


def test_mixed_diff_keeps_the_strongest_scope() -> None:
    expect_pipeline("docs/BUILDING.md", "src/probe/core/probe.cpp").to_build_on("full")


def test_explicit_full_mode_retains_release_evidence_for_empty_diff() -> None:
    plan = policy.select_validation((), full=True)
    PipelineExpectation(plan).to_build_on("full").to_run_only("lint", "python", "build", "static-probe", "mobile", "sanitizers", "benchmarks")


@pytest.mark.parametrize("result", ["failure", "cancelled", "skipped", "missing"])
def test_selected_job_cannot_disappear_or_fail_and_still_pass(result: str) -> None:
    plan = policy.select_validation(("python/src/qtpilot/fluent.py",))
    results = {} if result == "missing" else {"python": result}
    assert policy.failed_requirements(plan, results) == ("python",)


def test_intentionally_unselected_jobs_can_be_skipped() -> None:
    plan = policy.select_validation(("README.md",))
    assert policy.failed_requirements(plan, {"build": "skipped"}) == ()


def test_selected_success_is_sufficient() -> None:
    plan = policy.select_validation(("python/src/qtpilot/fluent.py",))
    assert policy.failed_requirements(plan, {"python": "success", "build": "skipped"}) == ()


def test_fluent_failure_explains_selected_and_expected_jobs() -> None:
    with pytest.raises(AssertionError, match="Selected.*wanted"):
        expect_pipeline("README.md").to_run_only("build")
