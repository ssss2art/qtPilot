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
            "   - Batch multi-step macros in a single turn using `qtpilot_replay_run(steps=[...])`.\n"
            "   - Verify visual state with `qt_ui_screenshot()`."
        )

    @mcp.prompt
    def qt_generate_replay_test(goal: str, target_widget: str | None = None) -> str:
        """Guide for creating deterministic replay tests and batch macros."""
        target_info = f" Target widget: {target_widget}." if target_widget else ""
        return (
            f"You are generating a deterministic test scenario for goal: '{goal}'.{target_info}\n\n"
            "Best practices for deterministic replay in qtPilot:\n"
            "1. Use symbolic object names or canonical IDs where possible.\n"
            "2. Pair actions with expected state observations:\n"
            "   - An action triggers application state change (e.g. `qt.ui.click`, `qt.ui.sendKeys`).\n"
            "   - An observation verifies that the property changed (e.g. `qt.properties.get` with expected value).\n"
            "3. Execute the scenario inline using `qtpilot_replay_run(steps=[...])`:\n"
            "   ```json\n"
            "   [\n"
            "     {\n"
            "       \"method\": \"qt.ui.click\",\n"
            "       \"params\": {\"objectId\": \"submitButton\"},\n"
            "       \"observations\": [\n"
            "         {\n"
            "           \"method\": \"qt.properties.get\",\n"
            "           \"params\": {\"objectId\": \"statusLabel\", \"name\": \"text\"},\n"
            "           \"result\": \"Success\"\n"
            "         }\n"
            "       ]\n"
            "     }\n"
            "   ]\n"
            "   ```\n"
            "4. Inspect differences reported in `divergences` if the replay does not pass."
        )
