"""Replay tools -- inspect and re-run a recorded session against the live application."""

from __future__ import annotations

from fastmcp import Context, FastMCP


def _divergence_dicts(divergences: list) -> list[dict]:
    """Render divergences for an MCP result, capped so one bad run cannot flood the reply."""
    return [
        {
            "step": d.step,
            "kind": d.kind,
            "method": d.method,
            "expected": d.expected,
            "actual": d.actual,
        }
        for d in divergences[:50]
    ]


def register_replay_tools(mcp: FastMCP) -> None:
    """Register scenario replay tools on the MCP server."""

    @mcp.tool
    async def qtpilot_replay_inspect(path: str, ctx: Context = None) -> dict:
        """Summarise a recorded message log without driving anything.

        Reports how many actions the log would replay, what it would assert on, and whether it
        is replayable at all. Worth running before qtpilot_replay_run on an unfamiliar log: a
        level-1 log records tool names but no wire traffic, so it has nothing to drive.

        Args:
            path: Path to a .jsonl message log written by qtpilot_log_start.

        Example: qtpilot_replay_inspect(path="qtPilot-log-20260906-101500.jsonl")
        """
        from qtpilot.replay import load_scenario

        scenario = load_scenario(path)
        actions = [s.action.method for s in scenario.steps if s.action]

        return {
            "source": scenario.source,
            "replayable": scenario.is_replayable,
            "steps": len(scenario.steps),
            "actions": actions,
            "observations": sum(len(s.observations) for s in scenario.steps),
            "notifications": sum(len(s.notifications) for s in scenario.steps),
        }

    @mcp.tool
    async def qtpilot_replay_run(
        path: str,
        settle: float = 0.1,
        ctx: Context = None,
    ) -> dict:
        """Re-drive a recorded session against the connected application and report differences.

        Re-issues the recorded actions in order, re-issues the recorded observations after each
        one, and compares. Timings, request ids and generated object handles are ignored, so a
        difference means the application behaved differently -- not that the clock moved.

        The application must be in the same state the recording started from. Replay drives
        input; it does not reset anything.

        Args:
            path: Path to a .jsonl message log written by qtpilot_log_start at level 2 or above.
            settle: Seconds to wait after each action for signals to arrive. Raise it for an
                application that updates asynchronously; a too-short window reports a race as a
                divergence.

        Example: qtpilot_replay_run(path="scenarios/submit-form.jsonl", settle=0.25)
        """
        from qtpilot.replay import load_scenario, run_scenario
        from qtpilot.server import get_probe

        probe = get_probe()
        if probe is None or not probe.is_connected:
            raise RuntimeError("Not connected to a probe -- replay drives a running application.")

        scenario = load_scenario(path)
        result = await run_scenario(scenario, probe, settle=settle)

        return {
            "source": result.scenario.source,
            "passed": result.passed,
            "summary": result.summary(),
            "steps_driven": len(result.steps),
            "aborted_at": result.aborted_at,
            "abort_reason": result.abort_reason,
            "divergence_count": len(result.divergences),
            "divergences": _divergence_dicts(result.divergences),
        }
