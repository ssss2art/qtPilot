---
phase: 07-python-integration
plan: 02
subsystem: python-tools
tags: [fastmcp, tools, json-rpc, async, python]
dependency-graph:
  requires: ["07-01"]
  provides: ["complete MCP tool surface for all 3 modes"]
  affects: ["07-03"]
tech-stack:
  added: []
  patterns: ["thin async bridge", "@mcp.tool decorator", "deferred import for get_probe()"]
key-files:
  created: []
  modified:
    - python/src/qtpilot/tools/native.py
    - python/src/qtpilot/tools/cu.py
    - python/src/qtpilot/tools/chrome.py
decisions:
  - decision: "Deferred import of get_probe() inside each tool function"
    rationale: "Avoids circular import since tools module is imported by server module"
  - decision: "ctx parameter uses Context type from fastmcp"
    rationale: "Matches FastMCP convention; required for tool registration even if unused"
metrics:
  duration: "4 min"
  completed: "2026-02-01"
---

# Phase 07 Plan 02: Tool Definitions Summary

**53 async MCP tool functions across 3 mode files, each a thin bridge to probe JSON-RPC methods**

## What Was Done

### Task 1: Native mode tools (32 qt_* functions)
- **Commit:** ee8fd66
- Replaced stub in `native.py` with `register_native_tools(mcp)` containing 32 `@mcp.tool` async functions
- Coverage: ping, version, modes, objects (find/findByClass/tree/info/inspect/query), properties (list/get/set), methods (list/invoke), signals (list/subscribe/unsubscribe/setLifecycle), UI (click/sendKeys/screenshot/geometry/hitTest), names (register/unregister/list/validate/load), QML (inspect), models (list/info/data)
- All optional params use `None` defaults and are excluded from JSON-RPC params dict when not provided

### Task 2: Computer Use + Chrome mode tools (21 functions)
- **Commit:** dc39a4b
- `cu.py`: 13 cu_* tools -- screenshot, leftClick, rightClick, middleClick, doubleClick, mouseMove, mouseDrag, mouseDown, mouseUp, type, key, scroll, cursorPosition
- `chrome.py`: 8 chr_* tools -- readPage, click, formInput, getPageText, find, navigate, tabsContext, readConsoleMessages
- Same thin bridge pattern: async, get_probe(), call(), return result

## Deviations from Plan

None -- plan executed exactly as written.

## Decisions Made

| Decision | Rationale |
|----------|-----------|
| Deferred import of get_probe() inside each tool | Avoids circular imports since server.py imports tool modules |
| ctx: Context parameter on every tool | Required by FastMCP for tool registration |

## Verification Results

- All three files import without errors
- Function counts verified: 32 native + 13 cu + 8 chrome = 53 tools
- Every tool calls `get_probe().call()` with correct JSON-RPC method prefix
- No print() statements in any tool file
- Docstrings are minimal (1 sentence + 1 example)

## Next Phase Readiness

Plan 07-03 (CLI + packaging) can proceed. All tool functions are registered and ready for end-to-end use once a probe is connected.
