"""Consistency between the shipped build matrix / version config and CI.

  * BUILD_MATRIX in download.py must equal what .github/workflows/ci.yml builds,
    so the two never drift (a drift = a `download-tools` 404).
  * release.yml checkouts must use fetch-depth: 0 so hatch-vcs can derive the
    version from git tags at publish time (a shallow clone can silently publish
    0.0.0 / a .dev version, which PyPI then refuses to let you re-upload).

stdlib-only:  pytest python/tests/test_ci_matrix_consistency.py --noconftest
"""

from __future__ import annotations

import re
from pathlib import Path

from qtpilot.download import BUILD_MATRIX

_REPO = Path(__file__).resolve().parents[2]
_CI = _REPO / ".github" / "workflows" / "ci.yml"
_RELEASE = _REPO / ".github" / "workflows" / "release.yml"


def _require(match: re.Match[str] | None, key: str, line: str) -> str:
    """First capture group, or a failure that names the offending line.

    The bare `re.search(...).group(1)` this replaces raised
    `AttributeError: 'NoneType' object has no attribute 'group'` with no clue which
    key or which line was at fault.
    """
    assert match is not None, f"CI build matrix entry has no `{key}:` key: {line}"
    return match.group(1)


def _parse_ci_builds() -> set[tuple[str, str, str]]:
    """Derive the (qt_minor, platform, arch) set the CI `build` job produces.

    Scoped to the `build` job deliberately. Other jobs carry matrix entries in the
    same `- { qt: ... }` shape -- `static-probe` pins a Qt version too -- but they
    publish no download artifacts, so they have no business being compared against
    BUILD_MATRIX. They also carry no `platform:` key, which is how this first
    surfaced: an opaque `AttributeError: 'NoneType' object has no attribute
    'group'` from the unchecked re.search below.
    """
    builds: set[tuple[str, str, str]] = set()
    in_build_job = False
    for raw in _CI.read_text().splitlines():
        # Job names are the only keys indented exactly two spaces (under `jobs:`);
        # everything inside a job is indented further.
        job = re.match(r"^ {2}([\w-]+):\s*$", raw)
        if job:
            in_build_job = job.group(1) == "build"
            continue
        if not in_build_job:
            continue
        line = raw.strip()
        if not line.startswith("- {") or "qt:" not in line:
            continue
        qt = _require(re.search(r'qt:\s*"([^"]+)"', line), "qt", line)
        qt_minor = ".".join(qt.split(".")[:2])
        platform = _require(re.search(r"platform:\s*(\w+)", line), "platform", line)
        preset_m = re.search(r"preset:\s*([\w-]+)", line)
        preset = preset_m.group(1) if preset_m else ""
        if "x86" in preset or "win32" in line:
            arch = "x86"
        elif platform == "macos":
            arch = "arm64"
        else:
            arch = "x64"
        builds.add((qt_minor, platform, arch))
    return builds


def test_build_matrix_matches_ci() -> None:
    assert set(BUILD_MATRIX) == _parse_ci_builds()


def test_release_checkouts_use_full_history() -> None:
    text = _RELEASE.read_text()
    n_checkout = text.count("actions/checkout")
    n_depth = len(re.findall(r"fetch-depth:\s*0", text))
    assert n_checkout > 0
    assert n_depth >= n_checkout, (
        f"{n_checkout} checkout step(s) but only {n_depth} `fetch-depth: 0` — "
        "hatch-vcs needs full history + tags to derive the version at publish time"
    )
