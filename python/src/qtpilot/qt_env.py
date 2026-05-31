"""Qt environment detection for ensuring probe DLL dependencies are available.

Detects Qt installations by examining the target application's directory,
explicit --qt-dir paths, and existing environment variables. Returns
environment variable overrides to pass to the launcher subprocess.
"""

from __future__ import annotations

import logging
import os
import sys
from pathlib import Path

logger = logging.getLogger(__name__)

# Qt DLL names to scan for when inferring Qt presence in a directory.
_QT_CORE_DLLS_WINDOWS = ("Qt5Core.dll", "Qt6Core.dll")
_QT_CORE_SOS_LINUX = ("libQt5Core.so.5", "libQt6Core.so.6")
_QT_CORE_DYLIBS_MACOS = ("libQt5Core.dylib", "libQt6Core.dylib")


def _is_valid_qt_prefix(qt_dir: Path) -> bool:
    """Check if a directory looks like a valid Qt installation prefix.

    A valid prefix has bin/ (or lib/ on Linux) and plugins/platforms/.
    """
    if not qt_dir.is_dir():
        return False

    plugins_platforms = qt_dir / "plugins" / "platforms"
    if not plugins_platforms.is_dir():
        return False

    if sys.platform == "win32":
        bin_dir = qt_dir / "bin"
        if not bin_dir.is_dir():
            return False
        # Check for a Qt core DLL
        return any((bin_dir / dll).exists() for dll in _QT_CORE_DLLS_WINDOWS)
    elif sys.platform == "darwin":
        lib_dir = qt_dir / "lib"
        if not lib_dir.is_dir():
            return False
        # Check for dylib or framework bundle
        has_dylib = any((lib_dir / dylib).exists() for dylib in _QT_CORE_DYLIBS_MACOS)
        has_framework = (lib_dir / "QtCore.framework").is_dir()
        return has_dylib or has_framework
    else:
        lib_dir = qt_dir / "lib"
        if not lib_dir.is_dir():
            return False
        return any((lib_dir / so).exists() for so in _QT_CORE_SOS_LINUX)


def _env_from_qt_dir(qt_dir: Path) -> dict[str, str]:
    """Derive environment variables from an explicit Qt installation prefix."""
    env: dict[str, str] = {}

    plugins_path = qt_dir / "plugins"
    if plugins_path.is_dir():
        env["QT_PLUGIN_PATH"] = str(plugins_path)

    if sys.platform == "win32":
        bin_path = qt_dir / "bin"
        if bin_path.is_dir():
            env["_PATH_PREPEND"] = str(bin_path)
    elif sys.platform == "darwin":
        lib_path = qt_dir / "lib"
        if lib_path.is_dir():
            env["_DYLD_LIBRARY_PATH_PREPEND"] = str(lib_path)
    else:
        lib_path = qt_dir / "lib"
        if lib_path.is_dir():
            env["_LD_LIBRARY_PATH_PREPEND"] = str(lib_path)

    return env


def _scan_target_directory(target_dir: Path) -> dict[str, str]:
    """Scan the target app's directory for Qt DLLs and infer env vars.

    Handles windeployqt layouts where Qt DLLs and platforms/ are
    alongside the target executable.
    """
    env: dict[str, str] = {}

    if not target_dir.is_dir():
        return env

    # Check for Qt DLLs in the target directory (windeployqt layout)
    if sys.platform == "win32":
        has_qt_dlls = any((target_dir / dll).exists() for dll in _QT_CORE_DLLS_WINDOWS)
    elif sys.platform == "darwin":
        has_qt_dlls = any((target_dir / dylib).exists() for dylib in _QT_CORE_DYLIBS_MACOS)
    else:
        has_qt_dlls = any((target_dir / so).exists() for so in _QT_CORE_SOS_LINUX)

    has_platforms = (target_dir / "platforms").is_dir()

    if has_qt_dlls and has_platforms:
        # Deploy layout: DLLs/dylibs + platforms/ in the same directory
        env["QT_PLUGIN_PATH"] = str(target_dir)
        if sys.platform == "win32":
            env["_PATH_PREPEND"] = str(target_dir)
        elif sys.platform == "darwin":
            env["_DYLD_LIBRARY_PATH_PREPEND"] = str(target_dir)
        else:
            env["_LD_LIBRARY_PATH_PREPEND"] = str(target_dir)
        logger.info("Detected Qt in target directory (windeployqt layout): %s", target_dir)
        return env

    # Check if target dir's parent is a Qt prefix (e.g. target in <prefix>/bin/)
    parent = target_dir.parent
    if _is_valid_qt_prefix(parent):
        env.update(_env_from_qt_dir(parent))
        logger.info("Detected Qt prefix from target's parent directory: %s", parent)

    return env


def _find_platforms_subdir(target_dir: Path) -> dict[str, str]:
    """Look for a platforms/ subdirectory next to the target."""
    env: dict[str, str] = {}

    if not target_dir.is_dir():
        return env

    platforms = target_dir / "platforms"
    if platforms.is_dir():
        env["QT_PLUGIN_PATH"] = str(target_dir)
        logger.debug("Found platforms/ subdirectory in: %s", target_dir)

    return env


def detect_qt_environment(
    target_path: str | None = None,
    qt_dir: str | None = None,
) -> dict[str, str]:
    """Detect Qt environment variables needed for launching a target app.

    Resolution cascade (first match wins):
      1. Explicit qt_dir (--qt-dir)
      2. Scan target app's directory for Qt DLLs
      3. Check if QT_PLUGIN_PATH is already set
      4. Look for platforms/ subdirectory next to target

    Args:
        target_path: Path to the target Qt application executable.
        qt_dir: Explicit path to a Qt installation prefix.

    Returns:
        Dict of environment variable overrides. Keys may include
        QT_PLUGIN_PATH, _PATH_PREPEND, _LD_LIBRARY_PATH_PREPEND.
        Internal _*_PREPEND keys are merged by build_subprocess_env().
    """
    # Step 1: Explicit --qt-dir
    if qt_dir:
        qt_dir_path = Path(qt_dir)
        if _is_valid_qt_prefix(qt_dir_path):
            logger.info("Using --qt-dir: %s", qt_dir)
            return _env_from_qt_dir(qt_dir_path)
        else:
            logger.warning(
                "--qt-dir '%s' does not look like a valid Qt prefix "
                "(expected plugins/platforms/ and bin/ or lib/)",
                qt_dir,
            )
            # Fall through to other strategies

    # Step 2: Scan target app's directory
    if target_path:
        target_dir = Path(target_path).resolve().parent
        env = _scan_target_directory(target_dir)
        if env:
            return env

    # Step 3: Check existing QT_PLUGIN_PATH
    existing_plugin_path = os.environ.get("QT_PLUGIN_PATH")
    if existing_plugin_path and Path(existing_plugin_path, "platforms").is_dir():
        logger.debug("QT_PLUGIN_PATH already set: %s", existing_plugin_path)
        return {}  # No changes needed

    # Step 4: Look for platforms/ next to target
    if target_path:
        target_dir = Path(target_path).resolve().parent
        env = _find_platforms_subdir(target_dir)
        if env:
            return env

    return {}


def build_subprocess_env(
    target_path: str | None = None,
    qt_dir: str | None = None,
) -> dict[str, str]:
    """Build a complete environment dict for the launcher subprocess.

    Starts with os.environ, then merges detected Qt paths.

    Args:
        target_path: Path to target executable.
        qt_dir: Explicit Qt installation prefix.

    Returns:
        Full environment dict suitable for subprocess env parameter.
    """
    env = os.environ.copy()
    overrides = detect_qt_environment(target_path, qt_dir)

    if not overrides:
        return env

    # Set QT_PLUGIN_PATH
    if "QT_PLUGIN_PATH" in overrides:
        existing = env.get("QT_PLUGIN_PATH", "")
        if not existing:
            env["QT_PLUGIN_PATH"] = overrides["QT_PLUGIN_PATH"]
        # Don't override if already set

    # Prepend to PATH (Windows)
    path_prepend = overrides.get("_PATH_PREPEND")
    if path_prepend:
        current_path = env.get("PATH", "")
        if path_prepend.lower() not in current_path.lower():
            env["PATH"] = path_prepend + os.pathsep + current_path

    # Prepend to DYLD_LIBRARY_PATH (macOS)
    dyld_prepend = overrides.get("_DYLD_LIBRARY_PATH_PREPEND")
    if dyld_prepend:
        current_dyld = env.get("DYLD_LIBRARY_PATH", "")
        if dyld_prepend not in current_dyld:
            env["DYLD_LIBRARY_PATH"] = dyld_prepend + os.pathsep + current_dyld

    # Prepend to LD_LIBRARY_PATH (Linux)
    ld_prepend = overrides.get("_LD_LIBRARY_PATH_PREPEND")
    if ld_prepend:
        current_ld = env.get("LD_LIBRARY_PATH", "")
        if ld_prepend not in current_ld:
            env["LD_LIBRARY_PATH"] = ld_prepend + os.pathsep + current_ld

    return env
