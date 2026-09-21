"""Helper functions for token-efficient screenshot handling."""

from __future__ import annotations

import base64
import struct
from pathlib import Path

import mcp.types as types


def extract_png_dimensions(raw_png: bytes) -> tuple[int, int]:
    """Extract width and height from PNG IHDR chunk if present, or (0, 0)."""
    if len(raw_png) >= 24 and raw_png.startswith(b"\x89PNG\r\n\x1a\n"):
        width, height = struct.unpack(">II", raw_png[16:24])
        return width, height
    return 0, 0


def process_screenshot_response(
    resp: dict,
    save_to: str | None = None,
    as_image: bool = True,
) -> types.ImageContent | dict:
    """Process a raw probe screenshot response into an image, file artifact, or dict.

    Args:
        resp: Raw response from probe containing "image" (base64 string) and optional "width"/"height".
        save_to: Optional path to write PNG file. When specified, returns lean metadata
                 omitting the base64 string to preserve model token budget.
        as_image: When True (default) and save_to is None, returns native MCP ImageContent
                  so multimodal models receive image tokens rather than raw text tokens.
                  When False, returns the raw dict with dimensions.
    """
    b64: str = resp.get("image", "")
    raw_bytes: bytes = base64.b64decode(b64) if b64 else b""

    width: int = resp.get("width", 0)
    height: int = resp.get("height", 0)
    if (width == 0 or height == 0) and raw_bytes:
        extracted_w, extracted_h = extract_png_dimensions(raw_bytes)
        width = width or extracted_w
        height = height or extracted_h

    if save_to:
        out_path = Path(save_to).resolve()
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_bytes(raw_bytes)
        return {
            "saved_to": str(out_path),
            "width": width,
            "height": height,
            "bytes": len(raw_bytes),
        }

    if as_image:
        from qtpilot._mcp_compat import create_image_content
        return create_image_content(data=b64, mime_type="image/png")

    result = dict(resp)
    result["width"] = width
    result["height"] = height
    return result
