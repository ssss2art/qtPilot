"""Every probe method the Python tools send must be one the probe registers.

The tool tests mock the probe, so a misspelt method name passes them and only
fails against a real probe as method-not-found. This reads both sides from
source to catch that without building the probe.

stdlib-only:  pytest python/tests/test_probe_method_contract.py --noconftest
"""

from __future__ import annotations

import re
from pathlib import Path

import pytest
from qtpilot.fluent import expect_wire_methods

_REPO = Path(__file__).resolve().parents[2]
_PY_SRC = _REPO / "python" / "src" / "qtpilot"
_PROBE_SRC = _REPO / "src" / "probe"

_CALL = re.compile(r'\.call\(\s*"([a-z]+\.[A-Za-z.]+)"')
_REGISTER = re.compile(r'RegisterMethod\(\s*QStringLiteral\("([^"]+)"\)')


def _scan(root: Path, glob: str, pattern: re.Pattern[str]) -> set[str]:
    return {m for path in root.rglob(glob) for m in pattern.findall(path.read_text(encoding="utf-8"))}


def _sent_by_tools() -> set[str]:
    return _scan(_PY_SRC, "*.py", _CALL)


def _registered_by_probe() -> set[str]:
    return _scan(_PROBE_SRC, "*.cpp", _REGISTER)


def test_scanners_find_both_sides() -> None:
    # Guards the regexes: an empty scan would make the contract vacuously true.
    assert {"qt.ping", "cu.click"} <= _sent_by_tools()
    assert {"qt.ping", "cu.click", "cu.drag"} <= _registered_by_probe()


def test_every_method_the_tools_send_is_registered_by_the_probe() -> None:
    expect_wire_methods(_sent_by_tools()).to_be_registered_by(_registered_by_probe())


def test_expectation_names_the_unregistered_method() -> None:
    with pytest.raises(AssertionError, match=r"\['cu\.mouseDrag'\]"):
        expect_wire_methods({"cu.click", "cu.mouseDrag"}).to_be_registered_by({"cu.click", "cu.drag"})
