"""Unit tests for multi-mode replay support (cu.* and chr.*)."""

from __future__ import annotations

import pytest
from qtpilot.fluent import expect_replay
from qtpilot.replay import (
    MUTATING_METHODS,
    OBSERVING_METHODS,
    parse_entries,
    run_scenario,
)


class TestReplayModes:
    def test_cu_methods_in_mutating_and_observing(self):
        """Computer Use methods must be recognized in MUTATING_METHODS and OBSERVING_METHODS."""
        assert "cu.click" in MUTATING_METHODS
        assert "cu.mouseMove" in MUTATING_METHODS
        assert "cu.mouseDrag" in MUTATING_METHODS
        assert "cu.type" in MUTATING_METHODS
        assert "cu.key" in MUTATING_METHODS
        assert "cu.scroll" in MUTATING_METHODS
        assert "cu.cursorPosition" in OBSERVING_METHODS

    def test_native_menu_methods_are_mutating(self):
        """Public menu actions must be re-driven rather than silently skipped."""
        assert "qt.ui.contextMenu" in MUTATING_METHODS
        assert "qt.ui.activateMenuItem" in MUTATING_METHODS

    def test_chrome_methods_in_mutating_and_observing(self):
        """Chrome mode methods must be recognized in MUTATING_METHODS and OBSERVING_METHODS."""
        assert "chr.click" in MUTATING_METHODS
        assert "chr.formInput" in MUTATING_METHODS
        assert "chr.navigate" in MUTATING_METHODS
        assert "chr.readPage" in OBSERVING_METHODS
        assert "chr.getPageText" in OBSERVING_METHODS
        assert "chr.find" in OBSERVING_METHODS

    def test_cu_session_parsing(self):
        """A session with cu.* calls must parse into steps with actions and observations, not unsupported."""
        entries = [
            {"dir": "req", "id": 1, "method": "cu.click", "params": {"x": 100, "y": 200}},
            {"dir": "res", "id": 1, "method": "cu.click", "result": {"ok": True}},
            {"dir": "req", "id": 2, "method": "cu.cursorPosition", "params": {}},
            {"dir": "res", "id": 2, "method": "cu.cursorPosition", "result": {"x": 100, "y": 200}},
        ]
        scenario = parse_entries(entries)
        assert scenario.is_replayable
        assert len(scenario.unsupported) == 0
        assert len(scenario.steps) == 2  # baseline + step 1
        assert scenario.steps[1].action.method == "cu.click"
        assert len(scenario.steps[1].observations) == 1
        assert scenario.steps[1].observations[0].method == "cu.cursorPosition"

    @pytest.mark.asyncio
    async def test_public_mouse_drag_and_menu_actions_are_replayed(self):
        """Wire methods emitted by public tools must each form a replay action."""
        entries = [
            {"dir": "req", "id": 1, "method": "cu.mouseDrag", "params": {"startX": 1, "startY": 2, "endX": 3, "endY": 4}},
            {"dir": "res", "id": 1, "method": "cu.mouseDrag", "result": {"ok": True}},
            {"dir": "req", "id": 2, "method": "qt.ui.contextMenu", "params": {"objectId": "tree"}},
            {"dir": "res", "id": 2, "method": "qt.ui.contextMenu", "result": {"ok": True}},
            {"dir": "req", "id": 3, "method": "qt.ui.activateMenuItem", "params": {"text": "Delete"}},
            {"dir": "res", "id": 3, "method": "qt.ui.activateMenuItem", "result": {"ok": True}},
        ]

        scenario = parse_entries(entries)

        assert scenario.is_replayable
        assert scenario.unsupported == {}
        assert [step.action.method for step in scenario.steps[1:]] == [
            "cu.mouseDrag",
            "qt.ui.contextMenu",
            "qt.ui.activateMenuItem",
        ]

        class RecordingProbe:
            is_connected = True

            def __init__(self):
                self.calls = []

            def add_notification_handler(self, handler):
                pass

            def remove_notification_handler(self, handler):
                pass

            async def call(self, method, params=None, **kwargs):
                self.calls.append((method, params))
                return {"ok": True}

        probe = RecordingProbe()
        result = await run_scenario(scenario, probe, settle=0)

        assert result.divergences == []
        assert [method for method, _ in probe.calls if method != "qt.sync"] == [
            "cu.mouseDrag",
            "qt.ui.contextMenu",
            "qt.ui.activateMenuItem",
        ]

    def test_chrome_session_parsing(self):
        """A session with chr.* calls must parse into steps with actions and observations."""
        entries = [
            {"dir": "req", "id": 1, "method": "chr.click", "params": {"ref": "btn1"}},
            {"dir": "res", "id": 1, "method": "chr.click", "result": {"ok": True}},
            {"dir": "req", "id": 2, "method": "chr.getPageText", "params": {}},
            {"dir": "res", "id": 2, "method": "chr.getPageText", "result": {"text": "Submitted"}},
        ]
        scenario = parse_entries(entries)
        assert scenario.is_replayable
        assert len(scenario.unsupported) == 0
        assert scenario.steps[1].action.method == "chr.click"
        assert scenario.steps[1].observations[0].method == "chr.getPageText"

    @pytest.mark.asyncio
    async def test_cu_session_replay_clean_and_divergence(self):
        """Simulate replaying a cu session against a mock probe using fluent assertions."""
        entries = [
            {"dir": "req", "id": 1, "method": "cu.type", "params": {"text": "Hello"}},
            {"dir": "res", "id": 1, "method": "cu.type", "result": {"ok": True}},
            {"dir": "req", "id": 2, "method": "cu.cursorPosition", "params": {}},
            {"dir": "res", "id": 2, "method": "cu.cursorPosition", "result": {"x": 50, "y": 50}},
        ]
        scenario = parse_entries(entries)

        class MatchingProbe:
            is_connected = True
            def add_notification_handler(self, h): pass
            def remove_notification_handler(self, h): pass
            async def call(self, method, params=None, **kwargs):
                if method == "cu.cursorPosition":
                    return {"x": 50, "y": 50}
                return {"ok": True}

        res_clean = await run_scenario(scenario, MatchingProbe(), settle=0)
        expect_replay(res_clean).to_pass().to_have_divergence_count(0)

        class DivergingProbe:
            is_connected = True
            def add_notification_handler(self, h): pass
            def remove_notification_handler(self, h): pass
            async def call(self, method, params=None, **kwargs):
                if method == "cu.cursorPosition":
                    return {"x": 999, "y": 999}
                return {"ok": True}

        res_div = await run_scenario(scenario, DivergingProbe(), settle=0)
        expect_replay(res_div).to_diverge_at(step=1, kind="observation", method="cu.cursorPosition")
