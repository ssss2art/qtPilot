---
phase: 01-foundation
plan: 02
subsystem: infra
tags: [dll-injection, ld-preload, singleton, deferred-init, windows-dll, linux-constructor]

# Dependency graph
requires:
  - phase: 01-01
    provides: CMake build system, Qt5/Qt6 dual support, probe DLL target
provides:
  - Probe singleton accessible via Probe::instance()
  - Safe Windows DllMain with deferred initialization (InitOnce API)
  - Safe Linux constructor with QTimer deferred initialization
  - ensureInitialized() function for triggering deferred init
affects: [01-03, 01-04, 01-05, 01-06]

# Tech tracking
tech-stack:
  added: [windows-initonce-api, gcc-constructor-attribute]
  patterns: [deferred-initialization, platform-specific-compilation]

key-files:
  created:
    - src/probe/core/probe_init_windows.cpp
    - src/probe/core/probe_init_linux.cpp
  modified:
    - src/probe/core/probe.h
    - src/probe/CMakeLists.txt

key-decisions:
  - "Use InitOnce API instead of std::call_once on Windows - std::call_once uses TLS internally on MSVC which breaks for dynamically loaded DLLs"
  - "DllMain only sets flags and calls DisableThreadLibraryCalls - all real work deferred to ensureInitialized()"
  - "Linux constructor uses QTimer::singleShot(0) for deferred init when Qt exists, or sets flag for later"

patterns-established:
  - "Deferred initialization: Never call Qt functions from DllMain or LD_PRELOAD constructor"
  - "Platform-specific compilation: if(WIN32) / elseif(UNIX AND NOT APPLE) pattern in CMakeLists.txt"
  - "ensureInitialized() pattern: Safe to call from any code path, triggers one-time init via platform API"

# Metrics
duration: 4min
completed: 2026-01-30
---

# Phase 01 Plan 02: Probe Singleton with Platform Initialization Summary

**Safe platform-specific DLL/library entry points with deferred initialization using Windows InitOnce API and Linux constructor attributes**

## Performance

- **Duration:** 4 min
- **Started:** 2026-01-30T13:27:54Z
- **Completed:** 2026-01-30T13:31:19Z
- **Tasks:** 2
- **Files modified:** 4

## Accomplishments

- Windows DllMain follows Microsoft DLL best practices: only DisableThreadLibraryCalls + flag set
- Linux constructor detects Qt availability and defers initialization via QTimer::singleShot
- InitOnce API used for thread-safe one-time initialization (avoids TLS-based std::call_once)
- No forbidden constructs (thread_local, __declspec(thread), std::call_once) in probe code
- Build produces qtPilot-probe.dll with correct entry points

## Task Commits

Each task was committed atomically:

1. **Task 1: Create Probe singleton class** - Already existed from prior work (probe.h/probe.cpp with Q_GLOBAL_STATIC)
2. **Task 2: Implement platform-specific entry points** - `591036d` (feat)

Note: Task 1 was satisfied by existing implementation. The Probe singleton with Q_GLOBAL_STATIC pattern was already in place.

## Files Created/Modified

- `src/probe/core/probe_init_windows.cpp` - Windows DllMain with InitOnce deferred initialization
- `src/probe/core/probe_init_linux.cpp` - Linux constructor with QTimer deferred initialization
- `src/probe/core/probe.h` - Added ensureInitialized() function declaration
- `src/probe/CMakeLists.txt` - Conditional platform-specific source file inclusion

## Decisions Made

1. **Use Windows InitOnce API instead of std::call_once** - MSVC's std::call_once implementation uses TLS internally, which causes corruption when used in dynamically loaded DLLs
2. **Minimal DllMain** - Only DisableThreadLibraryCalls and flag set; no Qt calls, no thread creation, no LoadLibrary
3. **Environment variable for disable** - QTPILOT_ENABLED=0 disables probe initialization on Linux
4. **Conditional cleanup on process termination** - Only call shutdown() when reserved==nullptr in DLL_PROCESS_DETACH (process terminating vs normal unload)

## Deviations from Plan

None - plan executed as written. The probe singleton class (Task 1) was already implemented in the codebase, so only Task 2 required new work.

## Issues Encountered

None - the implementation followed the patterns from 01-RESEARCH.md exactly.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Probe singleton with safe platform initialization complete
- WebSocket server can now be implemented in 01-04 (or later plan)
- ensureInitialized() available for triggering deferred init from any code path
- Ready for JSON-RPC handler integration (01-03 was completed before this plan)

---
*Phase: 01-foundation*
*Completed: 2026-01-30*
