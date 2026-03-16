---
phase: 01-foundation
plan: 03
subsystem: transport
tags: [json-rpc, qt, websocket, testing, qtest]

# Dependency graph
requires:
  - phase: 01-01
    provides: CMake build system with Qt5/Qt6 support
provides:
  - JSON-RPC 2.0 message handler with Qt-native JSON parsing
  - Qt Test framework test suite for JsonRpcHandler
  - Built-in methods: ping, getVersion, getModes, echo, qtpilot.echo
  - Notification signal for JSON-RPC notifications
affects: [01-04, 01-05, websocket-integration]

# Tech tracking
tech-stack:
  added: [QTest framework]
  patterns: [JSON-RPC 2.0 error codes, QTPILOT_EXPORT DLL macro]

key-files:
  created:
    - tests/test_jsonrpc.cpp
  modified:
    - src/probe/transport/jsonrpc_handler.h
    - src/probe/transport/jsonrpc_handler.cpp
    - src/probe/core/probe.h
    - src/probe/core/injector_windows.cpp
    - tests/CMakeLists.txt
    - CMakeLists.txt

key-decisions:
  - "Use Qt Test (QTest) instead of GTest to eliminate external dependencies"
  - "Add QTPILOT_EXPORT macro for proper DLL symbol export on Windows"

patterns-established:
  - "QTPILOT_EXPORT: Use dllexport/dllimport macro for QObject classes in DLL"
  - "JSON-RPC 2.0 error codes: -32700 parse, -32600 invalid, -32601 method not found"
  - "NotificationReceived signal: Emit when JSON-RPC notification (no id) arrives"

# Metrics
duration: 17min
completed: 2026-01-30
---

# Phase 1 Plan 3: JSON-RPC Handler Summary

**JSON-RPC 2.0 handler with Qt-native parsing, notification signals, and Qt Test suite proving all error codes**

## Performance

- **Duration:** 17 min
- **Started:** 2026-01-30T04:48:29Z
- **Completed:** 2026-01-30T05:05:10Z
- **Tasks:** 2
- **Files modified:** 7

## Accomplishments
- Added NotificationReceived signal for incoming JSON-RPC notifications
- Added qtpilot.echo method for integration testing (per RESEARCH.md spec)
- Rewrote test suite using Qt Test framework (no GTest dependency)
- All 13 JSON-RPC test cases pass

## Task Commits

Each task was committed atomically:

1. **Task 1: Create/enhance JsonRpcHandler class** - `c780873` (feat)
2. **Bug fix: Q_GLOBAL_STATIC and method names** - `1efa899` (fix)
3. **Task 2: Update CMakeLists and add Qt Test** - `4c2f9d1` (test)

## Files Created/Modified
- `src/probe/transport/jsonrpc_handler.h` - Added NotificationReceived signal, QTPILOT_EXPORT macro
- `src/probe/transport/jsonrpc_handler.cpp` - Added qtpilot.echo method, emit notification signal
- `src/probe/core/probe.h` - Made constructor/destructor public for Q_GLOBAL_STATIC
- `src/probe/core/injector_windows.cpp` - Fixed method name casing (Instance->instance, etc.)
- `tests/CMakeLists.txt` - Rewritten for Qt Test, Qt5/Qt6 support
- `tests/test_jsonrpc.cpp` - Rewritten using QTest macros
- `CMakeLists.txt` - Removed GTest requirement for tests

## Decisions Made
- Used Qt Test (QTest) instead of GTest to eliminate external dependency
- Added QTPILOT_EXPORT macro to JsonRpcHandler for proper Windows DLL symbol export
- Made Probe constructor/destructor public for Q_GLOBAL_STATIC compatibility (Qt6 requirement)

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Fixed Q_GLOBAL_STATIC access in probe.cpp**
- **Found during:** Task 1 (build verification)
- **Issue:** Q_GLOBAL_STATIC(Probe, ...) couldn't access private constructor/destructor
- **Fix:** Made constructor/destructor public with documentation note to use instance()
- **Files modified:** src/probe/core/probe.h
- **Verification:** Build succeeds, singleton still works
- **Committed in:** 1efa899

**2. [Rule 3 - Blocking] Fixed method name casing in injector_windows.cpp**
- **Found during:** Task 1 (build verification)
- **Issue:** Used Instance(), Initialize(), IsRunning(), Shutdown() but Probe uses lowercase
- **Fix:** Changed to instance(), initialize(), isRunning(), shutdown()
- **Files modified:** src/probe/core/injector_windows.cpp
- **Verification:** Build succeeds
- **Committed in:** 1efa899

---

**Total deviations:** 2 auto-fixed (2 blocking)
**Impact on plan:** Both auto-fixes necessary for build to succeed. No scope creep.

## Issues Encountered
- Qt Test executable couldn't find DLLs in PATH during ctest run (test_jsonrpc.exe runs fine manually with Qt in PATH)
- QTPILOT_EXPORT was missing from JsonRpcHandler causing linker errors for MOC-generated symbols

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- JsonRpcHandler complete and tested
- Ready for WebSocket server integration (01-04)
- Tests prove all JSON-RPC 2.0 error codes work correctly

---
*Phase: 01-foundation*
*Completed: 2026-01-30*
