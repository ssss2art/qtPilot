# Phase 7: Python Integration - Context

**Gathered:** 2026-02-01
**Status:** Ready for planning

<domain>
## Phase Boundary

Python MCP server that bridges Claude to the qtPilot probe via WebSocket, exposing all three API modes (Native, Computer Use, Chrome) as MCP tools. No standalone Python client library — MCP servers are the only Python deliverable.

</domain>

<decisions>
## Implementation Decisions

### Server Architecture
- 3 logical server modes, delivered as a single `qtpilot` package with `--mode native|cu|chrome` flag
- Each mode exposes only its own tools: native (~33 qt.* tools), cu (13 cu.* tools), chrome (8 chr.* tools)
- One server connects to one probe at a time (exclusive connection, matching probe's single-client WebSocket)
- User configures one qtPilot server entry in Claude config, mode passed as server arg

### Connection Lifecycle
- Support both auto-launch (pass target exe path) and connect-to-running (pass ws:// URL)
- CLI args take priority, environment variables (QTPILOT_TARGET, QTPILOT_PORT, etc.) as fallback
- Auto-launch: MCP server runs qtpilot-launch internally, starts target app + probe, then connects
- On disconnect: retry with exponential backoff (3 attempts), tools return errors during downtime

### MCP SDK & Features
- Built on FastMCP framework (decorator-based, less boilerplate)
- Tools only — no MCP prompts or templates
- Plus one status resource endpoint (probe connection state, target app info)
- Tool descriptions: minimal + 1-2 usage examples per tool. Not verbose param docs.

### Documentation
- README includes copy-paste Claude config snippets for each mode
- Examples for Claude Desktop, Claude Code, and other MCP hosts
- Per-mode config showing both auto-launch and connect-to-running patterns

### Claude's Discretion
- Internal WebSocket client implementation details
- Error message formatting and error code mapping
- Tool parameter naming (can follow existing JSON-RPC param names or simplify)
- Retry timing and backoff intervals
- Status resource schema

</decisions>

<specifics>
## Specific Ideas

- "3 separate MCP servers makes more sense" — confirmed by MCP ecosystem research: one server per domain is the recommended pattern, avoids tool count bloat
- Single package with --mode flag preferred over 3 separate package names for simpler user config
- Probe is already single-client WebSocket, so exclusive connection is natural — no contention
- FastMCP chosen for rapid development with decorator syntax

</specifics>

<deferred>
## Deferred Ideas

- Standalone Python client library for developer scripting — could be a future phase if demand exists
- MCP gateway/proxy for multi-app orchestration — out of scope
- Runtime mode switching without restarting server — not needed with config-based approach

</deferred>

---

*Phase: 07-python-integration*
*Context gathered: 2026-02-01*
