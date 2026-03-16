---
phase: 01-foundation
plan: 05
subsystem: launcher
tags: [dll-injection, ld_preload, createremotethread, qcommandlineparser, cli]

# Dependency graph
requires:
  - phase: 01-02
    provides: Probe singleton with platform-specific deferred initialization
  - phase: 01-04
    provides: WebSocket server for JSON-RPC communication
provides:
  - qtpilot-launch CLI tool with platform-consistent interface
  - Windows DLL injection via CreateRemoteThread
  - Linux LD_PRELOAD injection via fork/exec
  - Automatic probe initialization via Q_COREAPP_STARTUP_FUNCTION
affects: [02-native-api, user-workflows, integration-testing]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - CreateRemoteThread DLL injection (Windows)
    - LD_PRELOAD environment injection (Linux)
    - Q_COREAPP_STARTUP_FUNCTION for automatic DLL initialization
    - RAII HandleGuard for Windows handle cleanup

key-files:
  created:
    - src/launcher/injector.h
    - src/launcher/injector_windows.cpp
    - src/launcher/injector_linux.cpp
  modified:
    - src/launcher/main.cpp
    - src/launcher/CMakeLists.txt
    - CMakeLists.txt
    - src/probe/core/probe_init_windows.cpp
    - src/probe/core/probe_init_linux.cpp

key-decisions:
  - "QCommandLineParser for CLI instead of manual parsing"
  - "Q_COREAPP_STARTUP_FUNCTION for automatic probe initialization"
  - "Default port 9222 per CONTEXT.md (Chrome DevTools Protocol compatibility)"
  - "RAII HandleGuard class for automatic Windows handle cleanup"

patterns-established:
  - "Launcher CLI: qtpilot-launch --port X --detach --quiet target [args]"
  - "Windows injection: CREATE_SUSPENDED -> VirtualAllocEx -> WriteProcessMemory -> CreateRemoteThread -> ResumeThread"
  - "Linux injection: fork -> setenv LD_PRELOAD -> execvp"
  - "Auto-init: Q_COREAPP_STARTUP_FUNCTION triggers ensureInitialized() when Qt starts"

# Metrics
duration: 15min
completed: 2026-01-30
---

# Phase 1 Plan 5: Launcher CLI Summary

**Cross-platform launcher with CreateRemoteThread (Windows) and LD_PRELOAD (Linux) probe injection, auto-initialization via Q_COREAPP_STARTUP_FUNCTION**

## Performance

- **Duration:** 15 min
- **Started:** 2026-01-30T14:30:00Z
- **Completed:** 2026-01-30T14:45:00Z
- **Tasks:** 2
- **Files modified:** 9

## Accomplishments
- Launcher CLI with QCommandLineParser and --port, --detach, --quiet flags
- Windows DLL injection using CreateRemoteThread with proper error handling
- Linux LD_PRELOAD injection via fork/exec
- Automatic probe initialization when Qt application starts (no manual triggering needed)
- Port configuration passed via QTPILOT_PORT environment variable

## Task Commits

Each task was committed atomically:

1. **Task 1: Create launcher CLI with QCommandLineParser** - `d08783e` (feat)
2. **Task 2: Implement platform-specific injection** - `74285b0` (feat)

## Files Created/Modified
- `src/launcher/injector.h` - LaunchOptions struct and launchWithProbe declaration
- `src/launcher/injector_windows.cpp` - Windows CreateRemoteThread injection implementation
- `src/launcher/injector_linux.cpp` - Linux LD_PRELOAD injection implementation
- `src/launcher/main.cpp` - QCommandLineParser-based CLI with validation
- `src/launcher/CMakeLists.txt` - Updated for both platforms
- `CMakeLists.txt` - Enable launcher build on Windows and Linux
- `src/probe/core/probe_init_windows.cpp` - Added Q_COREAPP_STARTUP_FUNCTION
- `src/probe/core/probe_init_linux.cpp` - Added Q_COREAPP_STARTUP_FUNCTION

## Decisions Made
- **QCommandLineParser:** Used Qt's built-in parser instead of manual argument parsing. Provides automatic help generation, proper error messages, and handles quoting/escaping.
- **Q_COREAPP_STARTUP_FUNCTION:** Added automatic probe initialization hook. This function runs when QCoreApplication is constructed, eliminating the need for explicit ensureInitialized() calls.
- **RAII HandleGuard:** Created helper class for automatic Windows HANDLE cleanup. Prevents resource leaks on error paths.
- **Preserve existing LD_PRELOAD:** Linux injector prepends to existing LD_PRELOAD rather than replacing it.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing Critical] Added Q_COREAPP_STARTUP_FUNCTION for automatic initialization**
- **Found during:** Task 2 (Testing injection)
- **Issue:** Probe was injected but not initializing - deferred init required explicit trigger
- **Fix:** Added Q_COREAPP_STARTUP_FUNCTION in both platform init files to automatically call ensureInitialized() when Qt starts
- **Files modified:** src/probe/core/probe_init_windows.cpp, src/probe/core/probe_init_linux.cpp
- **Verification:** WebSocket server now listens automatically after injection
- **Committed in:** 74285b0 (Task 2 commit)

---

**Total deviations:** 1 auto-fixed (missing critical)
**Impact on plan:** Essential for automatic injection without application modification. No scope creep.

## Issues Encountered
- Windows process killed via PowerShell required extra delay before DLL could be rebuilt (file locking)
- PowerShell escaping for WebSocket testing was complex; used simpler connectivity test

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- Launcher and injection complete for both Windows and Linux
- WebSocket server auto-starts when Qt application runs
- Ready for Object Registry (01-06) to add object tracking
- Full end-to-end testing: `qtpilot-launch target.exe` -> connect to `ws://localhost:9222` -> send JSON-RPC

---
*Phase: 01-foundation*
*Completed: 2026-01-30*
