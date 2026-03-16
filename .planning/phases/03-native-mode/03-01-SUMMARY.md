---
phase: 03-native-mode
plan: 01
subsystem: api
tags: [error-codes, response-envelope, symbolic-names, object-resolver, json-rpc]

# Dependency graph
requires:
  - phase: 02-core-introspection
    provides: ObjectRegistry with findById(), object hooks, signal monitoring
provides:
  - ErrorCodes constants for application-specific JSON-RPC errors
  - ResponseEnvelope for uniform {result, meta} response wrapping
  - SymbolicNameMap for Squish-style name-to-path aliases
  - ObjectResolver for multi-style ID resolution (numeric, symbolic, path)
affects: [03-02 NativeModeApi, 03-03 testing]

# Tech tracking
tech-stack:
  added: []
  patterns: [response-envelope-pattern, multi-style-id-resolution, symbolic-name-aliases]

key-files:
  created:
    - src/probe/api/error_codes.h
    - src/probe/api/response_envelope.h
    - src/probe/api/response_envelope.cpp
    - src/probe/api/symbolic_name_map.h
    - src/probe/api/symbolic_name_map.cpp
    - src/probe/core/object_resolver.h
    - src/probe/core/object_resolver.cpp
  modified:
    - src/probe/CMakeLists.txt

key-decisions:
  - "QStringView::mid() instead of deprecated midRef() for Qt6 compatibility"
  - "Q_GLOBAL_STATIC singleton for SymbolicNameMap (same pattern as ObjectRegistry)"
  - "Auto-load name map from QTPILOT_NAME_MAP env var or qtPilot-names.json in CWD"

patterns-established:
  - "ResponseEnvelope::wrap() pattern: all qt.* methods return {result, meta{timestamp}}"
  - "ObjectResolver resolution order: numeric -> symbolic -> hierarchical path"
  - "ErrorCode namespace with constexpr int constants in -32001 to -32052 range"

# Metrics
duration: 5min
completed: 2026-01-31
---

# Phase 3 Plan 1: Infrastructure Classes Summary

**ErrorCodes, ResponseEnvelope, SymbolicNameMap, and ObjectResolver providing error constants, uniform response wrapping, name aliases, and multi-style ID resolution**

## Performance

- **Duration:** 5 min
- **Started:** 2026-01-31T18:42:51Z
- **Completed:** 2026-01-31T18:48:11Z
- **Tasks:** 2
- **Files modified:** 8

## Accomplishments
- ErrorCodes defines 15 application-specific error codes across 6 categories (object, property, method, signal, UI, name map)
- ResponseEnvelope provides wrap() for uniform {result, meta{timestamp}} responses and createError()/createValidationError() helpers
- SymbolicNameMap manages Squish-style name-to-path aliases with thread-safe singleton, JSON file I/O, and validation
- ObjectResolver resolves numeric (#N), symbolic, and hierarchical path IDs to QObject* with QPointer stale detection

## Task Commits

Each task was committed atomically:

1. **Task 1: Create ErrorCodes and ResponseEnvelope** - `aad4750` (feat)
2. **Task 2: Create SymbolicNameMap and ObjectResolver** - `52c6676` (feat)

## Files Created/Modified
- `src/probe/api/error_codes.h` - Application-specific JSON-RPC error code constants
- `src/probe/api/response_envelope.h` - Uniform response envelope wrapper class declaration
- `src/probe/api/response_envelope.cpp` - ResponseEnvelope wrap() and error helper implementations
- `src/probe/api/symbolic_name_map.h` - Thread-safe symbolic name alias manager declaration
- `src/probe/api/symbolic_name_map.cpp` - SymbolicNameMap with JSON I/O and validation
- `src/probe/core/object_resolver.h` - Multi-style object ID resolver declaration
- `src/probe/core/object_resolver.cpp` - ObjectResolver with numeric, symbolic, and path resolution
- `src/probe/CMakeLists.txt` - Added 4 new source files and 3 new headers

## Decisions Made
- Used QStringView::mid() instead of deprecated QString::midRef() for Qt6 compatibility
- Q_GLOBAL_STATIC singleton for SymbolicNameMap (consistent with ObjectRegistry, SignalMonitor)
- Auto-load name map from QTPILOT_NAME_MAP env var, falling back to qtPilot-names.json in CWD

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed Qt6 deprecated midRef() API**
- **Found during:** Task 2 (ObjectResolver implementation)
- **Issue:** QString::midRef() was removed in Qt6, causing compilation error C2039
- **Fix:** Changed to QStringView(id).mid(1).toInt() which works in both Qt5 and Qt6
- **Files modified:** src/probe/core/object_resolver.cpp
- **Verification:** Build succeeds, all 7 tests pass
- **Committed in:** 52c6676 (Task 2 commit)

---

**Total deviations:** 1 auto-fixed (1 bug)
**Impact on plan:** Minor Qt6 API compatibility fix. No scope creep.

## Issues Encountered
- DLL file locking during rebuild (transient - resolved on retry). Known Windows behavior documented in STATE.md.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- All 4 infrastructure classes ready for Plan 03-02 (NativeModeApi)
- ErrorCodes provides all error constants qt.* methods will throw
- ResponseEnvelope provides the wrap() pattern for uniform responses
- SymbolicNameMap and ObjectResolver provide flexible object lookup
- All 7 existing tests continue to pass

---
*Phase: 03-native-mode*
*Completed: 2026-01-31*
