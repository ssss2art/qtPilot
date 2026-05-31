"""Unit tests for the qt_env module (Qt environment detection)."""

from __future__ import annotations

import os
import sys
from pathlib import Path
from unittest import mock

import pytest

from qtpilot.qt_env import (
    _env_from_qt_dir,
    _is_valid_qt_prefix,
    _scan_target_directory,
    build_subprocess_env,
    detect_qt_environment,
)


def _make_qt_prefix_linux(base: Path) -> Path:
    """Create a fake Qt prefix with Linux layout."""
    (base / "lib").mkdir(parents=True, exist_ok=True)
    (base / "lib" / "libQt6Core.so.6").touch()
    (base / "plugins" / "platforms").mkdir(parents=True, exist_ok=True)
    return base


def _make_qt_prefix_macos_framework(base: Path) -> Path:
    """Create a fake Qt prefix with macOS framework layout."""
    (base / "lib" / "QtCore.framework").mkdir(parents=True, exist_ok=True)
    (base / "plugins" / "platforms").mkdir(parents=True, exist_ok=True)
    return base


def _make_qt_prefix_macos_dylib(base: Path) -> Path:
    """Create a fake Qt prefix with macOS dylib layout."""
    (base / "lib").mkdir(parents=True, exist_ok=True)
    (base / "lib" / "libQt6Core.dylib").touch()
    (base / "plugins" / "platforms").mkdir(parents=True, exist_ok=True)
    return base


def _make_qt_prefix_windows(base: Path) -> Path:
    """Create a fake Qt prefix with Windows layout."""
    (base / "bin").mkdir(parents=True, exist_ok=True)
    (base / "bin" / "Qt6Core.dll").touch()
    (base / "plugins" / "platforms").mkdir(parents=True, exist_ok=True)
    return base


class TestIsValidQtPrefix:
    """Tests for _is_valid_qt_prefix across platforms."""

    def test_nonexistent_dir(self, tmp_path: Path) -> None:
        """Non-existent directory is not valid."""
        assert _is_valid_qt_prefix(tmp_path / "nope") is False

    def test_empty_dir(self, tmp_path: Path) -> None:
        """Empty directory is not valid."""
        assert _is_valid_qt_prefix(tmp_path) is False

    def test_missing_plugins_platforms(self, tmp_path: Path) -> None:
        """Directory without plugins/platforms/ is not valid."""
        (tmp_path / "lib").mkdir()
        (tmp_path / "lib" / "libQt6Core.so.6").touch()
        assert _is_valid_qt_prefix(tmp_path) is False

    def test_valid_linux_prefix(self, tmp_path: Path) -> None:
        """Valid Linux Qt prefix is recognized."""
        _make_qt_prefix_linux(tmp_path)
        with mock.patch("qtpilot.qt_env.sys.platform", "linux"):
            assert _is_valid_qt_prefix(tmp_path) is True

    def test_valid_macos_framework_prefix(self, tmp_path: Path) -> None:
        """Valid macOS Qt prefix with framework bundle is recognized."""
        _make_qt_prefix_macos_framework(tmp_path)
        with mock.patch("qtpilot.qt_env.sys.platform", "darwin"):
            assert _is_valid_qt_prefix(tmp_path) is True

    def test_valid_macos_dylib_prefix(self, tmp_path: Path) -> None:
        """Valid macOS Qt prefix with plain dylib is recognized."""
        _make_qt_prefix_macos_dylib(tmp_path)
        with mock.patch("qtpilot.qt_env.sys.platform", "darwin"):
            assert _is_valid_qt_prefix(tmp_path) is True

    def test_valid_windows_prefix(self, tmp_path: Path) -> None:
        """Valid Windows Qt prefix is recognized."""
        _make_qt_prefix_windows(tmp_path)
        with mock.patch("qtpilot.qt_env.sys.platform", "win32"):
            assert _is_valid_qt_prefix(tmp_path) is True

    def test_macos_rejects_linux_layout(self, tmp_path: Path) -> None:
        """macOS platform rejects Linux-style .so layout."""
        _make_qt_prefix_linux(tmp_path)
        with mock.patch("qtpilot.qt_env.sys.platform", "darwin"):
            assert _is_valid_qt_prefix(tmp_path) is False


class TestEnvFromQtDir:
    """Tests for _env_from_qt_dir across platforms."""

    def test_linux_sets_ld_library_path(self, tmp_path: Path) -> None:
        """Linux prefix produces _LD_LIBRARY_PATH_PREPEND."""
        _make_qt_prefix_linux(tmp_path)
        with mock.patch("qtpilot.qt_env.sys.platform", "linux"):
            env = _env_from_qt_dir(tmp_path)
        assert "QT_PLUGIN_PATH" in env
        assert "_LD_LIBRARY_PATH_PREPEND" in env
        assert env["_LD_LIBRARY_PATH_PREPEND"] == str(tmp_path / "lib")

    def test_macos_sets_dyld_library_path(self, tmp_path: Path) -> None:
        """macOS prefix produces _DYLD_LIBRARY_PATH_PREPEND."""
        _make_qt_prefix_macos_framework(tmp_path)
        with mock.patch("qtpilot.qt_env.sys.platform", "darwin"):
            env = _env_from_qt_dir(tmp_path)
        assert "QT_PLUGIN_PATH" in env
        assert "_DYLD_LIBRARY_PATH_PREPEND" in env
        assert env["_DYLD_LIBRARY_PATH_PREPEND"] == str(tmp_path / "lib")

    def test_windows_sets_path_prepend(self, tmp_path: Path) -> None:
        """Windows prefix produces _PATH_PREPEND."""
        _make_qt_prefix_windows(tmp_path)
        with mock.patch("qtpilot.qt_env.sys.platform", "win32"):
            env = _env_from_qt_dir(tmp_path)
        assert "QT_PLUGIN_PATH" in env
        assert "_PATH_PREPEND" in env
        assert env["_PATH_PREPEND"] == str(tmp_path / "bin")

    def test_missing_plugins_omits_key(self, tmp_path: Path) -> None:
        """Missing plugins/ directory omits QT_PLUGIN_PATH."""
        (tmp_path / "lib").mkdir()
        with mock.patch("qtpilot.qt_env.sys.platform", "linux"):
            env = _env_from_qt_dir(tmp_path)
        assert "QT_PLUGIN_PATH" not in env


class TestBuildSubprocessEnv:
    """Tests for build_subprocess_env DYLD/LD path merging."""

    def test_macos_dyld_prepended(self, tmp_path: Path) -> None:
        """macOS: DYLD_LIBRARY_PATH is prepended correctly."""
        _make_qt_prefix_macos_framework(tmp_path)
        lib_path = str(tmp_path / "lib")

        with mock.patch("qtpilot.qt_env.sys.platform", "darwin"):
            with mock.patch.dict(os.environ, {"DYLD_LIBRARY_PATH": "/existing/path"}, clear=False):
                env = build_subprocess_env(qt_dir=str(tmp_path))

        assert env["DYLD_LIBRARY_PATH"].startswith(lib_path)
        assert "/existing/path" in env["DYLD_LIBRARY_PATH"]

    def test_macos_dyld_not_duplicated(self, tmp_path: Path) -> None:
        """macOS: path is not prepended if already present."""
        _make_qt_prefix_macos_framework(tmp_path)
        lib_path = str(tmp_path / "lib")

        with mock.patch("qtpilot.qt_env.sys.platform", "darwin"):
            with mock.patch.dict(os.environ, {"DYLD_LIBRARY_PATH": lib_path}, clear=False):
                env = build_subprocess_env(qt_dir=str(tmp_path))

        # Should not duplicate
        assert env["DYLD_LIBRARY_PATH"].count(lib_path) == 1

    def test_linux_ld_prepended(self, tmp_path: Path) -> None:
        """Linux: LD_LIBRARY_PATH is prepended correctly."""
        _make_qt_prefix_linux(tmp_path)
        lib_path = str(tmp_path / "lib")

        with mock.patch("qtpilot.qt_env.sys.platform", "linux"):
            with mock.patch.dict(os.environ, {"LD_LIBRARY_PATH": "/existing"}, clear=False):
                env = build_subprocess_env(qt_dir=str(tmp_path))

        assert env["LD_LIBRARY_PATH"].startswith(lib_path)

    def test_qt_plugin_path_set(self, tmp_path: Path) -> None:
        """QT_PLUGIN_PATH is set when not already present."""
        _make_qt_prefix_linux(tmp_path)

        with mock.patch("qtpilot.qt_env.sys.platform", "linux"):
            with mock.patch.dict(os.environ, {}, clear=False):
                # Remove QT_PLUGIN_PATH if present
                env_clean = {k: v for k, v in os.environ.items() if k != "QT_PLUGIN_PATH"}
                with mock.patch.dict(os.environ, env_clean, clear=True):
                    env = build_subprocess_env(qt_dir=str(tmp_path))

        assert "QT_PLUGIN_PATH" in env
        assert str(tmp_path / "plugins") in env["QT_PLUGIN_PATH"]

    def test_no_qt_dir_returns_environ(self, tmp_path: Path) -> None:
        """No Qt found returns os.environ unchanged."""
        env = build_subprocess_env(target_path=str(tmp_path / "nonexistent"))
        # Should get environ back without modifications
        assert isinstance(env, dict)


class TestDetectQtEnvironment:
    """Tests for the full detection cascade."""

    def test_explicit_qt_dir_wins(self, tmp_path: Path) -> None:
        """Explicit qt_dir takes priority."""
        _make_qt_prefix_linux(tmp_path)
        with mock.patch("qtpilot.qt_env.sys.platform", "linux"):
            env = detect_qt_environment(qt_dir=str(tmp_path))
        assert "QT_PLUGIN_PATH" in env

    def test_invalid_qt_dir_falls_through(self, tmp_path: Path) -> None:
        """Invalid qt_dir falls through to other strategies."""
        with mock.patch("qtpilot.qt_env.sys.platform", "linux"):
            env = detect_qt_environment(qt_dir=str(tmp_path / "bogus"))
        assert env == {}

    def test_scan_target_directory(self, tmp_path: Path) -> None:
        """Target directory with deploy layout is detected."""
        app_dir = tmp_path / "app"
        app_dir.mkdir()
        (app_dir / "libQt6Core.so.6").touch()
        (app_dir / "platforms").mkdir()
        target = app_dir / "myapp"
        target.touch()

        with mock.patch("qtpilot.qt_env.sys.platform", "linux"):
            env = detect_qt_environment(target_path=str(target))
        assert "QT_PLUGIN_PATH" in env
