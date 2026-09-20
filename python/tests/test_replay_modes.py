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
        assert "cu.drag" in MUTATING_METHODS
        assert "cu.type" in MUTATING_METHODS
        assert "cu.key" in MUTATING_METHODS
        assert "cu.scroll" in MUTATING_METHODS
        assert "cu.cursorPosition" in OBSERVING_METHODS

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
