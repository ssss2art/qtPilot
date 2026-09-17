"""Unit tests for replay tool registration and behaviour."""

from __future__ import annotations

import json

import pytest

from fastmcp import FastMCP

from qtpilot.tools.replay_tools import register_replay_tools


def _tool_names(mcp: FastMCP) -> set[str]:
    return set(mcp._tool_manager._tools.keys())


def _fn(mcp: FastMCP, name: str):
    return mcp._tool_manager._tools[name].fn


def write_log(tmp_path, entries: list[dict]) -> str:
    path = tmp_path / "session.jsonl"
    path.write_text("\n".join(json.dumps(e) for e in entries) + "\n")
    return str(path)


CLICK_SESSION = [
    {"ts": "t", "dir": "req", "id": 1, "method": "qt.ui.click", "params": {"objectId": "btn"}},
    {"ts": "t", "dir": "res", "id": 1, "method": "qt.ui.click", "dur_ms": 1.0, "result": {"ok": True}},
    {"ts": "t", "dir": "req", "id": 2, "method": "qt.properties.get", "params": {"objectId": "l", "name": "text"}},
    {"ts": "t", "dir": "res", "id": 2, "method": "qt.properties.get", "dur_ms": 1.0, "result": {"value": "clicked"}},
]

MCP_ONLY_SESSION = [
    {"ts": "t", "dir": "mcp_in", "tool": "qt_ui_click", "args": {"objectId": "btn"}},
    {"ts": "t", "dir": "mcp_out", "tool": "qt_ui_click", "dur_ms": 1.0, "ok": True},
]


class TestReplayToolRegistration:
    def test_registers_both_tools(self, mock_mcp):
        register_replay_tools(mock_mcp)
        assert _tool_names(mock_mcp) == {"qtpilot_replay_inspect", "qtpilot_replay_run"}


class TestReplayInspect:
    @pytest.mark.asyncio
    async def test_summarises_a_replayable_log(self, mock_mcp, tmp_path):
        register_replay_tools(mock_mcp)
        path = write_log(tmp_path, CLICK_SESSION)

        result = await _fn(mock_mcp, "qtpilot_replay_inspect")(path=path)

        assert result["replayable"] is True
        assert result["actions"] == ["qt.ui.click"]
        assert result["observations"] == 1
        assert result["source"] == path

    @pytest.mark.asyncio
    async def test_reports_a_log_with_nothing_to_drive(self, mock_mcp, tmp_path):
        # The reason this tool exists: a level-1 log looks fine and would replay green without
        # having driven anything. Inspect says so before a run is attempted.
        register_replay_tools(mock_mcp)
        path = write_log(tmp_path, MCP_ONLY_SESSION)

        result = await _fn(mock_mcp, "qtpilot_replay_inspect")(path=path)

        assert result["replayable"] is False
        assert result["actions"] == []

    @pytest.mark.asyncio
    async def test_inspect_drives_nothing(self, mock_mcp, tmp_path, monkeypatch):
        # Inspecting a log must be safe against a live application, so it must not reach a probe
        # at all -- not even to check whether one is connected.
        import qtpilot.server as server

        def explode():
            raise AssertionError("inspect must not touch the probe")

        monkeypatch.setattr(server, "get_probe", explode)
        register_replay_tools(mock_mcp)

        await _fn(mock_mcp, "qtpilot_replay_inspect")(path=write_log(tmp_path, CLICK_SESSION))


class TestReplayRun:
    @pytest.mark.asyncio
    async def test_refuses_to_run_without_a_probe(self, mock_mcp, tmp_path, monkeypatch):
        import qtpilot.server as server

        monkeypatch.setattr(server, "get_probe", lambda: None)
        register_replay_tools(mock_mcp)

        with pytest.raises(RuntimeError, match="Not connected"):
            await _fn(mock_mcp, "qtpilot_replay_run")(path=write_log(tmp_path, CLICK_SESSION))

    @pytest.mark.asyncio
    async def test_reports_a_clean_replay(self, mock_mcp, tmp_path, monkeypatch):
        import qtpilot.server as server

        class Probe:
            is_connected = True

            def __init__(self):
                self.handlers = []

            async def call(self, method, params=None, timeout=None):
                return {"value": "clicked"} if method == "qt.properties.get" else {"ok": True}

            def add_notification_handler(self, h):
                self.handlers.append(h)

            def remove_notification_handler(self, h):
                self.handlers.remove(h)

        monkeypatch.setattr(server, "get_probe", Probe)
        register_replay_tools(mock_mcp)

        result = await _fn(mock_mcp, "qtpilot_replay_run")(
            path=write_log(tmp_path, CLICK_SESSION), settle=0
        )

        assert result["passed"] is True
        assert result["divergence_count"] == 0
        assert "no divergence" in result["summary"]

    @pytest.mark.asyncio
    async def test_reports_a_divergence_in_a_form_a_reader_can_act_on(
        self, mock_mcp, tmp_path, monkeypatch
    ):
        import qtpilot.server as server

        class Probe:
            is_connected = True

            def __init__(self):
                self.handlers = []

            async def call(self, method, params=None, timeout=None):
                return {"value": "REGRESSED"} if method == "qt.properties.get" else {"ok": True}

            def add_notification_handler(self, h):
                self.handlers.append(h)

            def remove_notification_handler(self, h):
                self.handlers.remove(h)

        monkeypatch.setattr(server, "get_probe", Probe)
        register_replay_tools(mock_mcp)

        result = await _fn(mock_mcp, "qtpilot_replay_run")(
            path=write_log(tmp_path, CLICK_SESSION), settle=0
        )

        assert result["passed"] is False
        assert result["divergence_count"] == 1
        divergence = result["divergences"][0]
        assert divergence["step"] == 1
        assert divergence["method"] == "qt.properties.get"
        assert divergence["expected"] == {"value": "clicked"}
        assert divergence["actual"] == {"value": "REGRESSED"}
