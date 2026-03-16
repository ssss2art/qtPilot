---
phase: 03-native-mode
plan: 02
subsystem: api
tags: [json-rpc, native-mode, qt-introspection, object-resolver, response-envelope]

# Dependency graph
requires:
  - phase: 03-01
    provides: "ErrorCodes, ResponseEnvelope, ObjectResolver, SymbolicNameMap"
  - phase: 02-core-introspection
    provides: "All introspection components (MetaInspector, SignalMonitor, InputSimulator, Screenshot, HitTest)"
provides:
  - "NativeModeApi class registering 29 qt.* JSON-RPC methods"
  - "JsonRpcException for structured error responses with code + data"
  - "Auto-loading symbolic name map on probe init"
  - "Numeric ID clearing on client disconnect"
affects: [03-native-mode, 04-testing, 05-chrome-mode]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "NativeModeApi pattern: separate class registers methods on JsonRpcHandler"
    - "JsonRpcException for structured error responses (code + message + data)"
    - "resolveObjectParam/resolveWidgetParam helpers for consistent ID resolution"
    - "envelopeToString helper for compact JSON serialization"

key-files:
  created:
    - src/probe/api/native_mode_api.h
    - src/probe/api/native_mode_api.cpp
  modified:
    - src/probe/transport/jsonrpc_handler.h
    - src/probe/transport/jsonrpc_handler.cpp
    - src/probe/core/probe.cpp
    - src/probe/CMakeLists.txt

key-decisions:
  - "JsonRpcException added to jsonrpc_handler.h in Task 1 (blocking dependency for NativeModeApi)"
  - "CreateErrorResponse overload with QJsonObject data for structured errors"
  - "Numeric IDs cleared on client disconnect via WebSocketServer::clientDisconnected signal"

patterns-established:
  - "NativeModeApi registration: constructor registers all methods, no further calls needed"
  - "Internal helper functions in anonymous namespace (resolveObjectParam, resolveWidgetParam, parseParams, envelopeToString)"
  - "All qt.* methods use ObjectResolver::resolve() not ObjectRegistry::findById() directly"

# Metrics
duration: 10min
completed: 2026-01-31
---

# Phase 3 Plan 02: NativeModeApi Summary

**29 qt.* JSON-RPC methods across 7 domains with structured error responses, ObjectResolver integration, and ResponseEnvelope wrapping**

## Performance

- **Duration:** 10 min
- **Started:** 2026-01-31T18:52:26Z
- **Completed:** 2026-01-31T19:02:17Z
- **Tasks:** 2
- **Files modified:** 6

## Accomplishments
- Registered 29 qt.* namespaced methods across 7 domains (objects, properties, methods, signals, ui, names, system)
- Every method uses ObjectResolver for ID resolution, ResponseEnvelope for wrapping, ErrorCode for structured errors
- JsonRpcException enables structured JSON-RPC error responses with code, message, and optional data field
- NativeModeApi auto-instantiated in Probe::initialize(), name map auto-loaded from env/file
- Old qtpilot.* methods preserved for backward compatibility
- All 7 existing tests pass

## Task Commits

Each task was committed atomically:

1. **Task 1: Create NativeModeApi class with all qt.* method registrations** - `eeb3a7c` (feat)
2. **Task 2: Wire NativeModeApi into Probe initialization** - `e3ab2f2` (feat)

## Files Created/Modified
- `src/probe/api/native_mode_api.h` - NativeModeApi class declaration with 7 registration method groups
- `src/probe/api/native_mode_api.cpp` - 29 method registrations with internal helpers (530+ lines)
- `src/probe/transport/jsonrpc_handler.h` - JsonRpcException class, CreateErrorResponse overload with data
- `src/probe/transport/jsonrpc_handler.cpp` - Catch JsonRpcException before std::exception, CreateErrorResponse with QJsonObject data
- `src/probe/core/probe.cpp` - NativeModeApi instantiation, name map auto-load, numeric ID clearing on disconnect
- `src/probe/CMakeLists.txt` - Added native_mode_api.h/.cpp to build

## Decisions Made
- Added JsonRpcException to jsonrpc_handler.h during Task 1 (was planned for Task 2) because it was a compile-time blocking dependency for NativeModeApi -- tracked as Rule 3 deviation
- CreateErrorResponse overload uses QJsonDocument for proper JSON construction (not string formatting) to support nested data objects
- Numeric IDs cleared on client disconnect using existing WebSocketServer::clientDisconnected signal

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Added JsonRpcException to jsonrpc_handler.h during Task 1**
- **Found during:** Task 1 (NativeModeApi compilation)
- **Issue:** NativeModeApi throws JsonRpcException, but the class was planned for Task 2. Task 1 cannot compile without it.
- **Fix:** Added JsonRpcException class and QJsonObject include to jsonrpc_handler.h as part of Task 1 commit
- **Files modified:** src/probe/transport/jsonrpc_handler.h
- **Verification:** Build succeeds
- **Committed in:** eeb3a7c (Task 1 commit)

---

**Total deviations:** 1 auto-fixed (1 blocking)
**Impact on plan:** Moved one definition earlier than planned to unblock compilation. No scope creep.

## Issues Encountered
None

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- All 29 qt.* methods registered and wired into Probe initialization
- Ready for Phase 3 Plan 03 (Testing) to validate all method registrations
- Old qtpilot.* backward compatibility preserved for gradual migration

---
*Phase: 03-native-mode*
*Completed: 2026-01-31*
