# Phase 1: Foundation - Context

**Gathered:** 2025-01-29
**Status:** Ready for planning

<domain>
## Phase Boundary

DLL/LD_PRELOAD injection into any Qt application and WebSocket transport layer for JSON-RPC communication. The probe can be injected and accepts connections. Discovery, introspection, and interaction APIs are separate phases.

</domain>

<decisions>
## Implementation Decisions

### Launch experience
- CLI with flags: `qtpilot-launch.exe --port 9222 target.exe [args]`
- Verbose output by default: log injection steps, Qt detection, server startup
- Attached by default (launcher waits for target to exit), `--detach` flag for background mode
- Consistent CLI on both platforms: Linux uses same `qtpilot-launch` command (wraps LD_PRELOAD internally)

### Connection behavior
- Single client only: one WebSocket connection at a time
- Reject new connections while one is active (first client keeps control)
- Keep listening after client disconnects (probe stays active, ready for reconnection)
- Announce on startup: print "qtPilot listening on ws://..." to stderr, `--quiet` flag to suppress

### Configuration approach
- Default port: 9222 (same as Chrome DevTools Protocol)
- Default bind address: 0.0.0.0 (all interfaces)
- CLI flags only, no environment variable fallback
- All three modes (Native, Computer Use, Chrome) available simultaneously — client picks which API to use

### Claude's Discretion
- Exact verbose log format and levels
- Internal error handling and retry logic
- DLL/LD_PRELOAD implementation details
- WebSocket library choice

</decisions>

<specifics>
## Specific Ideas

- Port 9222 chosen for familiarity with Chrome DevTools Protocol users
- Same CLI UX on Windows and Linux for scripting portability

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope

</deferred>

---

*Phase: 01-foundation*
*Context gathered: 2025-01-29*
