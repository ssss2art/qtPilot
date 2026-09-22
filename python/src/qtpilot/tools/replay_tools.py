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
        path: str | None = None,
        steps: list[dict] | None = None,
        settle: float = 0.1,
        ctx: Context = None,
    ) -> dict:
        """Re-drive a recorded session or inline steps against the connected application and report differences.

        Pass either `path` (to replay a recorded .jsonl session) or `steps` (to execute
        a batch sequence of actions directly in memory without writing to disk).

        Args:
            path: Path to a .jsonl message log written by qtpilot_log_start.
            steps: In-memory list of action step dictionaries: `[{"method": "qt.ui.click", "params": {...}}, ...]`.
            settle: Seconds to wait after each action for signals to arrive.

        Example: qtpilot_replay_run(path="scenarios/submit-form.jsonl", settle=0.25)
        Example: qtpilot_replay_run(steps=[{"method": "qt.ui.click", "params": {"objectId": "btn"}}])
        """
        from qtpilot.replay import load_scenario, run_scenario, scenario_from_steps
        from qtpilot.server import get_probe

        if (path is None) == (steps is None):
            raise ValueError("Provide exactly one of 'path' or 'steps'")

        probe = get_probe()
        if probe is None or not probe.is_connected:
            raise RuntimeError("Not connected to a probe -- replay drives a running application.")

        if path is not None:
            scenario = load_scenario(path)
        else:
            scenario = scenario_from_steps(steps or [])
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
