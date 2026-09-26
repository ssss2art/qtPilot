"""Synthetic binding fixture; launched externally through the C++ probe."""

from __future__ import annotations

import ctypes
import json
import os
import platform
import sys
from pathlib import Path

from PySide6 import __version__ as binding_version
from PySide6.QtCore import Property, Signal, Slot, qVersion
from PySide6.QtWidgets import QApplication, QWidget


def loaded_qt_core() -> list[str]:
    if sys.platform == "linux":
        return sorted({line.split()[-1] for line in Path("/proc/self/maps").read_text().splitlines()
                       if "/libQt6Core.so" in line})
    if sys.platform == "darwin":
        library = ctypes.CDLL(None)
        library._dyld_image_count.restype = ctypes.c_uint32
        library._dyld_get_image_name.argtypes = [ctypes.c_uint32]
        library._dyld_get_image_name.restype = ctypes.c_char_p
        return sorted({library._dyld_get_image_name(i).decode()
                       for i in range(library._dyld_image_count())
                       if "/QtCore.framework/" in library._dyld_get_image_name(i).decode()})
    raise RuntimeError("Loaded-library qualification currently supports Linux and macOS")


class Fixture(QWidget):
    changed = Signal(int)

    def __init__(self) -> None:
        super().__init__()
        self.setObjectName("fixtureRoot")
        self._value = 1

    def get_value(self) -> int:
        return self._value

    def set_value(self, value: int) -> None:
        self._value = value
        self.changed.emit(value)

    value = Property(int, get_value, set_value, notify=changed)

    @Slot(int, result=int)
    def increment(self, delta: int) -> int:
        self.set_value(self._value + delta)
        return self._value


def main() -> int:
    app = QApplication([])
    app.setApplicationName("python-widgets-fixture")
    widget = Fixture()
    widget.show()
    Path(sys.argv[1]).write_text(json.dumps({
        "python": platform.python_version(), "binding": binding_version,
        "qt": qVersion(), "architecture": platform.machine(), "pid": os.getpid(),
        "qtCoreLibraries": loaded_qt_core(), "pluginPaths": app.libraryPaths(),
    }, indent=2))
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
