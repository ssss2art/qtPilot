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
