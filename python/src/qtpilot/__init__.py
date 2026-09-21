"""qtPilot - MCP server for controlling Qt applications."""

from __future__ import annotations

import importlib.metadata

try:
    __version__ = importlib.metadata.version("qtpilot")
except importlib.metadata.PackageNotFoundError:
    try:
        from qtpilot._version import __version__
    except ImportError:
        __version__ = "0.0.0.dev"

