---
phase: 02-core-introspection
plan: 07
subsystem: api
tags: [json-rpc, websocket, introspection, integration, qt]

# Dependency graph
requires:
  - phase: 02-02
    provides: "ObjectRegistry with findByObjectName, findAllByClassName, findById, objectId"
  - phase: 02-04
    provides: "MetaInspector with getProperty, setProperty, invokeMethod"
  - phase: 02-05
    provides: "SignalMonitor with subscribe, unsubscribe, lifecycle notifications"
  - phase: 02-06
    provides: "InputSimulator, Screenshot, HitTest for UI interaction"
provides:
  - "Complete JSON-RPC API for all introspection capabilities"
  - "21 JSON-RPC methods covering object discovery, properties, methods, signals, UI"
  - "Push notifications wired from SignalMonitor to WebSocket client"
  - "sendMessage() on WebSocketServer for outbound notifications"
  - "Integration tests proving end-to-end API functionality"
affects: [03-jsonrpc, 04-computer-use, 05-chrome]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "JSON-RPC method registration pattern with lambda handlers"
    - "Push notification pattern via SignalMonitor -> Probe -> WebSocket"

key-files:
  created:
    - "tests/test_jsonrpc_introspection.cpp"
  modified:
    - "src/probe/transport/jsonrpc_handler.cpp"
    - "src/probe/transport/websocket_server.h"
    - "src/probe/transport/websocket_server.cpp"
    - "src/probe/core/probe.cpp"
    - "tests/CMakeLists.txt"

key-decisions:
  - "Combined Tasks 1 and 2 into single commit (all method registrations are logically coupled)"
  - "Push notifications use JSON-RPC notification format (no id, method + params)"

patterns-established:
  - "JSON-RPC method handler pattern: parse params -> lookup object -> call API -> return JSON"
  - "Widget validation pattern: findById then qobject_cast<QWidget*> with error"

# Metrics
duration: 18min
completed: 2026-01-30
---

# Phase 2 Plan 7: JSON-RPC Introspection Integration Summary

**21 JSON-RPC methods wired for object discovery, properties, methods, signals, and UI interaction with push notification forwarding**

## Performance

- **Duration:** 18 min
- **Started:** 2026-01-30
- **Completed:** 2026-01-30
- **Tasks:** 3
- **Files modified:** 6

## Accomplishments
- Registered all 21 JSON-RPC methods covering the complete introspection API
- Wired SignalMonitor notifications (signalEmitted, objectCreated, objectDestroyed) to WebSocket push
- Added sendMessage() to WebSocketServer for outbound notification delivery
- 18 integration tests verifying complete API end-to-end

## Task Commits

Each task was committed atomically:

1. **Tasks 1+2: Register all JSON-RPC methods + wire notifications** - `e207a0c` (feat)
2. **Task 3: Integration tests for complete API** - `496179e` (test)

## Files Created/Modified
- `src/probe/transport/jsonrpc_handler.cpp` - All 21 JSON-RPC method registrations
- `src/probe/transport/websocket_server.h` - Added sendMessage() declaration
- `src/probe/transport/websocket_server.cpp` - Added sendMessage() implementation
- `src/probe/core/probe.cpp` - Wired SignalMonitor notifications to WebSocket
- `tests/test_jsonrpc_introspection.cpp` - 18 integration tests
- `tests/CMakeLists.txt` - Added test_jsonrpc_introspection target

## Decisions Made
- Combined Tasks 1 and 2 into a single commit since all method registrations and notification wiring are logically coupled
- Push notifications use standard JSON-RPC notification format: `{"jsonrpc":"2.0","method":"qtpilot.signalEmitted","params":{...}}`

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Added sendMessage() to WebSocketServer**
- **Found during:** Task 2 (notification wiring)
- **Issue:** WebSocketServer had no public method for sending unsolicited messages to the client
- **Fix:** Added `bool sendMessage(const QString& message)` method
- **Files modified:** websocket_server.h, websocket_server.cpp
- **Verification:** Build succeeds, notifications can be sent to client
- **Committed in:** e207a0c (Task 1+2 commit)

**2. [Rule 1 - Bug] Fixed subscription ID prefix in test**
- **Found during:** Task 3 (integration testing)
- **Issue:** Test expected `sub-` prefix but SignalMonitor uses `sub_` format
- **Fix:** Changed test assertion from `startsWith("sub-")` to `startsWith("sub_")`
- **Files modified:** tests/test_jsonrpc_introspection.cpp
- **Verification:** testSubscribeSignal passes
- **Committed in:** 496179e (Task 3 commit)

---

**Total deviations:** 2 auto-fixed (1 blocking, 1 bug)
**Impact on plan:** Both fixes necessary for correct operation. No scope creep.

## Issues Encountered
- Qt Test output not visible on Windows stderr when tests fail silently - used individual test execution with fprintf debugging to identify failures
- Object IDs computed at hook time (before objectName is set) - tests adapted to verify by object pointer equality rather than ID string content

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Phase 2 (Core Introspection) is now COMPLETE
- All 21 requirements (OBJ-01 through OBJ-11, SIG-01 through SIG-05, UI-01 through UI-05) are accessible via JSON-RPC
- Ready for Phase 3 (JSON-RPC refinement) or Phase 4+ as specified in roadmap
- All 7 test suites pass (42+ total test cases across the phase)

---
*Phase: 02-core-introspection*
*Completed: 2026-01-30*
