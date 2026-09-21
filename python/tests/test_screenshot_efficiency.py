"""Tests for token-efficient screenshot handling (ImageContent & save_to)."""

from __future__ import annotations

import base64
import struct
import tempfile
from pathlib import Path
from unittest.mock import AsyncMock, patch

import pytest
from fastmcp import Client

from qtpilot._mcp_compat import get_image_mime_type
from qtpilot.server import create_server
from qtpilot.tools.screenshot_helper import extract_png_dimensions, process_screenshot_response
import mcp.types as types


def _make_dummy_png(width: int = 10, height: int = 20) -> tuple[str, bytes]:
    """Generate a minimal valid PNG header with specified dimensions."""
    header = b"\x89PNG\r\n\x1a\n"
    ihdr_len = struct.pack(">I", 13)
    ihdr_type = b"IHDR"
    ihdr_data = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    ihdr_crc = struct.pack(">I", 0)  # dummy crc
    raw = header + ihdr_len + ihdr_type + ihdr_data + ihdr_crc
    b64 = base64.b64encode(raw).decode("ascii")
    return b64, raw


class TestPngDimensionExtraction:
    def test_extract_dimensions_valid_png(self):
        b64, raw = _make_dummy_png(120, 80)
        assert extract_png_dimensions(raw) == (120, 80)

    def test_extract_dimensions_invalid_data(self):
        assert extract_png_dimensions(b"not a png") == (0, 0)
        assert extract_png_dimensions(b"") == (0, 0)


class TestProcessScreenshotResponse:
    def test_default_returns_image_content(self):
        b64, _ = _make_dummy_png(50, 60)
        resp = {"image": b64}
        result = process_screenshot_response(resp, save_to=None, as_image=True)
        assert isinstance(result, types.ImageContent)
        assert result.type == "image"
        assert get_image_mime_type(result) == "image/png"
        assert result.data == b64

    def test_as_image_false_returns_dict_with_dimensions(self):
        b64, _ = _make_dummy_png(50, 60)
        resp = {"image": b64}
        result = process_screenshot_response(resp, save_to=None, as_image=False)
        assert isinstance(result, dict)
        assert result["image"] == b64
        assert result["width"] == 50
        assert result["height"] == 60

    def test_save_to_writes_file_and_omits_base64(self):
        b64, raw = _make_dummy_png(64, 48)
        resp = {"image": b64}
        with tempfile.TemporaryDirectory() as tmpdir:
            out_path = Path(tmpdir) / "nested" / "capture.png"
            result = process_screenshot_response(resp, save_to=str(out_path), as_image=True)
            assert isinstance(result, dict)
            assert "image" not in result  # Token optimization: no massive base64 in reply!
            assert result["saved_to"] == str(out_path.resolve())
            assert result["width"] == 64
            assert result["height"] == 48
            assert result["bytes"] == len(raw)
            assert out_path.exists()
            assert out_path.read_bytes() == raw


class TestNativeScreenshotTool:
    @pytest.mark.asyncio
    async def test_qt_ui_screenshot_returns_image_content_by_default(self):
        b64, _ = _make_dummy_png(100, 200)
        mock_probe = AsyncMock()
        mock_probe.is_connected = True
        mock_probe.call = AsyncMock(return_value={"image": b64})

        with patch("qtpilot.server.require_probe", return_value=mock_probe):
            mcp = create_server(mode="native")
            async with Client(mcp) as client:
                res = await client.call_tool("qt_ui_screenshot", {"objectId": "MainWindow"})
                assert len(res.content) == 1
                content = res.content[0]
                assert isinstance(content, types.ImageContent)
                assert content.data == b64
                assert get_image_mime_type(content) == "image/png"

    @pytest.mark.asyncio
    async def test_qt_ui_screenshot_save_to_returns_lean_metadata(self):
        b64, raw = _make_dummy_png(100, 200)
        mock_probe = AsyncMock()
        mock_probe.is_connected = True
        mock_probe.call = AsyncMock(return_value={"image": b64})

        with tempfile.TemporaryDirectory() as tmpdir:
            out_file = str(Path(tmpdir) / "win.png")
            with patch("qtpilot.server.require_probe", return_value=mock_probe):
                mcp = create_server(mode="native")
                async with Client(mcp) as client:
                    res = await client.call_tool(
                        "qt_ui_screenshot",
                        {"objectId": "MainWindow", "save_to": out_file},
                    )
                    assert len(res.content) == 1
                    text = res.content[0].text
                    assert "saved_to" in text
                    assert "image" not in text
                    assert Path(out_file).exists()
                    assert Path(out_file).read_bytes() == raw


class TestCuScreenshotTool:
    @pytest.mark.asyncio
    async def test_cu_screenshot_returns_image_content_by_default(self):
        b64, _ = _make_dummy_png(80, 40)
        mock_probe = AsyncMock()
        mock_probe.is_connected = True
        mock_probe.call = AsyncMock(return_value={"image": b64, "width": 80, "height": 40})

        with patch("qtpilot.server.require_probe", return_value=mock_probe):
            mcp = create_server(mode="cu")
            async with Client(mcp) as client:
                res = await client.call_tool("cu_screenshot", {})
                assert len(res.content) == 1
                content = res.content[0]
                assert isinstance(content, types.ImageContent)
                assert content.data == b64

    @pytest.mark.asyncio
    async def test_cu_screenshot_save_to_returns_lean_metadata(self):
        b64, raw = _make_dummy_png(80, 40)
        mock_probe = AsyncMock()
        mock_probe.is_connected = True
        mock_probe.call = AsyncMock(return_value={"image": b64, "width": 80, "height": 40})

        with tempfile.TemporaryDirectory() as tmpdir:
            out_file = str(Path(tmpdir) / "cu_win.png")
            with patch("qtpilot.server.require_probe", return_value=mock_probe):
                mcp = create_server(mode="cu")
                async with Client(mcp) as client:
                    res = await client.call_tool("cu_screenshot", {"save_to": out_file})
                    assert len(res.content) == 1
                    text = res.content[0].text
                    assert "saved_to" in text
                    assert Path(out_file).exists()
                    assert Path(out_file).read_bytes() == raw
