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
    async def qtpilot_replay_inspect(path: str, exploratory: bool = False, ctx: Context = None) -> dict:
        """Summarise a recorded message log without driving anything.

        Reports how many actions the log would replay, what it would assert on, and whether it
        is replayable at all. Worth running before qtpilot_replay_run on an unfamiliar log: a
        level-1 log records tool names but no wire traffic, so it has nothing to drive.

        Args:
            path: Path to a .jsonl message log written by qtpilot_log_start.

        Example: qtpilot_replay_inspect(path="qtPilot-log-20260906-101500.jsonl")
        """
        from qtpilot.replay import load_scenario
        from qtpilot.replay_contract import load_contract

        if not exploratory:
            parsed = load_contract(path)
            if parsed.is_err():
                raise ValueError(f"{parsed.unwrap_err()}; diagnostic transcripts require exploratory=True")
            return {"mode": "strict", "contract": parsed.unwrap().to_dict()}

        scenario = load_scenario(path)
        actions = [s.action.method for s in scenario.steps if s.action]

        return {
            "mode": "exploratory",
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
        settle: float | None = None,
        contract: dict[str, object] | None = None,
        exploratory: bool = False,
        ctx: Context = None,
    ) -> dict:
        """Validate a version-2 contract against the connected application.

        Supply exactly one of path, contract (inline version-2 JSON), or steps.
        Diagnostic JSONL paths and unasserted inline steps require exploratory=True;
        their normalized comparisons never certify strict acceptance. settle is
        exploratory-only. Strict completion uses the contract's signals/postconditions.
        """
        import json
        from qtpilot.contract_runner import run_contract
        from qtpilot.replay_contract import load_contract, parse_contract
        from qtpilot.replay import load_scenario, run_scenario, scenario_from_steps
        from qtpilot.server import get_probe

        if sum(value is not None for value in (path, steps, contract)) != 1:
            raise ValueError("Provide exactly one of 'path', 'contract' or 'steps'")

        if not exploratory:
            if steps is not None or settle is not None:
                raise ValueError("Inline steps and settling delays require exploratory=True; use a version-2 contract for acceptance")
            parsed = load_contract(path) if path is not None else parse_contract(json.dumps(contract, allow_nan=False))
            if parsed.is_err():
                raise ValueError(f"{parsed.unwrap_err()}; diagnostic transcripts require exploratory=True")
        elif contract is not None:
            raise ValueError("Version-2 contracts must run in strict mode")

        probe = get_probe()
        if probe is None or not probe.is_connected:
            raise RuntimeError("Not connected to a probe -- replay drives a running application.")

        if not exploratory:
            return (await run_contract(parsed.unwrap(), probe)).to_dict()

        if path is not None:
            scenario = load_scenario(path)
        else:
            scenario = scenario_from_steps(steps or [])
        result = await run_scenario(scenario, probe, settle=0.1 if settle is None else settle)

        return {
            "mode": "exploratory",
            "strict_passed": False,
            "source": result.scenario.source,
            "passed": result.passed,
            "summary": result.summary(),
            "steps_driven": len(result.steps),
            "aborted_at": result.aborted_at,
            "abort_reason": result.abort_reason,
            "failure_kind": result.failure_kind,
            "actions_driven": result.actions_driven,
            "divergence_count": len(result.divergences),
            "divergences": _divergence_dicts(result.divergences),
        }
