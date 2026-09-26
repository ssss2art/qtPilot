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
import json
from pathlib import Path

from qtpilot.download import BUILD_MATRIX

_REPO = Path(__file__).resolve().parents[2]
_CI = _REPO / ".github" / "workflows" / "ci.yml"
_RELEASE = _REPO / ".github" / "workflows" / "release.yml"


def _parse_ci_builds() -> set[tuple[str, str, str]]:
    """The complete release catalog, not the reduced per-PR validation matrix."""
    catalog: dict[str, list[dict[str, str]]] = json.loads(
        (_REPO / "scripts" / "ci-matrix.json").read_text()
    )
    builds: set[tuple[str, str, str]] = set()
    for entry in catalog["build"]:
        qt_minor = ".".join(entry["qt"].split(".")[:2])
        platform = entry["platform"]
        arch = "x86" if "x86" in entry["preset"] else ("arm64" if platform == "macos" else "x64")
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
