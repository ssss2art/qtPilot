"""Select validation by changed scope and review stage; no network dependencies."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Literal


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
    if native:
        broad = ready and not integration
        performance = any(path.startswith("benchmarks/") for path in changed)
        return ValidationPlan(
            desktop="full" if broad else "linux", python="core", lint=True,
            static=broad, mobile=broad, sanitizers=broad, benchmarks=performance,
            e2e=True, reason="ready native revision" if broad else "native Linux iteration",
        )
    sdk = any(
        path in {"python/uv.lock", "python/pyproject.toml", "python/.python-version"}
        or path.endswith("/_mcp_compat.py")
        or path.startswith("python/tests/test_mcp")
        for path in changed
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
