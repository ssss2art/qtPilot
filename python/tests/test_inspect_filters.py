"""Tests for declared_only and property_name filtering in qt_objects_inspect."""

from __future__ import annotations

from unittest.mock import AsyncMock, patch
import pytest
from fastmcp import Client

from qtpilot.server import create_server


class TestInspectFilters:
    @pytest.mark.asyncio
    async def test_inspect_declared_only_sends_param_and_filters_base(self):
        mock_probe = AsyncMock()
        mock_probe.is_connected = True
        mock_probe.call = AsyncMock(
            return_value={
                "properties": [
                    {"name": "objectName", "type": "QString", "value": "myBtn"},
                    {"name": "palette", "type": "QPalette", "value": {}},
                    {"name": "font", "type": "QFont", "value": {}},
                    {"name": "text", "type": "QString", "value": "Click Me"},
                    {"name": "autoDefault", "type": "bool", "value": True},
                ]
            }
        )

        with patch("qtpilot.server.require_probe", return_value=mock_probe):
            mcp = create_server(mode="native")
            async with Client(mcp) as client:
                res = await client.call_tool(
                    "qt_objects_inspect",
                    {
                        "objectId": "btn1",
                        "parts": ["properties"],
                        "declared_only": True,
                    },
                )
                import json
                parsed = json.loads(res.content[0].text)
                props = parsed.get("properties", [])
                prop_names = [p["name"] for p in props]

                # Should filter out base QObject/QWidget properties
                assert "text" in prop_names
                assert "autoDefault" in prop_names
                assert "objectName" not in prop_names
                assert "palette" not in prop_names
                assert "font" not in prop_names

                mock_probe.call.assert_awaited_once_with(
                    "qt.objects.inspect",
                    {
                        "objectId": "btn1",
                        "parts": ["properties"],
                        "declaredOnly": True,
                    },
                )

    @pytest.mark.asyncio
    async def test_inspect_property_name_sends_param_and_filters(self):
        mock_probe = AsyncMock()
        mock_probe.is_connected = True
        mock_probe.call = AsyncMock(
            return_value={
                "properties": [
                    {"name": "text", "type": "QString", "value": "Click Me"},
                    {"name": "autoDefault", "type": "bool", "value": True},
                ]
            }
        )

        with patch("qtpilot.server.require_probe", return_value=mock_probe):
            mcp = create_server(mode="native")
            async with Client(mcp) as client:
                res = await client.call_tool(
                    "qt_objects_inspect",
                    {
                        "objectId": "btn1",
                        "parts": ["properties"],
                        "property_name": "text",
                    },
                )
                import json
                parsed = json.loads(res.content[0].text)
                props = parsed.get("properties", [])
                assert len(props) == 1
                assert props[0]["name"] == "text"

                mock_probe.call.assert_awaited_once_with(
                    "qt.objects.inspect",
                    {
                        "objectId": "btn1",
                        "parts": ["properties"],
                        "propertyName": "text",
                    },
                )
