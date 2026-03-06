# Dynamic Mode Switching for QtMCP Server

## Context

The probe runs in "all" mode by default (all three API sets registered), but the MCP server CLI requires `--mode` to be one of `native`, `cu`, or `chrome`. This forces the user to hardcode a mode in `.mcp.json` at startup, with no way to change it at runtime. The user wants to either have all tools available by default, or be able to switch modes dynamically via a tool call.

## Approach

Make `--mode` optional (default: "all" = register everything), and add a `qtmcp_set_mode` tool that dynamically adds/removes mode-specific tools at runtime using FastMCP's `add_tool`/`remove_tool` + `send_tool_list_changed()`. Tool removal uses **prefix-based matching** (no hardcoded name arrays) — `qt_*` = native, `cu_*` = CU, `chr_*` = chrome.

## Files to Modify

1. **`python/src/qtmcp/cli.py`** — Make `--mode` optional, default to `"all"`
2. **`python/src/qtmcp/server.py`** — Support `"all"` mode, expose `_mcp` instance, add `set_mode()` with prefix-based tool removal
3. **`python/src/qtmcp/tools/discovery_tools.py`** — Add `qtmcp_set_mode` tool
4. **`.mcp.json`** — Remove `--mode native` from args

## Detailed Changes

### 1. `python/src/qtmcp/cli.py`

- Change `--mode` from `required=True` to `default="all"`
- Add `"all"` to `choices`: `choices=["native", "cu", "chrome", "all"]`

### 2. `python/src/qtmcp/server.py`

Add module-level state:
```python
_mcp: FastMCP | None = None
```

Add accessor:
```python
def get_mcp() -> FastMCP | None:
    return _mcp
```

Add prefix mapping and helper:
```python
_MODE_PREFIXES = {
    "native": ["qt_"],
    "cu": ["cu_"],
    "chrome": ["chr_"],
}

def _remove_tools_by_prefixes(mcp: FastMCP, prefixes: list[str]) -> None:
    """Remove all tools whose names match any of the given prefixes."""
    for tool in list(mcp.get_tools()):
        if any(tool.name.startswith(p) for p in prefixes):
            mcp.remove_tool(tool.name)
```

Add `set_mode()` function:
```python
def set_mode(new_mode: str) -> dict:
    """Switch the active tool set. Returns previous and new mode."""
    global _mode
    valid = {"native", "cu", "chrome", "all"}
    if new_mode not in valid:
        return {"error": f"Invalid mode '{new_mode}'. Choose from: {', '.join(sorted(valid))}"}

    old_mode = _mode
    if new_mode == old_mode:
        return {"mode": new_mode, "changed": False}

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

    _remove_tools_by_prefixes(_mcp, prefixes_to_remove)

    # Register new mode's tools
    if new_mode == "all":
        # Add back anything that's missing
        for mode_key in _MODE_PREFIXES:
            if mode_key != old_mode:  # old_mode's tools are already there
                _register_mode_tools(_mcp, mode_key)
    elif new_mode != old_mode:
        _register_mode_tools(_mcp, new_mode)

    _mode = new_mode
    return {"mode": new_mode, "previous_mode": old_mode, "changed": True}
```

Add helper to register tools for a specific mode:
```python
def _register_mode_tools(mcp: FastMCP, mode: str) -> None:
    if mode == "native":
        from qtmcp.tools.native import register_native_tools
        register_native_tools(mcp)
    elif mode == "cu":
        from qtmcp.tools.cu import register_cu_tools
        register_cu_tools(mcp)
    elif mode == "chrome":
        from qtmcp.tools.chrome import register_chrome_tools
        register_chrome_tools(mcp)
```

Update `create_server()`:
- Store `mcp` in `_mcp` after creation
- When `mode == "all"`: register native + cu + chrome tools
- Recording tools always registered (only 3, useful across modes)

### 3. `python/src/qtmcp/tools/discovery_tools.py`

Add `qtmcp_set_mode` tool:
```python
@mcp.tool
async def qtmcp_set_mode(mode: str, ctx: Context) -> dict:
    """Switch the active API mode, changing which tools are available.

    Modes:
    - "native": Qt object introspection (qt_* tools)
    - "cu": Computer use / screenshot-based (cu_* tools)
    - "chrome": Accessibility tree (chr_* tools)
    - "all": All tools from all modes (default)

    Args:
        mode: Target mode - "native", "cu", "chrome", or "all"

    Example: qtmcp_set_mode(mode="native")
    """
    from qtmcp.server import set_mode

    result = set_mode(mode)
    if "error" not in result and result.get("changed", False):
        await ctx.send_tool_list_changed()
    return result
```

### 4. `.mcp.json`

```json
{
  "mcpServers": {
    "qtmcp": {
      "command": "qtmcp",
      "args": ["serve"]
    }
  }
}
```

Remove `--mode native` and `--ws-url` (discovery handles connection).

## Key Design Decisions

- **Discovery + recording tools stay registered always** — they have `qtmcp_*` prefix and are mode-agnostic
- **Prefix-based removal** — no hardcoded tool name arrays; `qt_*`, `cu_*`, `chr_*` prefixes are intrinsic to the naming convention
- **`send_tool_list_changed()`** — notifies the MCP client (Claude) to refresh its tool list after a mode switch
- **Backward compatible** — `qtmcp serve --mode native` still works exactly as before

## Verification

1. **Start server with no `--mode`** → all tools should be registered
2. **Start server with `--mode native`** → only native tools (backward compat)
3. **Call `qtmcp_set_mode(mode="native")`** from "all" → cu/chrome tools removed, native stays
4. **Call `qtmcp_set_mode(mode="all")`** → all tools restored
5. **Call `qtmcp_probe_status()`** → should reflect current mode
6. **Existing tests still pass** after changes
