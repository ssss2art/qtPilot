"""Tests for token-efficient screenshot handling (ImageContent & save_to)."""

from __future__ import annotations

import base64
import re
import struct
import tempfile
from pathlib import Path
from unittest.mock import AsyncMock, patch

import pytest
from fastmcp import Client

from qtpilot._mcp_compat import get_image_mime_type
from qtpilot.server import create_server
from qtpilot.tools.screenshot_helper import (
    extract_png_dimensions,
    process_screenshot_response,
    screenshot_run_dir,
)
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


def _envelope(b64: str, **extra: int) -> dict:
    """Shape of a real probe reply, as ProbeConnection.call() returns it."""
    return {"meta": {"timestamp": 1}, "result": {"image": b64, **extra}}


class TestProcessScreenshotResponse:
    @pytest.fixture(autouse=True)
    def _temp_root(self, tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
        monkeypatch.setattr(tempfile, "tempdir", str(tmp_path))

    def test_envelope_is_saved_to_the_run_folder(self, tmp_path: Path) -> None:
        b64, raw = _make_dummy_png(50, 60)
        result = process_screenshot_response(_envelope(b64), tool="qt_ui_screenshot")
        path = Path(result["saved_to"])
        assert path.parent == screenshot_run_dir()
        assert screenshot_run_dir().is_relative_to(tmp_path / "qtpilot" / "screenshots")
        assert re.fullmatch(r"\d{4}-qt_ui_screenshot\.png", path.name)
        assert path.read_bytes() == raw
        assert result == {"saved_to": str(path), "width": 50, "height": 60, "bytes": len(raw)}
        assert b64 not in result.values()

    def test_two_captures_never_share_a_file(self) -> None:
        b64, _ = _make_dummy_png()
        first = process_screenshot_response(_envelope(b64), tool="cu_screenshot")["saved_to"]
        second = process_screenshot_response(_envelope(b64), tool="cu_screenshot")["saved_to"]
        assert first != second and Path(first).exists() and Path(second).exists()

    def test_dimensions_come_from_the_png_when_the_reply_has_none(self) -> None:
        b64, _ = _make_dummy_png(120, 80)
        result = process_screenshot_response(_envelope(b64), tool="qt_ui_screenshot")
        assert (result["width"], result["height"]) == (120, 80)

    def test_reply_dimensions_win_over_the_png_header(self) -> None:
        b64, _ = _make_dummy_png(10, 10)
        result = process_screenshot_response(
            _envelope(b64, width=80, height=40), tool="cu_screenshot"
        )
        assert (result["width"], result["height"]) == (80, 40)

    @pytest.mark.parametrize(
        "resp", [{"meta": {}, "result": {}}, {"meta": {}, "result": {"image": ""}}, {}]
    )
    def test_no_image_is_an_error_and_writes_nothing(self, resp: dict) -> None:
        with pytest.raises(ValueError, match="no image"):
            process_screenshot_response(resp, tool="qt_ui_screenshot")
        assert not screenshot_run_dir().exists()

    def test_flat_reply_is_still_accepted(self) -> None:
        b64, raw = _make_dummy_png()
        result = process_screenshot_response({"image": b64}, tool="qt_ui_screenshot")
        assert Path(result["saved_to"]).read_bytes() == raw

    def test_save_to_creates_folders_and_is_used_verbatim(self, tmp_path: Path) -> None:
        b64, raw = _make_dummy_png(64, 48)
        out = tmp_path / "nested" / "capture.png"
        result = process_screenshot_response(
            _envelope(b64), tool="qt_ui_screenshot", save_to=str(out)
        )
        assert result["saved_to"] == str(out.resolve())
        assert out.read_bytes() == raw


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
