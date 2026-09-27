"""MCP prompt definitions for qtPilot workflows."""

from __future__ import annotations

from fastmcp import FastMCP


def register_prompts(mcp: FastMCP) -> None:
    """Register reusable prompts guiding AI assistants in Qt exploration and testing."""

    @mcp.prompt
    def qt_explore_ui() -> str:
        """Guide for efficiently exploring and interacting with a running Qt application."""
        return (
            "Recommended workflow to explore and interact with the connected Qt application:\n\n"
            "1. Overview & Hierarchy:\n"
            "   - Read the UI tree resource `qtpilot://ui/tree` or call `qt_objects_tree(maxDepth=2, visible_only=True)`.\n"
            "   - This gives an indented outline showing visible widgets, their classes, geometry, and object IDs (#N).\n\n"
            "2. Locating Widgets:\n"
            "   - Find specific controls by name or class: `qt_objects_search(className='QPushButton')`.\n"
            "   - Filter by property: `qt_objects_search(properties={'text': 'Save'})`.\n\n"
            "3. Inspecting Widgets:\n"
            "   - Inspect widgets without token bloat: `qt_objects_inspect(objectId=..., declared_only=True)`.\n"
            "   - To check a single property: `qt_objects_inspect(objectId=..., property_name='text')`.\n\n"
            "4. Interacting:\n"
            "   - Click controls: `qt_ui_click(objectId=...)`.\n"
            "   - Type text: `qt_ui_sendKeys(objectId=..., text='...')`.\n"
            "   - Batch multi-step macros in a single turn using `qtpilot_replay_run(steps=[...], exploratory=True)`; these macros do not certify acceptance.\n"
            "   - Verify visual state with `qt_ui_screenshot()`."
        )

    @mcp.prompt
    def qt_generate_replay_test(goal: str, target_widget: str | None = None) -> str:
        """Guide for creating deterministic replay tests and batch macros."""
        target_info = f" Target widget: {target_widget}." if target_widget else ""
        return (
            f"Create a version-2 replay contract for goal: '{goal}'.{target_info}\n\n"
            "1. Prepare the application fixture explicitly; replay does not reset arbitrary applications.\n"
            "2. Declare format=2, fixture, timeout (seconds), requirements, preconditions and steps.\n"
            "3. Requirements and preconditions are read-only assertions: name, method, params, path and expected.\n"
            "   Example: qt.properties.get with path=['result', 'value'] and an exact expected value.\n"
            "4. Each named step declares action={method, params}, postconditions, and optionally a checkpoint.\n"
            "   checkpoint={objectId, signal, arguments} subscribes before the action; correlate arguments when causal attribution matters.\n"
            "5. Execute qtpilot_replay_run(contract={...}) or save JSON and pass path.\n"
            "6. Require strict_passed=true. Inspect failure_kind, comparisons and loss counters on failure.\n"
            "Missing, truncated, normalized or lost evidence cannot certify strict acceptance.\n"
            "Use exploratory=True only for diagnostic JSONL transcripts or unasserted steps."
        )
