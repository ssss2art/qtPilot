"""Select validation by changed scope and review stage; no network dependencies."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Literal

MatrixEntry = dict[str, str]


def desktop_matrix(plan: ValidationPlan) -> list[MatrixEntry]:
    catalog: dict[str, list[MatrixEntry]] = json.loads(Path(__file__).with_name("ci-matrix.json").read_text())
    entries = catalog["build"]
    if plan.desktop == "full":
        return entries
    if plan.desktop == "none":
        return []
    return [entry for entry in entries if entry["platform"] == "linux" and (
        (entry["qt"] == "6.10.0" and (plan.lint or not entry.get("extra_cmake")))
        or (plan.lint and entry["qt"] == "5.15.2")
    )]


def changed_paths(base: str, head: str, *, merge_base: bool = True) -> tuple[str, ...] | None:
    comparison = f"{base}...{head}" if merge_base else f"{base}..{head}"
    result = subprocess.run(
        ["git", "diff", "--name-only", "--no-renames", "-z", comparison, "--"],
        capture_output=True, check=False,
    )
    if result.returncode:
        return None
    return tuple(os.fsdecode(path) for path in result.stdout.split(b"\0") if path)


@dataclass(frozen=True, slots=True)
class ValidationPlan:
    desktop: Literal["none", "linux", "full"] = "none"
    python: Literal["none", "core", "all"] = "none"
    lint: bool = False
    static: bool = False
    mobile: bool = False
    sanitizers: bool = False
    benchmarks: bool = False
    e2e: bool = False
    reason: str = "documentation only"

    def selected_jobs(self) -> set[str]:
        choices = {
            "lint": self.lint,
            "python": self.python != "none",
            "build": self.desktop != "none",
            "static-probe": self.static,
            "mobile": self.mobile,
            "sanitizers": self.sanitizers,
            "benchmarks": self.benchmarks,
        }
        return {job for job, selected in choices.items() if selected}


def _documentation(path: str) -> bool:
    return path in {"AGENTS.md", ".gitignore", ".markdownlint.json"} or (
        path.endswith(".md") and not path.startswith(("scripts/", ".github/"))
    ) or path.startswith(("docs/", "logs/"))


def _full(reason: str) -> ValidationPlan:
    return ValidationPlan("full", "all", True, True, True, True, True, True, reason)


def select_validation(
    paths: tuple[str, ...], *, ready: bool = True, integration: bool = False, full: bool = False
) -> ValidationPlan:
    if full or not paths:
        return _full("release, explicit full request, or unavailable diff")
    changed = tuple(path for path in paths if not _documentation(path))
    if not changed:
        return ValidationPlan()
    native_prefixes = ("src/", "tests/", "test_app/", "test_app_qml/", "cmake/", "benchmarks/")
    native_files = {"CMakeLists.txt", "CMakePresets.json"}
    native = any(path.startswith(native_prefixes) or path in native_files for path in changed)
    unknown = any(
        not (path.startswith(("python/", *native_prefixes)) or path in native_files)
        for path in changed
    )
    if unknown:
        return _full("unknown path or validation infrastructure changed")
    sdk = any(
        path in {"python/uv.lock", "python/pyproject.toml", "python/.python-version"}
        or path.endswith("/_mcp_compat.py")
        or path == "python/src/qtpilot/server.py"
        or path.startswith("python/src/qtpilot/tools/")
        or path.startswith("python/tests/test_mcp")
        for path in changed
    )
    if native:
        broad = ready and not integration
        performance = any(path.startswith("benchmarks/") for path in changed)
        return ValidationPlan(
            desktop="full" if broad else "linux", python="all" if sdk else "core", lint=True,
            static=broad, mobile=broad, sanitizers=broad, benchmarks=performance,
            e2e=True, reason="ready native revision" if broad else "native Linux iteration",
        )
    pure_files = {
        "python/src/qtpilot/fluent.py", "python/src/qtpilot/result.py",
        "python/src/qtpilot/tree_format.py", "python/tests/test_fluent_dsl.py",
        "python/tests/test_result_monad.py", "python/tests/test_tree_format.py",
    }
    wire = sdk or any(path not in pure_files for path in changed)
    return ValidationPlan(
        desktop="linux" if wire else "none", python="all" if sdk else "core",
        e2e=wire, reason="Python wire behavior" if wire else "Python domain-only behavior",
    )


def failed_requirements(plan: ValidationPlan, results: dict[str, str]) -> tuple[str, ...]:
    return tuple(sorted(job for job in plan.selected_jobs() if results.get(job) != "success"))


def github_plan(event: dict[str, object], event_name: str, ref: str) -> ValidationPlan:
    """Use the entire PR diff; failed/missing Git history widens validation."""
    if event_name in {"workflow_dispatch", "workflow_call"} or ref.startswith("refs/tags/"):
        return _full("explicit dispatch or release")
    if event_name == "pull_request":
        pr = event.get("pull_request")
        if not isinstance(pr, dict):
            return _full("missing PR context")
        base, head = pr.get("base", {}), pr.get("head", {})
        paths = changed_paths(str(base.get("sha", "")), str(head.get("sha", "")))
        return select_validation(paths or (), ready=not pr.get("draft", True))
    paths = changed_paths(str(event.get("before", "")), os.environ.get("GITHUB_SHA", "HEAD"), merge_base=False)
    return select_validation(paths or (), integration=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("plan", "gate"))
    args = parser.parse_args()
    if args.action == "gate":
        plan = ValidationPlan(**json.loads(os.environ["VALIDATION_PLAN"]))
        needs: dict[str, dict[str, str]] = json.loads(os.environ["VALIDATION_RESULTS"])
        results = {job: result.get("result", "missing") for job, result in needs.items()}
        failures = failed_requirements(plan, results)
        print(f"Selected: {sorted(plan.selected_jobs())}; unmet: {list(failures)}")
        return int(bool(failures) or results.get("plan") != "success")
    event: dict[str, object] = json.loads(Path(os.environ["GITHUB_EVENT_PATH"]).read_text())
    plan = github_plan(event, os.environ["GITHUB_EVENT_NAME"], os.environ["GITHUB_REF"])
    catalog: dict[str, list[MatrixEntry]] = json.loads(Path(__file__).with_name("ci-matrix.json").read_text())
    outputs = {
        "plan": json.dumps(asdict(plan), separators=(",", ":")),
        "desktop_matrix": json.dumps({"include": desktop_matrix(plan)}, separators=(",", ":")),
        "python_matrix": json.dumps({"include": [entry for entry in catalog["python"]
            if plan.python == "all" or entry["name"] in {"py3.11", "py3.14"}]}, separators=(",", ":")),
        **{job.replace("-", "_"): str(job in plan.selected_jobs()).lower()
           for job in ("lint", "python", "build", "static-probe", "mobile", "sanitizers", "benchmarks")},
        "e2e": str(plan.e2e).lower(),
        "release": str(os.environ["GITHUB_REF"].startswith("refs/tags/")).lower(),
    }
    with Path(os.environ["GITHUB_OUTPUT"]).open("a") as output:
        for key, value in outputs.items():
            output.write(f"{key}={value}\n")
    summary = f"## Validation scope\n\n{plan.reason}\n\nSelected: {', '.join(sorted(plan.selected_jobs())) or 'policy/docs checks only'}\n"
    with Path(os.environ["GITHUB_STEP_SUMMARY"]).open("a") as output:
        output.write(summary)
    print(summary)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
