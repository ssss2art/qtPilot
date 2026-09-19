"""Unit tests for legacy compatibility shims (ghost tools)."""

from __future__ import annotations

from unittest.mock import AsyncMock, MagicMock, patch

import pytest
from fastmcp import Client, FastMCP

from qtpilot import _mcp_compat as mcp_compat
from qtpilot.server import create_server
from qtpilot.tools.legacy import LEGACY_GHOST_TOOLS


async def _tool_names(mcp: FastMCP) -> set[str]:
    return set(await mcp_compat.list_tool_names(mcp))


def _result_data(res):
    return res.data if res.data is not None else res.structured_content


class TestLegacyGhostVisibility:
    @pytest.mark.asyncio
    async def test_legacy_tools_are_hidden_from_public_list(self):
        """None of the legacy tools should appear in tools/list across any mode."""
        for mode, expected_count in [
            ("native", 41),
            ("cu", 23),
            ("chrome", 18),
            ("all", 62),
        ]:
            mcp = create_server(mode=mode)
            names = await _tool_names(mcp)
            assert len(names) == expected_count
            for legacy in LEGACY_GHOST_TOOLS:
                assert legacy not in names, f"{legacy} should not be in public tools/list for mode {mode}"

    @pytest.mark.asyncio
    async def test_legacy_tools_are_resolvable(self):
        """All legacy tools must resolve via get_tool / find_tool."""
        mcp = create_server(mode="native")
        for legacy in LEGACY_GHOST_TOOLS:
            tool = await mcp_compat.find_tool(mcp, legacy)
            assert tool is not None, f"Legacy tool {legacy} should be resolvable"


class TestLegacyForwarding:
    @pytest.fixture
    def mock_probe(self):
        probe = MagicMock()
        probe.call = AsyncMock(return_value={"ok": True})
        probe.is_connected = True
        probe.ws_url = "ws://localhost:9222"
        return probe

    @pytest.mark.asyncio
    async def test_qt_objects_find_forwards_to_search(self, mock_probe):
        with patch("qtpilot.server.require_probe", return_value=mock_probe):
            mcp = create_server(mode="native")
            async with Client(mcp) as client:
                await client.call_tool("qt_objects_find", {"name": "testButton", "root": "win1"})
            mock_probe.call.assert_awaited_once_with(
                "qt.objects.search", {"objectName": "testButton", "root": "win1"}
            )

    @pytest.mark.asyncio
    async def test_qt_objects_findByClass_forwards_to_search(self, mock_probe):
        with patch("qtpilot.server.require_probe", return_value=mock_probe):
            mcp = create_server(mode="native")
            async with Client(mcp) as client:
                await client.call_tool("qt_objects_findByClass", {"className": "QPushButton"})
            mock_probe.call.assert_awaited_once_with(
                "qt.objects.search", {"className": "QPushButton"}
            )

    @pytest.mark.asyncio
    async def test_qt_objects_query_forwards_to_search(self, mock_probe):
        with patch("qtpilot.server.require_probe", return_value=mock_probe):
            mcp = create_server(mode="native")
            async with Client(mcp) as client:
                await client.call_tool(
                    "qt_objects_query",
                    {"className": "QLabel", "properties": {"text": "Hello"}},
                )
            mock_probe.call.assert_awaited_once_with(
                "qt.objects.search",
                {"className": "QLabel", "properties": {"text": "Hello"}},
            )

    @pytest.mark.asyncio
    async def test_qt_objects_info_forwards_to_inspect(self, mock_probe):
        mock_probe.call.return_value = {
            "objectId": "btn",
            "info": {"className": "QPushButton", "objectName": "btn"},
        }
        with patch("qtpilot.server.require_probe", return_value=mock_probe):
            mcp = create_server(mode="native")
            async with Client(mcp) as client:
                res = await client.call_tool("qt_objects_info", {"objectId": "btn"})
            mock_probe.call.assert_awaited_once_with(
                "qt.objects.inspect", {"objectId": "btn", "parts": ["info"]}
            )
            assert _result_data(res) == {"className": "QPushButton", "objectName": "btn"}

    @pytest.mark.asyncio
    async def test_qt_properties_list_forwards_to_inspect(self, mock_probe):
        mock_probe.call.return_value = {
            "objectId": "btn",
            "properties": [{"name": "text", "value": "Click"}],
        }
        with patch("qtpilot.server.require_probe", return_value=mock_probe):
            mcp = create_server(mode="native")
            async with Client(mcp) as client:
                res = await client.call_tool("qt_properties_list", {"objectId": "btn"})
            mock_probe.call.assert_awaited_once_with(
                "qt.objects.inspect", {"objectId": "btn", "parts": ["properties"]}
            )
            assert _result_data(res) == {
                "objectId": "btn",
                "properties": [{"name": "text", "value": "Click"}],
            }

    @pytest.mark.asyncio
    async def test_qt_methods_list_forwards_to_inspect(self, mock_probe):
        mock_probe.call.return_value = {
            "objectId": "btn",
            "methods": [{"name": "click"}],
        }
        with patch("qtpilot.server.require_probe", return_value=mock_probe):
            mcp = create_server(mode="native")
            async with Client(mcp) as client:
                res = await client.call_tool("qt_methods_list", {"objectId": "btn"})
            mock_probe.call.assert_awaited_once_with(
                "qt.objects.inspect", {"objectId": "btn", "parts": ["methods"]}
            )
            assert _result_data(res) == {
                "objectId": "btn",
                "methods": [{"name": "click"}],
            }

    @pytest.mark.asyncio
    async def test_qt_signals_list_forwards_to_inspect(self, mock_probe):
        mock_probe.call.return_value = {
            "objectId": "btn",
            "signals": [{"name": "clicked"}],
        }
        with patch("qtpilot.server.require_probe", return_value=mock_probe):
            mcp = create_server(mode="native")
            async with Client(mcp) as client:
                res = await client.call_tool("qt_signals_list", {"objectId": "btn"})
            mock_probe.call.assert_awaited_once_with(
                "qt.objects.inspect", {"objectId": "btn", "parts": ["signals"]}
            )
            assert _result_data(res) == {
                "objectId": "btn",
                "signals": [{"name": "clicked"}],
            }

    @pytest.mark.asyncio
    async def test_qt_qml_inspect_forwards_to_inspect(self, mock_probe):
        mock_probe.call.return_value = {
            "objectId": "qmlItem",
            "qml": {"isQmlItem": True},
        }
        with patch("qtpilot.server.require_probe", return_value=mock_probe):
            mcp = create_server(mode="native")
            async with Client(mcp) as client:
                res = await client.call_tool("qt_qml_inspect", {"objectId": "qmlItem"})
            mock_probe.call.assert_awaited_once_with(
                "qt.objects.inspect", {"objectId": "qmlItem", "parts": ["qml"]}
            )
            assert _result_data(res) == {"isQmlItem": True}

    @pytest.mark.asyncio
    async def test_qt_models_info_forwards_to_inspect(self, mock_probe):
        mock_probe.call.return_value = {
            "objectId": "tree",
            "model": {"rowCount": 10},
        }
        with patch("qtpilot.server.require_probe", return_value=mock_probe):
            mcp = create_server(mode="native")
            async with Client(mcp) as client:
                res = await client.call_tool("qt_models_info", {"objectId": "tree"})
            mock_probe.call.assert_awaited_once_with(
                "qt.objects.inspect", {"objectId": "tree", "parts": ["model"]}
            )
            assert _result_data(res) == {"rowCount": 10}

    @pytest.mark.asyncio
    async def test_qt_modes_returns_available_modes(self):
        mcp = create_server(mode="native")
        async with Client(mcp) as client:
            res = await client.call_tool("qt_modes", {})
        assert "modes" in _result_data(res)
        assert "native" in _result_data(res)["modes"]

    @pytest.mark.asyncio
    async def test_qtpilot_list_probes_returns_probes(self):
        mcp = create_server(mode="native")
        async with Client(mcp) as client:
            res = await client.call_tool("qtpilot_list_probes", {})
        assert "probes" in _result_data(res)

    @pytest.mark.asyncio
    async def test_qtpilot_get_mode_returns_mode(self):
        mcp = create_server(mode="native")
        async with Client(mcp) as client:
            res = await client.call_tool("qtpilot_get_mode", {})
        assert _result_data(res) == {"mode": "native"}

    @pytest.mark.asyncio
    async def test_qtPilot_probe_status_returns_status(self):
        mcp = create_server(mode="native")
        async with Client(mcp) as client:
            res = await client.call_tool("qtPilot_probe_status", {})
        assert "mode" in _result_data(res)
        assert "connection" in _result_data(res)

    @pytest.mark.asyncio
    async def test_qtpilot_log_tail_forwards_to_status(self):
        mcp = create_server(mode="native")
        async with Client(mcp) as client:
            res = await client.call_tool("qtpilot_log_tail", {"count": 5})
        assert "entries" in _result_data(res)

    @pytest.mark.asyncio
    async def test_qt_events_startCapture_forwards(self, mock_probe):
        with patch("qtpilot.server.require_probe", return_value=mock_probe):
            mcp = create_server(mode="native")
            async with Client(mcp) as client:
                await client.call_tool("qt_events_startCapture", {})
            mock_probe.call.assert_awaited_once_with("qt.events.start")

    @pytest.mark.asyncio
    async def test_qt_events_stopCapture_forwards(self, mock_probe):
        with patch("qtpilot.server.require_probe", return_value=mock_probe):
            mcp = create_server(mode="native")
            async with Client(mcp) as client:
                await client.call_tool("qt_events_stopCapture", {})
            mock_probe.call.assert_awaited_once_with("qt.events.stop")

    @pytest.mark.asyncio
    async def test_qt_models_find_forwards_to_search(self, mock_probe):
        with patch("qtpilot.server.require_probe", return_value=mock_probe):
            mcp = create_server(mode="native")
            async with Client(mcp) as client:
                await client.call_tool(
                    "qt_models_find",
                    {"objectId": "model1", "value": "itemA"},
                )
            mock_probe.call.assert_awaited_once_with(
                "qt.models.search",
                {
                    "objectId": "model1",
                    "value": "itemA",
                    "column": 0,
                    "role": "display",
                    "match": "contains",
                    "maxHits": 10,
                },
            )
