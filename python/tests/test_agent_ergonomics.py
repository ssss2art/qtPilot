"""Tests for Phase 3 agent ergonomics: inline replay, MCP prompts, and UI hierarchy resource."""

from __future__ import annotations

import json
from unittest.mock import AsyncMock, patch
import pytest
from fastmcp import Client

from qtpilot.server import create_server


class TestAgentErgonomics:
    @pytest.mark.asyncio
    async def test_replay_run_inline_steps(self):
        """Test inline replay execution without disk files."""
        class MockProbe:
            is_connected = True
            def __init__(self):
                self.calls = []
                self.handlers = []

            async def call(self, method, params=None, **kwargs):
                self.calls.append((method, params))
                return {"ok": True}

            def add_notification_handler(self, h):
                self.handlers.append(h)

            def remove_notification_handler(self, h):
                if h in self.handlers:
                    self.handlers.remove(h)

        mock_probe = MockProbe()

        with patch("qtpilot.server.get_probe", return_value=mock_probe):
            mcp = create_server(mode="native")
            async with Client(mcp) as client:
                steps = [
                    {"method": "qt.ui.click", "params": {"objectId": "btn1"}},
                    {"method": "qt.ui.sendKeys", "params": {"objectId": "input1", "text": "hello"}},
                ]
                res = await client.call_tool(
                    "qtpilot_replay_run",
                    {"steps": steps},
                )
                parsed = json.loads(res.content[0].text)
                assert parsed["passed"] is True
                assert parsed["steps_driven"] == 2
                methods_called = [m for m, p in mock_probe.calls]
                assert "qt.ui.click" in methods_called
                assert "qt.ui.sendKeys" in methods_called

    @pytest.mark.asyncio
    async def test_replay_run_validates_path_or_steps(self):
        """Test error raised when neither or both path and steps are provided."""
        mock_probe = AsyncMock()
        mock_probe.is_connected = True

        with patch("qtpilot.server.get_probe", return_value=mock_probe):
            mcp = create_server(mode="native")
            async with Client(mcp) as client:
                with pytest.raises(Exception) as exc1:
                    await client.call_tool("qtpilot_replay_run", {})
                assert "path" in str(exc1.value) or "steps" in str(exc1.value)

                with pytest.raises(Exception) as exc2:
                    await client.call_tool(
                        "qtpilot_replay_run",
                        {"path": "test.jsonl", "steps": [{"method": "qt.ping"}]},
                    )
                assert "both" in str(exc2.value) or "one of" in str(exc2.value)

    @pytest.mark.asyncio
    async def test_mcp_prompts_registered_and_retrievable(self):
        """Verify FastMCP prompts qt_explore_ui and qt_generate_replay_test."""
        mcp = create_server(mode="all")
        async with Client(mcp) as client:
            prompts = await client.list_prompts()
            prompt_names = {p.name for p in prompts}
            assert "qt_explore_ui" in prompt_names
            assert "qt_generate_replay_test" in prompt_names

            # Retrieve qt_explore_ui
            p1 = await client.get_prompt("qt_explore_ui", {})
            text1 = p1.messages[0].content.text
            assert "qt_objects_tree" in text1 or "compact" in text1

            # Retrieve qt_generate_replay_test
            p2 = await client.get_prompt("qt_generate_replay_test", {"goal": "login flow"})
            text2 = p2.messages[0].content.text
            assert "login flow" in text2

    @pytest.mark.asyncio
    async def test_ui_tree_resource(self):
        """Verify qtpilot://ui/tree resource returns compact outline."""
        mock_probe = AsyncMock()
        mock_probe.is_connected = True
        mock_probe.call = AsyncMock(
            return_value={
                "id": "win1",
                "className": "QMainWindow",
                "children": [
                    {
                        "id": "btn1",
                        "className": "QPushButton",
                        "objectName": "submitBtn",
                        "text": "Submit",
                        "visible": True,
                    }
                ],
            }
        )

        with patch("qtpilot.server.get_probe", return_value=mock_probe):
            mcp = create_server(mode="native")
            async with Client(mcp) as client:
                resources = await client.list_resources()
                uris = {str(r.uri) for r in resources}
                assert "qtpilot://ui/tree" in uris

                content = await client.read_resource("qtpilot://ui/tree")
                text = content[0].text
                assert "QMainWindow" in text
                assert "submitBtn" in text
                assert "Submit" in text
