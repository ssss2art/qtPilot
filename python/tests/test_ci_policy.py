"""Executable specifications for validation scope, cost, and required evidence."""

from __future__ import annotations

import importlib.util
import sys
import json
import os
import subprocess
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


def test_native_plus_sdk_changes_do_not_drop_python_compatibility() -> None:
    plan = policy.select_validation(("src/probe/core/probe.cpp", "python/uv.lock"))
    assert plan.python == "all", "A combined diff must retain SDK compatibility coverage"


def test_fast_wire_lane_builds_only_one_representative_linux_kit() -> None:
    plan = policy.select_validation(("python/src/qtpilot/replay.py",))
    matrix = policy.desktop_matrix(plan)
    assert [(entry["qt"], entry["os"]) for entry in matrix] == [("6.10.0", "ubuntu-24.04")]


def test_draft_native_lane_keeps_qt_floor_and_no_qml_compile_path() -> None:
    plan = policy.select_validation(("src/probe/core/probe.cpp",), ready=False)
    matrix = policy.desktop_matrix(plan)
    assert {entry["qt"] for entry in matrix} == {"5.15.2", "6.10.0"}
    assert any(entry.get("arch_label") == "noqml" for entry in matrix)


def test_full_release_selection_keeps_the_entire_artifact_catalog() -> None:
    catalog = json.loads((Path(__file__).resolve().parents[2] / "scripts/ci-matrix.json").read_text())
    assert policy.desktop_matrix(policy.select_validation((), full=True)) == catalog["build"]


def test_diff_lookup_failure_is_explicit_full_validation() -> None:
    assert policy.changed_paths("missing-base", "missing-head") is None
    assert policy.select_validation(policy.changed_paths("missing-base", "missing-head") or ()).desktop == "full"


@pytest.mark.parametrize("outcome", ["success", "failure", "skipped", "cancelled"])
def test_gate_cli_reports_the_actual_selected_job_outcome(outcome: str) -> None:
    plan = policy.select_validation(("python/src/qtpilot/fluent.py",))
    env = dict(os.environ, VALIDATION_PLAN=json.dumps(policy.asdict(plan)),
        VALIDATION_RESULTS=json.dumps({"plan": {"result": "success"}, "python": {"result": outcome}}))
    result = subprocess.run([sys.executable, str(Path(policy.__file__)), "gate"], env=env,
        capture_output=True, text=True)
    assert result.returncode == (0 if outcome == "success" else 1), result.stdout + result.stderr


def test_gate_cannot_pass_when_the_classifier_failed() -> None:
    env = dict(os.environ, VALIDATION_PLAN=json.dumps(policy.asdict(policy.ValidationPlan())),
        VALIDATION_RESULTS=json.dumps({"plan": {"result": "failure"}}))
    result = subprocess.run([sys.executable, str(Path(policy.__file__)), "gate"], env=env,
        capture_output=True, text=True)
    assert result.returncode == 1


def test_release_context_selects_all_even_if_tag_diff_was_documentation() -> None:
    plan = policy.github_plan({}, "push", "refs/tags/v1.0.0")
    PipelineExpectation(plan).to_build_on("full").to_exercise_real_probe()


def test_pr_event_wiring_is_always_triggered_and_has_a_final_gate() -> None:
    workflow = (Path(__file__).resolve().parents[2] / ".github/workflows/ci.yml").read_text()
    assert "ready_for_review" in workflow, "Readying a draft must request native validation"
    assert "name: Validation gate" in workflow, "Branch rules need a stable selected-job gate"
    assert 'python scripts/ci_policy.py gate' in workflow
    assert 'python scripts/ci_policy.py plan' in workflow
    push_block = workflow.split("  push:", 1)[1].split("  pull_request:", 1)[0]
    assert "paths:" not in push_block, "All changed paths must reach the classifier"
