"""Unit tests for smart defaults and argument tolerance across MCP tools."""

from __future__ import annotations

from unittest.mock import AsyncMock, MagicMock, patch

import pytest
from fastmcp import Client

from qtpilot.server import create_server


class TestSmartDefaults:
    @pytest.fixture
    def mock_probe(self):
        probe = MagicMock()
        probe.call = AsyncMock(return_value={"ok": True})
        probe.is_connected = True
        probe.ws_url = "ws://localhost:9222"
        return probe

    @pytest.mark.asyncio
    async def test_objects_search_accepts_name_and_class_name(self, mock_probe):
        with patch("qtpilot.server.require_probe", return_value=mock_probe):
            mcp = create_server(mode="native")
            async with Client(mcp) as client:
                await client.call_tool(
                    "qt_objects_search",
                    {"name": "myBtn", "class_name": "QPushButton"},
                )
            mock_probe.call.assert_awaited_once_with(
                "qt.objects.search",
                {"objectName": "myBtn", "className": "QPushButton"},
            )

    @pytest.mark.asyncio
    async def test_objects_inspect_accepts_comma_separated_parts_and_alias(self, mock_probe):
        with patch("qtpilot.server.require_probe", return_value=mock_probe):
            mcp = create_server(mode="native")
            async with Client(mcp) as client:
                await client.call_tool(
                    "qt_objects_inspect",
                    {"objectId": "btn", "part": "info, properties"},
                )
            mock_probe.call.assert_awaited_once_with(
                "qt.objects.inspect",
                {"objectId": "btn", "parts": ["info", "properties"]},
            )

    @pytest.mark.asyncio
    async def test_models_search_accepts_maxHits(self, mock_probe):
        with patch("qtpilot.server.require_probe", return_value=mock_probe):
            mcp = create_server(mode="native")
            async with Client(mcp) as client:
                await client.call_tool(
                    "qt_models_search",
                    {"objectId": "tree", "value": "test", "maxHits": 25},
                )
            mock_probe.call.assert_awaited_once_with(
                "qt.models.search",
                {
                    "objectId": "tree",
                    "value": "test",
                    "column": 0,
                    "role": "display",
                    "match": "contains",
                    "maxHits": 25,
                },
            )

    @pytest.mark.asyncio
    async def test_cu_click_accepts_coordinate_and_casing_aliases(self, mock_probe):
        with patch("qtpilot.server.require_probe", return_value=mock_probe):
            mcp = create_server(mode="cu")
            async with Client(mcp) as client:
                await client.call_tool(
                    "cu_leftClick",
                    {
                        "coordinate": [120, 240],
                        "screen_absolute": True,
                        "delayMs": 50,
                    },
                )
            mock_probe.call.assert_awaited_once_with(
                "cu.click",
                {
                    "x": 120,
                    "y": 240,
                    "screenAbsolute": True,
                    "delay_ms": 50,
                },
            )

    @pytest.mark.asyncio
    async def test_cu_mouseDrag_accepts_anthropic_coordinates(self, mock_probe):
        with patch("qtpilot.server.require_probe", return_value=mock_probe):
            mcp = create_server(mode="cu")
            async with Client(mcp) as client:
                await client.call_tool(
                    "cu_mouseDrag",
                    {
                        "start_coordinate": [10, 20],
                        "coordinate": [300, 400],
                    },
                )
            mock_probe.call.assert_awaited_once_with(
                "cu.mouseDrag",
                {
                    "startX": 10,
                    "startY": 20,
                    "endX": 300,
                    "endY": 400,
                },
            )

    @pytest.mark.asyncio
    async def test_cu_key_accepts_text_parameter(self, mock_probe):
        with patch("qtpilot.server.require_probe", return_value=mock_probe):
            mcp = create_server(mode="cu")
            async with Client(mcp) as client:
                await client.call_tool("cu_key", {"text": "ctrl+c"})
            mock_probe.call.assert_awaited_once_with("cu.key", {"key": "ctrl+c"})

    @pytest.mark.asyncio
    async def test_chr_click_accepts_int_ref(self, mock_probe):
        with patch("qtpilot.server.require_probe", return_value=mock_probe):
            mcp = create_server(mode="chrome")
            async with Client(mcp) as client:
                await client.call_tool("chr_click", {"ref": 42})
            mock_probe.call.assert_awaited_once_with("chr.click", {"ref": "42"})

    @pytest.mark.asyncio
    async def test_chr_formInput_accepts_int_ref_and_text_alias(self, mock_probe):
        with patch("qtpilot.server.require_probe", return_value=mock_probe):
            mcp = create_server(mode="chrome")
            async with Client(mcp) as client:
                await client.call_tool("chr_formInput", {"ref": 7, "text": "foo"})
            mock_probe.call.assert_awaited_once_with(
                "chr.formInput", {"ref": "7", "value": "foo"}
            )

    @pytest.mark.asyncio
    async def test_recording_start_accepts_string_targets_and_objectId(self, mock_probe):
        recorder = MagicMock()
        recorder.start = AsyncMock(return_value={"ok": True, "recording": True})
        with (
            patch("qtpilot.server.require_probe", return_value=mock_probe),
            patch("qtpilot.server.get_recorder", return_value=recorder),
        ):
            mcp = create_server(mode="native")
            async with Client(mcp) as client:
                # String target
                await client.call_tool(
                    "qtpilot_recording_start", {"targets": ["MainWindow"]}
                )
                # Dict with objectId
                await client.call_tool(
                    "qtpilot_recording_start",
                    {"targets": [{"objectId": "SubmitBtn", "signals": ["clicked"]}]},
                )
            assert recorder.start.await_count == 2
            specs1 = recorder.start.await_args_list[0][0][1]
            assert len(specs1) == 1
            assert specs1[0].object_id == "MainWindow"

            specs2 = recorder.start.await_args_list[1][0][1]
            assert len(specs2) == 1
            assert specs2[0].object_id == "SubmitBtn"
            assert specs2[0].signals == ["clicked"]

    @pytest.mark.asyncio
    async def test_ui_clickItem_accepts_string_itemPath(self, mock_probe):
        with patch("qtpilot.server.require_probe", return_value=mock_probe):
            mcp = create_server(mode="native")
            async with Client(mcp) as client:
                await client.call_tool(
                    "qt_ui_clickItem",
                    {"objectId": "tree", "itemPath": "File > Save"},
                )
            mock_probe.call.assert_awaited_once_with(
                "qt.ui.clickItem",
                {
                    "objectId": "tree",
                    "itemPath": ["File", "Save"],
                    "column": 0,
                    "action": "click",
                    "expand": True,
                    "scroll": True,
                },
            )

    @pytest.mark.asyncio
    async def test_ui_click_accepts_list_position(self, mock_probe):
        with patch("qtpilot.server.require_probe", return_value=mock_probe):
            mcp = create_server(mode="native")
            async with Client(mcp) as client:
                await client.call_tool(
                    "qt_ui_click",
                    {"objectId": "btn", "position": [15, 25]},
                )
            mock_probe.call.assert_awaited_once_with(
                "qt.ui.click",
                {"objectId": "btn", "position": {"x": 15, "y": 25}},
            )

    @pytest.mark.asyncio
    async def test_ui_sendKeys_accepts_key_alias(self, mock_probe):
        with patch("qtpilot.server.require_probe", return_value=mock_probe):
            mcp = create_server(mode="native")
            async with Client(mcp) as client:
                await client.call_tool(
                    "qt_ui_sendKeys",
                    {"objectId": "input", "key": "Return"},
                )
            mock_probe.call.assert_awaited_once_with(
                "qt.ui.sendKeys",
                {"objectId": "input", "sequence": "Return"},
            )
