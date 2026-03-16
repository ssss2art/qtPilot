# Dynamic Mode Switching for qtPilot Server

## Context

The probe runs in "all" mode by default (all three API sets registered), but the MCP server CLI requires `--mode` to be one of `native`, `cu`, or `chrome`. This forces the user to hardcode a mode in `.mcp.json` at startup, with no way to change it at runtime. The user wants to either have all tools available by default, or be able to switch modes dynamically via a tool call.

## Approach

Make `--mode` optional (default: `"native"`), and add a `qtpilot_set_mode` tool that dynamically adds/removes mode-specific tools at runtime using FastMCP's `add_tool`/`remove_tool` + `send_tool_list_changed()`. Tool removal uses **prefix-based matching** (no hardcoded name arrays) — `qt_*` = native, `cu_*` = CU, `chr_*` = chrome.

## Review Decisions

1. **Default mode → `"native"`** — keeps the tool list lean for Claude by default; users expand with `qtpilot_set_mode("all")`.
2. **Prefix-based tool removal** — the `qt_*`/`cu_*`/`chr_*` naming convention is reliable; no registry needed.
3. **`ServerState` class** — wrap `_mode`, `_mcp`, and related state in a class instead of module globals, for testability.
4. **Guard against duplicate registration** — before registering a mode's tools, check if tools with that prefix already exist and skip.
5. **`qt_modes` stays separate** from `qtpilot_set_mode` — probe-side vs. server-side are different concerns.
6. **`--ws-url` stays as a CLI flag** but is removed from `.mcp.json`. Supported as optional override.

## Files to Modify

1. **`python/src/qtpilot/cli.py`** — Make `--mode` optional, default to `"native"`
2. **`python/src/qtpilot/server.py`** — Add `ServerState` class, support `"all"` mode, add `set_mode()` with prefix-based tool removal and duplicate guard
3. **`python/src/qtpilot/tools/discovery_tools.py`** — Add `qtpilot_set_mode` tool
4. **`.mcp.json`** — Remove `--mode native` from args (keep `--ws-url` as supported CLI flag, just not in default config)

## Detailed Changes

### 1. `python/src/qtpilot/cli.py`

- Change `--mode` from `required=True` to `default="native"`
- Add `"all"` to `choices`: `choices=["native", "cu", "chrome", "all"]`

### 2. `python/src/qtpilot/server.py`

Add `ServerState` class to hold server state (replaces module globals):
```python
class ServerState:
    def __init__(self, mcp: FastMCP, mode: str = "native"):
        self.mcp = mcp
        self.mode = mode

_state: ServerState | None = None

def get_state() -> ServerState:
    if _state is None:
        raise RuntimeError("Server not initialized")
    return _state
```

Add prefix mapping and helper:
```python
_MODE_PREFIXES = {
    "native": ["qt_"],
    "cu": ["cu_"],
    "chrome": ["chr_"],
}

def _has_tools_with_prefix(mcp: FastMCP, prefixes: list[str]) -> bool:
    """Check if any tools with the given prefixes are already registered."""
    for tool in mcp.get_tools():
        if any(tool.name.startswith(p) for p in prefixes):
            return True
    return False

def _remove_tools_by_prefixes(mcp: FastMCP, prefixes: list[str]) -> None:
    """Remove all tools whose names match any of the given prefixes."""
    for tool in list(mcp.get_tools()):
        if any(tool.name.startswith(p) for p in prefixes):
            mcp.remove_tool(tool.name)
```

Add `set_mode()` method on `ServerState`:
```python
def set_mode(self, new_mode: str) -> dict:
    """Switch the active tool set. Returns previous and new mode."""
    valid = {"native", "cu", "chrome", "all"}
    if new_mode not in valid:
        return {"error": f"Invalid mode '{new_mode}'. Choose from: {', '.join(sorted(valid))}"}

    if new_mode == self.mode:
        return {"mode": new_mode, "changed": False}

    old_mode = self.mode

    # Determine which prefixes to remove (modes we're leaving)
    if old_mode == "all":
        # Remove everything except what the new mode needs
        prefixes_to_remove = []
        for mode_key, pfx in _MODE_PREFIXES.items():
            if mode_key != new_mode:
                prefixes_to_remove.extend(pfx)
    else:
        # Remove old mode's tools
        prefixes_to_remove = list(_MODE_PREFIXES.get(old_mode, []))

    _remove_tools_by_prefixes(self.mcp, prefixes_to_remove)

    # Register new mode's tools (with duplicate guard)
    if new_mode == "all":
        for mode_key in _MODE_PREFIXES:
            if mode_key != old_mode:  # old_mode's tools are already there
                _register_mode_tools_if_absent(self.mcp, mode_key)
    elif new_mode != old_mode:
        _register_mode_tools_if_absent(self.mcp, new_mode)

    self.mode = new_mode
    return {"mode": new_mode, "previous_mode": old_mode, "changed": True}
```

Add helper to register tools for a specific mode (with duplicate guard):
```python
def _register_mode_tools_if_absent(mcp: FastMCP, mode: str) -> None:
    """Register tools for a mode, skipping if tools with that prefix already exist."""
    prefixes = _MODE_PREFIXES.get(mode, [])
    if _has_tools_with_prefix(mcp, prefixes):
        return
    if mode == "native":
        from qtpilot.tools.native import register_native_tools
        register_native_tools(mcp)
    elif mode == "cu":
        from qtpilot.tools.cu import register_cu_tools
        register_cu_tools(mcp)
    elif mode == "chrome":
        from qtpilot.tools.chrome import register_chrome_tools
        register_chrome_tools(mcp)
```

Update `create_server()`:
- Create `ServerState` and store in `_state` after creation
- When `mode == "all"`: register native + cu + chrome tools
- When `mode == "native"` (default): register only native tools
- Recording tools always registered (only 3, useful across modes)

### 3. `python/src/qtpilot/tools/discovery_tools.py`

Add `qtpilot_set_mode` tool:
```python
@mcp.tool
async def qtpilot_set_mode(mode: str, ctx: Context) -> dict:
    """Switch the active API mode, changing which tools are available.

    Modes:
    - "native": Qt object introspection (qt_* tools)
    - "cu": Computer use / screenshot-based (cu_* tools)
    - "chrome": Accessibility tree (chr_* tools)
    - "all": All tools from all modes

    Args:
        mode: Target mode - "native", "cu", "chrome", or "all"

    Example: qtpilot_set_mode(mode="native")
    """
    from qtpilot.server import get_state

    state = get_state()
    result = state.set_mode(mode)
    if "error" not in result and result.get("changed", False):
        await ctx.send_tool_list_changed()
    return result
```

### 4. `.mcp.json`

```json
{
  "mcpServers": {
    "qtpilot": {
      "command": "qtpilot",
      "args": ["serve"]
    }
  }
}
```

Remove `--mode native` and `--ws-url` from default config. `--ws-url` remains a supported CLI flag for manual override.

## Key Design Decisions

- **Default mode is `"native"`** — keeps tool count manageable; users opt into broader modes via `qtpilot_set_mode`
- **`ServerState` class** — encapsulates server state for testability instead of module globals
- **Discovery + recording tools stay registered always** — they have `qtpilot_*` prefix and are mode-agnostic
- **Prefix-based removal** — no hardcoded tool name arrays; `qt_*`, `cu_*`, `chr_*` prefixes are intrinsic to the naming convention
- **Duplicate registration guard** — `_register_mode_tools_if_absent` checks for existing tools before registering, preventing dupes on repeated switches
- **`qt_modes` stays separate** — probe-side modes vs. server-side tool filtering are different concerns
- **`send_tool_list_changed()`** — notifies the MCP client (Claude) to refresh its tool list after a mode switch
- **Backward compatible** — `qtpilot serve --mode native` still works exactly as before; `--ws-url` still supported as CLI flag

## Verification

1. **Start server with no `--mode`** → only native tools registered (default)
2. **Start server with `--mode native`** → only native tools (backward compat)
3. **Start server with `--mode all`** → all tools registered
4. **Call `qtpilot_set_mode(mode="all")`** from "native" → cu/chrome tools added
5. **Call `qtpilot_set_mode(mode="native")`** from "all" → cu/chrome tools removed, native stays
6. **Call `qtpilot_set_mode(mode="all")` twice** → no duplicate tools registered
7. **Call `qtPilot_probe_status()`** → should reflect current mode
8. **Existing tests still pass** after changes
