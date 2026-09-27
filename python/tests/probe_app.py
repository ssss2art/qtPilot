"""Typed binary selection shared by native probe integration tests."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import sys


@dataclass(frozen=True, slots=True)
class ProbeBuild:
    directory: Path
    launcher: Path
    application: Path

    @property
    def available(self) -> bool:
        return self.launcher.is_file() and self.application.is_file()

    @property
    def qt_prefix(self) -> Path | None:
        cache = self.directory / "CMakeCache.txt"
        if not cache.is_file():
            return None
        entries = dict(line.split("=", 1) for line in cache.read_text().splitlines() if "=" in line)
        explicit = entries.get("QTPILOT_QT_DIR:PATH", "")
        if explicit:
            return Path(explicit)
        for key in ("Qt6_DIR:PATH", "Qt5_DIR:PATH"):
            if entries.get(key):
                return Path(entries[key]).parents[2]
        return None


def probe_build(directory: Path) -> ProbeBuild:
    suffix = ".exe" if sys.platform == "win32" else ""
    binary_dir = directory / "bin"
    if not (binary_dir / f"qtPilot-launcher{suffix}").is_file() and (binary_dir / "Release").is_dir():
        binary_dir /= "Release"
    return ProbeBuild(directory, binary_dir / f"qtPilot-launcher{suffix}", binary_dir / f"qtPilot-test-app{suffix}")
