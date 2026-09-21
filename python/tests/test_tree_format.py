"""Tests for token-efficient compact object tree formatting."""

from __future__ import annotations

from unittest.mock import AsyncMock, patch

import pytest
from fastmcp import Client

from qtpilot.server import create_server
from qtpilot.tree_format import format_compact_tree


@pytest.fixture
def sample_object_tree() -> dict:
    """Sample Qt object tree resembling a desktop application."""
    return {
        "id": "",
        "className": "Root",
        "children": [
            {
                "id": "MainWindow",
                "className": "QMainWindow",
                "objectName": "MainWindow",
                "visible": True,
                "geometry": {"x": 0, "y": 0, "width": 1024, "height": 768},
                "text": "Control Center",
                "children": [
                    {
                        "id": "MainWindow/centralWidget",
                        "className": "QWidget",
                        "objectName": "centralWidget",
                        "visible": True,
                        "geometry": {"x": 0, "y": 24, "width": 1024, "height": 744},
                        "children": [
                            {
                                "id": "MainWindow/centralWidget/btn_start",
                                "className": "QPushButton",
                                "objectName": "btn_start",
                                "text": "Start Engine",
                                "visible": True,
                                "geometry": {"x": 20, "y": 30, "width": 120, "height": 36},
                            },
                            {
                                "id": "MainWindow/centralWidget/lbl_status",
                                "className": "QLabel",
                                "objectName": "lbl_status",
                                "text": "Engine Idle",
                                "visible": True,
                                "geometry": {"x": 160, "y": 30, "width": 200, "height": 36},
                            },
                            {
                                "id": "MainWindow/centralWidget/secret_panel",
                                "className": "QFrame",
                                "objectName": "secret_panel",
                                "visible": False,
                                "geometry": {"x": 0, "y": 100, "width": 400, "height": 200},
                                "children": [
                                    {
                                        "id": "MainWindow/centralWidget/secret_panel/btn_override",
                                        "className": "QPushButton",
                                        "objectName": "btn_override",
                                        "text": "Admin Override",
                                        "visible": True,
                                        "geometry": {"x": 10, "y": 10, "width": 100, "height": 30},
                                    }
                                ],
                            },
                        ],
                    }
                ],
            }
        ],
    }


class TestFormatCompactTree:
    def test_format_compact_tree_basic(self, sample_object_tree):
        outline = format_compact_tree(sample_object_tree)
        lines = outline.splitlines()

        assert len(lines) == 6
        # Line 0: Root MainWindow
        assert lines[0] == 'MainWindow (QMainWindow) [1024x768] "Control Center" #MainWindow'
        # Line 1: centralWidget indented by 2 spaces
        assert lines[1] == "  centralWidget (QWidget) [1024x744] #MainWindow/centralWidget"
        # Line 2: btn_start indented by 4 spaces
        assert lines[2] == '    btn_start (QPushButton) [120x36] "Start Engine" #MainWindow/centralWidget/btn_start'
        # Line 3: lbl_status
        assert lines[3] == '    lbl_status (QLabel) [200x36] "Engine Idle" #MainWindow/centralWidget/lbl_status'
        # Line 4: secret_panel with (hidden) flag
        assert lines[4] == "    secret_panel (QFrame) [400x200] (hidden) #MainWindow/centralWidget/secret_panel"

    def test_format_compact_tree_visible_only(self, sample_object_tree):
        outline = format_compact_tree(sample_object_tree, visible_only=True)
        lines = outline.splitlines()

        # Hidden secret_panel and its children must be omitted
        assert len(lines) == 4
        assert not any("secret_panel" in line for line in lines)
        assert not any("btn_override" in line for line in lines)

    def test_format_compact_tree_qml_item(self):
        qml_tree = {
            "id": "root_item",
            "className": "QQuickItem",
            "objectName": "hud",
            "visible": True,
            "isQmlItem": True,
            "qmlId": "gameHud",
            "qmlTypeName": "HudView",
            "geometry": {"x": 0, "y": 0, "width": 640, "height": 480},
        }
        outline = format_compact_tree(qml_tree)
        assert "hud (QQuickItem)" in outline
        assert "qml:gameHud" in outline
        assert "#root_item" in outline

    def test_format_compact_tree_truncates_long_text(self):
        long_tree = {
            "id": "item1",
            "className": "QLabel",
            "text": "This is a very long text property that should be trimmed to conserve agent context",
        }
        outline = format_compact_tree(long_tree)
        assert "..." in outline
        assert len(outline) < 80

    def test_format_compact_tree_empty(self):
        assert format_compact_tree({}) == "<empty tree>"
        assert format_compact_tree({"className": "Root", "children": []}) == "<empty tree>"


class TestObjectsTreeToolIntegration:
    @pytest.mark.asyncio
    async def test_objects_tree_defaults_to_compact_outline(self, sample_object_tree):
        mock_probe = AsyncMock()
        mock_probe.is_connected = True
        mock_probe.call = AsyncMock(return_value=sample_object_tree)

        with patch("qtpilot.server.require_probe", return_value=mock_probe):
            mcp = create_server(mode="native")
            async with Client(mcp) as client:
                res = await client.call_tool("qt_objects_tree", {})
                assert len(res.content) == 1
                text = res.content[0].text
                # Compact outline text, not raw JSON
                assert text.startswith("MainWindow (QMainWindow)")
                assert "btn_start" in text
                mock_probe.call.assert_awaited_with("qt.objects.tree", {"maxDepth": 3})

    @pytest.mark.asyncio
    async def test_objects_tree_format_json(self, sample_object_tree):
        mock_probe = AsyncMock()
        mock_probe.is_connected = True
        mock_probe.call = AsyncMock(return_value=sample_object_tree)

        with patch("qtpilot.server.require_probe", return_value=mock_probe):
            mcp = create_server(mode="native")
            async with Client(mcp) as client:
                res = await client.call_tool("qt_objects_tree", {"format": "json"})
                assert len(res.content) == 1
                import json
                parsed = json.loads(res.content[0].text)
                assert parsed["className"] == "Root"
                assert len(parsed["children"]) == 1

    @pytest.mark.asyncio
    async def test_objects_tree_visible_only(self, sample_object_tree):
        mock_probe = AsyncMock()
        mock_probe.is_connected = True
        mock_probe.call = AsyncMock(return_value=sample_object_tree)

        with patch("qtpilot.server.require_probe", return_value=mock_probe):
            mcp = create_server(mode="native")
            async with Client(mcp) as client:
                res = await client.call_tool("qt_objects_tree", {"visible_only": True})
                text = res.content[0].text
                assert "secret_panel" not in text
                assert "btn_start" in text

    @pytest.mark.asyncio
    async def test_objects_tree_invalid_format_raises_value_error(self, sample_object_tree):
        mock_probe = AsyncMock()
        mock_probe.is_connected = True
        mock_probe.call = AsyncMock(return_value=sample_object_tree)

        with patch("qtpilot.server.require_probe", return_value=mock_probe):
            mcp = create_server(mode="native")
            async with Client(mcp) as client:
                with pytest.raises(Exception) as exc_info:
                    await client.call_tool("qt_objects_tree", {"format": "yaml"})
                assert "format" in str(exc_info.value).lower()
