"""Tests verifying tool parameter schema consolidation and alias normalization."""

from __future__ import annotations

import json
from typing import Any
from unittest.mock import AsyncMock, patch
import pytest
from fastmcp import Client
from fastmcp.exceptions import ToolError

from qtpilot._mcp_compat import get_tool_input_schema
from qtpilot.alias_middleware import ALIAS_MAP
from qtpilot.server import create_server

_ALIAS_CASES: list[tuple[str, str, str]] = [
    (tool_name, alias_param, canonical_param)
    for tool_name, mapping in ALIAS_MAP.items()
    for alias_param, canonical_param in mapping.items()
]

_TOOL_BASE_ARGS: dict[str, dict[str, Any]] = {
    "qt_objects_inspect": {"objectId": "obj_1"},
    "qt_ui_sendKeys": {"objectId": "obj_1"},
    "qt_models_search": {"objectId": "model_1", "value": "needle"},
    "qt_ui_clickItem": {"objectId": "view_1"},
    "cu_leftClick": {"x": 10, "y": 20},
    "cu_rightClick": {"x": 10, "y": 20},
    "cu_middleClick": {"x": 10, "y": 20},
    "cu_doubleClick": {"x": 10, "y": 20},
    "cu_tripleClick": {"x": 10, "y": 20},
    "cu_mouseMove": {"x": 10, "y": 20},
    "cu_mouseDrag": {"startX": 10, "startY": 20, "endX": 30, "endY": 40},
    "cu_mouseDown": {"x": 10, "y": 20},
    "cu_mouseUp": {"x": 10, "y": 20},
    "cu_scroll": {"x": 10, "y": 20},
    "chr_formInput": {"ref": "input_1"},
}

_CANONICAL_SAMPLE_VALUES: dict[str, Any] = {
    "objectName": "myBtn",
    "className": "QPushButton",
    "objectNamePattern": "btn_*",
    "root": "win",
    "parts": ["info"],
    "declared_only": True,
    "property_name": "text",
    "sequence": "Return",
    "max_hits": 5,
    "itemPath": "Row 1",
    "screen_absolute": True,
    "delay_ms": 10,
    "key": "Return",
    "value": "Alice",
}


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
                search_props = set(get_tool_input_schema(search_tool).get("properties", {}).keys())
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
                assert "objectNamePattern" in search_props
                assert "name_pattern" not in search_props

                # Verify invoking with alias still works via AliasMiddleware
                await client.call_tool(
                    "qt_objects_search",
                    {"name": "myBtn", "class_name": "QPushButton", "rootId": "win"},
                )
                mock_probe.call.assert_awaited_with(
                    "qt.objects.search",
                    {"objectName": "myBtn", "className": "QPushButton", "root": "win"},
                )
                await client.call_tool("qt_objects_search", {"name_pattern": "dvTabletRail_*"})
                mock_probe.call.assert_awaited_with(
                    "qt.objects.search", {"objectNamePattern": "dvTabletRail_*"},
                )

                # 2. qt_ui_sendKeys
                send_keys_tool = tools_by_name["qt_ui_sendKeys"]
                send_keys_props = set(get_tool_input_schema(send_keys_tool).get("properties", {}).keys())
                assert "sequence" in send_keys_props
                assert "key" not in send_keys_props
                assert "keys" not in send_keys_props

                # 3. qt_models_search
                models_search_tool = tools_by_name["qt_models_search"]
                models_props = set(get_tool_input_schema(models_search_tool).get("properties", {}).keys())
                assert "max_hits" in models_props
                assert "maxHits" not in models_props

                # 4. qt_ui_clickItem
                click_item_tool = tools_by_name["qt_ui_clickItem"]
                click_item_props = set(get_tool_input_schema(click_item_tool).get("properties", {}).keys())
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
                click_props = set(get_tool_input_schema(click_tool).get("properties", {}).keys())
                assert "screen_absolute" in click_props
                assert "delay_ms" in click_props
                assert "screenAbsolute" not in click_props
                assert "delayMs" not in click_props

                # cu_key
                key_tool = tools_by_name["cu_key"]
                key_props = set(get_tool_input_schema(key_tool).get("properties", {}).keys())
                assert "key" in key_props
                assert "text" not in key_props

    @pytest.mark.asyncio
    @pytest.mark.parametrize("tool_name,alias_param,canonical_param", _ALIAS_CASES)
    async def test_alias_map_contract(
        self, tool_name: str, alias_param: str, canonical_param: str
    ) -> None:
        mock_probe = AsyncMock()
        mock_probe.is_connected = True
        mock_probe.call = AsyncMock(return_value={"ok": True})

        with patch("qtpilot.server.require_probe", return_value=mock_probe):
            mcp = create_server(mode="all")
            async with Client(mcp) as client:
                tools = await client.list_tools()
                tools_by_name = {t.name: t for t in tools}

                assert tool_name in tools_by_name, f"{tool_name} not registered in mode='all'"
                props = set(
                    get_tool_input_schema(tools_by_name[tool_name]).get("properties", {}).keys()
                )
                assert canonical_param in props, (
                    f"Canonical param {canonical_param!r} missing from {tool_name} schema"
                )
                assert alias_param not in props, (
                    f"Alias param {alias_param!r} still exposed in {tool_name} schema"
                )

                args = dict(_TOOL_BASE_ARGS.get(tool_name, {}))
                args[alias_param] = _CANONICAL_SAMPLE_VALUES[canonical_param]
                await client.call_tool(tool_name, args)
                mock_probe.call.assert_awaited_once()

    @pytest.mark.asyncio
    @pytest.mark.parametrize(
        "tool_name,args",
        [
            ("chr_click", {"ref": True}),
            ("chr_formInput", {"ref": True, "value": "Alice"}),
            ("chr_navigate", {"ref": False}),
        ],
    )
    async def test_chr_ref_rejects_bool(
        self, tool_name: str, args: dict[str, Any]
    ) -> None:
        mock_probe = AsyncMock()
        mock_probe.is_connected = True
        mock_probe.call = AsyncMock(return_value={"ok": True})

        with patch("qtpilot.server.require_probe", return_value=mock_probe):
            mcp = create_server(mode="all")
            async with Client(mcp) as client:
                with pytest.raises(ToolError):
                    await client.call_tool(tool_name, args)
                mock_probe.call.assert_not_awaited()
