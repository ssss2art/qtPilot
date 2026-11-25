"""Save probe screenshots to PNG files and describe them without image data."""

from __future__ import annotations

import base64
import itertools
import os
import struct
import tempfile
import time
from dataclasses import asdict, dataclass
from pathlib import Path

# One folder per server run, named once at import; files are numbered in capture order.
_RUN_ID = f"{time.strftime('%Y%m%d-%H%M%S')}-{os.getpid()}"
_SEQUENCE = itertools.count(1)


@dataclass(frozen=True, slots=True)
class SavedScreenshot:
    """What a screenshot tool returns: where the PNG went and what it holds."""

    saved_to: str
    width: int
    height: int
    bytes: int


def extract_png_dimensions(raw_png: bytes) -> tuple[int, int]:
    """Extract width and height from PNG IHDR chunk if present, or (0, 0)."""
    if len(raw_png) >= 24 and raw_png.startswith(b"\x89PNG\r\n\x1a\n"):
        width, height = struct.unpack(">II", raw_png[16:24])
        return width, height
    return 0, 0


def screenshot_run_dir() -> Path:
    """Default folder for this server run's screenshots (created on first save)."""
    return Path(tempfile.gettempdir()) / "qtpilot" / "screenshots" / _RUN_ID


def process_screenshot_response(
    resp: dict,
    *,
    tool: str,
    save_to: str | None = None,
) -> dict:
    """Write a probe screenshot reply to a PNG file and return its metadata.

    Args:
        resp: What ``ProbeConnection.call()`` returned: the envelope
            ``{"meta": {...}, "result": {"image": b64, "width"?, "height"?}}``,
            or a flat ``{"image": b64, ...}``.
        tool: Name of the calling tool, used in the default file name.
        save_to: Path to write the PNG to. When omitted, the PNG goes to
            ``screenshot_run_dir()`` as ``<sequence>-<tool>.png``.

    Returns:
        ``{"saved_to", "width", "height", "bytes"}``; never the image data.

    Raises:
        ValueError: The reply has no image or an empty one. Nothing is written.
    """
    inner = resp.get("result")
    payload: dict = inner if isinstance(inner, dict) else resp

    b64: str = payload.get("image") or ""
    if not b64:
        raise ValueError("probe reply contained no image")
    raw_bytes = base64.b64decode(b64)

    width: int = payload.get("width") or 0
    height: int = payload.get("height") or 0
    if width == 0 or height == 0:
        extracted_w, extracted_h = extract_png_dimensions(raw_bytes)
        width = width or extracted_w
        height = height or extracted_h

    if save_to:
        out_path = Path(save_to).resolve()
    else:
        out_path = screenshot_run_dir() / f"{next(_SEQUENCE):04d}-{tool}.png"
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(raw_bytes)

    return asdict(
        SavedScreenshot(
            saved_to=str(out_path),
            width=width,
            height=height,
            bytes=len(raw_bytes),
        )
    )
