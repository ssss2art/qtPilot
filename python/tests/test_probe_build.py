"""Required E2E cannot accidentally certify missing binaries or skipped tests."""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

import pytest

from tests.probe_app import probe_build


def test_explicit_build_directory_drives_binary_selection(tmp_path: Path) -> None:
    binary_dir = tmp_path / "bin"
    binary_dir.mkdir()
    suffix = ".exe" if sys.platform == "win32" else ""
    for name in ("qtPilot-launcher", "qtPilot-test-app"):
        (binary_dir / f"{name}{suffix}").touch()
    build = probe_build(tmp_path)
    assert build.directory == tmp_path
    assert build.available, "The explicit built kit must be selected instead of build/bin"


def test_required_e2e_refuses_missing_binaries_instead_of_skipping(tmp_path: Path) -> None:
    result = subprocess.run(
        [sys.executable, "-m", "pytest", "python/tests/test_replay_e2e.py", "--collect-only", "-q", "-p", "no:cacheprovider"],
        cwd=Path(__file__).resolve().parents[2],
        env=dict(os.environ, QTPILOT_REQUIRE_E2E="1", QTPILOT_TEST_BUILD_DIR=str(tmp_path),
                 PYTHONPATH=str(Path(__file__).resolve().parents[1])),
        capture_output=True, text=True,
    )
    assert result.returncode != 0, "Required E2E with no artifacts silently passed collection"
    assert "Required E2E" in result.stdout + result.stderr


def test_required_e2e_refuses_an_empty_selection() -> None:
    result = subprocess.run(
        [sys.executable, "-m", "pytest", "python/tests/test_replay_e2e.py", "-k", "nothing_matches_this", "-q", "-p", "no:cacheprovider"],
        cwd=Path(__file__).resolve().parents[2],
        env=dict(os.environ, QTPILOT_REQUIRE_E2E="1"), capture_output=True, text=True,
    )
    assert result.returncode != 0
    assert "Required E2E" in result.stdout + result.stderr


def test_required_e2e_refuses_a_runtime_skip(tmp_path: Path) -> None:
    test_explicit_build_directory_drives_binary_selection(tmp_path)
    test_file = tmp_path / "test_skipped_probe.py"
    (tmp_path / "conftest.py").write_text("from tests.conftest import *\n")
    test_file.write_text(
        "import pytest\n@pytest.mark.real_probe\ndef test_probe():\n    pytest.skip('fixture unavailable')\n"
    )
    result = subprocess.run(
        [sys.executable, "-m", "pytest", str(test_file), "-q", "-p", "no:cacheprovider"],
        cwd=Path(__file__).resolve().parents[2],
        env=dict(os.environ, QTPILOT_REQUIRE_E2E="1", QTPILOT_TEST_BUILD_DIR=str(tmp_path),
                 PYTHONPATH=str(Path(__file__).resolve().parents[1])),
        capture_output=True, text=True,
    )
    assert result.returncode == 1, result.stdout + result.stderr
    assert "Required E2E unexpectedly skipped" in result.stdout + result.stderr
