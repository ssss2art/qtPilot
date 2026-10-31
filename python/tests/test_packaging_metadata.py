"""Metadata checks on pyproject.toml (red/green for packaging-review findings).

  * fastmcp floor was >=2.0 but the code imports fastmcp.server.middleware,
    which only exists in FastMCP >= 2.9.
  * websockets floor was >=14, higher than the code needs (>=13), narrowing the
    resolver against fastmcp's own websockets pin.
  * classifiers omitted Python 3.13.
  * No LICENSE shipped in the distribution.

stdlib-only (tomllib, py3.11+):
    pytest python/tests/test_packaging_metadata.py --noconftest
"""

from __future__ import annotations

import re
import tomllib
from pathlib import Path

PYPROJECT = Path(__file__).resolve().parents[1] / "pyproject.toml"
PROJECT_DIR = PYPROJECT.parent
_DATA = tomllib.loads(PYPROJECT.read_text())
_PROJECT = _DATA["project"]
_DEPS = _PROJECT["dependencies"]


def _floor(pkg: str) -> tuple[int, ...] | None:
    for dep in _DEPS:
        if re.match(rf"^{pkg}\b", dep.replace(" ", ""), re.IGNORECASE):
            m = re.search(r">=\s*([0-9]+(?:\.[0-9]+)*)", dep)
            return tuple(int(x) for x in m.group(1).split(".")) if m else None
    return None


def test_fastmcp_floor_at_least_2_9() -> None:
    floor = _floor("fastmcp")
    assert floor is not None, "fastmcp must declare a lower bound"
    assert floor >= (2, 9), f"fastmcp floor {floor} predates fastmcp.server.middleware (2.9)"


def test_websockets_floor_not_needlessly_high() -> None:
    floor = _floor("websockets")
    assert floor is not None
    # The code uses websockets.asyncio.client (>=13); 14 was needlessly high.
    assert floor[0] <= 13


def test_python_313_classifier_present() -> None:
    classifiers = _PROJECT.get("classifiers", [])
    assert "Programming Language :: Python :: 3.13" in classifiers


def test_license_shipped() -> None:
    # PEP 639 license-files must be declared and the referenced file present in
    # the project dir so it lands in the sdist/wheel.
    license_files = _PROJECT.get("license-files")
    assert license_files, "project.license-files must be set so LICENSE is packaged"
    assert (PROJECT_DIR / "LICENSE").exists(), "python/LICENSE must exist to be packaged"


def test_package_not_found_version_fallback(monkeypatch: pytest.MonkeyPatch) -> None:
    """Ensure qtpilot imports cleanly without raising PackageNotFoundError if uninstalled."""
    import importlib
    import importlib.metadata
    import sys
    import pytest

    def fake_version(pkg: str) -> str:
        raise importlib.metadata.PackageNotFoundError(pkg)

    monkeypatch.setattr(importlib.metadata, "version", fake_version)
    if "qtpilot" in sys.modules:
        monkeypatch.delitem(sys.modules, "qtpilot")

    import qtpilot

    assert isinstance(qtpilot.__version__, str)
    assert len(qtpilot.__version__) > 0


def _parse_version_tuple(version_str: str) -> tuple[int, ...]:
    head = re.match(r"^v?([0-9]+(?:\.[0-9]+)*)", version_str.strip())
    assert head is not None, f"Cannot parse version tuple from {version_str!r}"
    return tuple(int(part) for part in head.group(1).split("."))


def test_uv_lock_minimum_security_and_sdk_versions() -> None:
    lock_path = PROJECT_DIR / "uv.lock"
    lock_data = tomllib.loads(lock_path.read_text())
    packages = lock_data.get("package", [])
    versions_by_name: dict[str, list[tuple[int, ...]]] = {}
    for pkg in packages:
        name = pkg["name"]
        if name == "qtpilot":
            continue
        ver = _parse_version_tuple(pkg["version"])
        versions_by_name.setdefault(name, []).append(ver)
        sdist = pkg.get("sdist")
        if sdist is not None:
            assert str(sdist.get("hash", "")).startswith("sha256:"), f"{name} sdist missing sha256"
        for wheel in pkg.get("wheels", []):
            assert str(wheel.get("hash", "")).startswith("sha256:"), f"{name} wheel missing sha256"

    expected_minimums: dict[str, tuple[int, ...]] = {
        "authlib": (1, 9, 1),
        "certifi": (2026, 10, 23),
        "cryptography": (50, 0, 2),
        "joserfc": (1, 7, 7),
        "pyjwt": (2, 15, 1),
        "python-multipart": (0, 0, 33),
        "starlette": (1, 10, 0),
        "websockets": (17, 4),
        "pytest": (9, 2, 0),
        "pytest-asyncio": (1, 4, 1),
    }
    for pkg_name, min_ver in expected_minimums.items():
        locked = versions_by_name.get(pkg_name, [])
        assert locked, f"{pkg_name} missing from uv.lock"
        assert min(locked) >= min_ver, f"{pkg_name} locked at {locked}, expected >= {min_ver}"

    fastmcp_versions = sorted(versions_by_name.get("fastmcp", []))
    assert any(v[0] == 3 and v >= (3, 4, 11) for v in fastmcp_versions), (
        f"Expected fastmcp 3.x >= 3.4.11 in uv.lock, got {fastmcp_versions}"
    )
    assert any(v[0] == 4 and v >= (4, 1, 4) for v in fastmcp_versions), (
        f"Expected fastmcp 4.x >= 4.1.4 in uv.lock, got {fastmcp_versions}"
    )

    mcp_versions = sorted(versions_by_name.get("mcp", []))
    assert any(v[0] == 1 and v >= (1, 30, 0) for v in mcp_versions), (
        f"Expected mcp 1.x >= 1.30.0 in uv.lock, got {mcp_versions}"
    )
    assert any(v[0] == 2 and v >= (2, 3, 0) for v in mcp_versions), (
        f"Expected mcp 2.x >= 2.3.0 in uv.lock, got {mcp_versions}"
    )


def test_cmake_fetchcontent_minimum_versions() -> None:
    repo_root = PROJECT_DIR.parent
    checks = (
        (repo_root / "tests" / "CMakeLists.txt", "googletest", (1, 18, 0)),
        (repo_root / "benchmarks" / "CMakeLists.txt", "googlebenchmark", (1, 10, 0)),
        (repo_root / "CMakeLists.txt", "detours", (4, 0, 1)),
    )
    for cmake_file, dep_name, min_ver in checks:
        content = cmake_file.read_text()
        match = re.search(
            rf"FetchContent_Declare\(\s*{dep_name}\b.*?GIT_TAG\s+(v?[0-9]+(?:\.[0-9]+)+)",
            content,
            re.DOTALL,
        )
        assert match is not None, f"Could not find FetchContent_Declare({dep_name}) in {cmake_file}"
        actual = _parse_version_tuple(match.group(1))
        assert actual >= min_ver, f"{dep_name} in {cmake_file.name} is {actual}, expected >= {min_ver}"


def test_github_workflows_pin_immutable_shas_and_consistent_uv() -> None:
    workflows_dir = PROJECT_DIR.parent / ".github" / "workflows"
    uv_versions: set[str] = set()
    for wf_path in sorted(workflows_dir.glob("*.yml")):
        text = wf_path.read_text()
        for line in text.splitlines():
            uses_match = re.match(r"^\s*-?\s*uses:\s*([^\s#]+)", line)
            if uses_match:
                target = uses_match.group(1)
                if target.startswith("./"):
                    continue
                assert re.match(r"^[^@]+@[0-9a-f]{40}$", target), (
                    f"{wf_path.name} has non-SHA-pinned action: {target}"
                )
        for uv_match in re.finditer(r"astral-sh/setup-uv@[0-9a-f]{40}.*?version:\s*\"([^\"]+)\"", text, re.DOTALL):
            uv_versions.add(uv_match.group(1))

    assert len(uv_versions) == 1, f"Expected a single consistent uv version across workflows, got {uv_versions}"
    uv_ver = _parse_version_tuple(next(iter(uv_versions)))
    assert uv_ver >= (0, 13, 6), f"Workflow uv version {uv_ver} is older than (0, 13, 6)"


