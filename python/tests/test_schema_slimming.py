"""Tests verifying tool parameter schema consolidation and alias normalization."""

from __future__ import annotations

import json
from unittest.mock import AsyncMock, patch
import pytest
from fastmcp import Client

from qtpilot.server import create_server


class TestSchemaSlimming:
    @pytest.mark.asyncio
    async def test_native_tool_schemas_consolidated(self):
        mock_probe = AsyncMock()
        mock_probe.is_connected = True
        mock_probe.call = AsyncMock(return_value={"ok": True})

        with patch("qtpilot.server.require_probe", return_value=mock_probe):
            mcp = create_server(mode="native")
            async with Client(mcp) as client:
                tools = await client.list_tools()
                tools_by_name = {t.name: t for t in tools}

                # 1. qt_objects_search
                search_tool = tools_by_name["qt_objects_search"]
                search_props = set(search_tool.inputSchema.get("properties", {}).keys())
                assert "objectName" in search_props
                assert "className" in search_props
                assert "properties" in search_props
                assert "root" in search_props
                assert "limit" in search_props
                # Redundant aliases must NOT appear in JSON schema
                assert "name" not in search_props
                assert "class_name" not in search_props
                assert "root_id" not in search_props
                assert "rootId" not in search_props

                # Verify invoking with alias still works via AliasMiddleware
                await client.call_tool(
                    "qt_objects_search",
                    {"name": "myBtn", "class_name": "QPushButton", "rootId": "win"},
                )
                mock_probe.call.assert_awaited_with(
                    "qt.objects.search",
                    {"objectName": "myBtn", "className": "QPushButton", "root": "win"},
                )

                # 2. qt_ui_sendKeys
                send_keys_tool = tools_by_name["qt_ui_sendKeys"]
                send_keys_props = set(send_keys_tool.inputSchema.get("properties", {}).keys())
                assert "sequence" in send_keys_props
                assert "key" not in send_keys_props
                assert "keys" not in send_keys_props

                # 3. qt_models_search
                models_search_tool = tools_by_name["qt_models_search"]
                models_props = set(models_search_tool.inputSchema.get("properties", {}).keys())
                assert "max_hits" in models_props
                assert "maxHits" not in models_props

                # 4. qt_ui_clickItem
                click_item_tool = tools_by_name["qt_ui_clickItem"]
                click_item_props = set(click_item_tool.inputSchema.get("properties", {}).keys())
                assert "itemPath" in click_item_props
                assert "target" not in click_item_props

    @pytest.mark.asyncio
    async def test_cu_tool_schemas_consolidated(self):
        mock_probe = AsyncMock()
        mock_probe.is_connected = True
        mock_probe.call = AsyncMock(return_value={"ok": True})

        with patch("qtpilot.server.require_probe", return_value=mock_probe):
            mcp = create_server(mode="cu")
            async with Client(mcp) as client:
                tools = await client.list_tools()
                tools_by_name = {t.name: t for t in tools}

                # cu_leftClick
                click_tool = tools_by_name["cu_leftClick"]
                click_props = set(click_tool.inputSchema.get("properties", {}).keys())
                assert "screen_absolute" in click_props
                assert "delay_ms" in click_props
                assert "screenAbsolute" not in click_props
                assert "delayMs" not in click_props

                # cu_key
                key_tool = tools_by_name["cu_key"]
                key_props = set(key_tool.inputSchema.get("properties", {}).keys())
                assert "key" in key_props
                assert "text" not in key_props
