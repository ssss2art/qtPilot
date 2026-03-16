---
phase: 01-foundation
plan: 01
subsystem: infra
tags: [cmake, qt, vcpkg, build-system]

# Dependency graph
requires: []
provides:
  - CMake build system with Qt5/Qt6 dual support
  - vcpkg manifest for dependency management
  - Probe shared library target (qtPilot_probe)
  - Launcher executable target (qtPilot_launcher)
affects: [01-02, 01-03, 01-04, 01-05, 01-06]

# Tech tracking
tech-stack:
  added: [cmake, vcpkg, qt6]
  patterns: [qt-version-abstraction, conditional-dependencies]

key-files:
  created: []
  modified:
    - CMakeLists.txt
    - CMakePresets.json
    - vcpkg.json
    - src/probe/CMakeLists.txt
    - src/launcher/CMakeLists.txt
    - test_app/CMakeLists.txt
    - src/probe/core/probe.cpp
    - src/probe/core/injector_windows.cpp
    - src/probe/transport/websocket_server.cpp
    - src/probe/transport/jsonrpc_handler.cpp

key-decisions:
  - "Qt6 preferred, Qt5 5.15+ fallback - using QT_VERSION_MAJOR for conditional linking"
  - "nlohmann_json and spdlog optional - using QJsonDocument and QDebug as fallbacks"
  - "WINDOWS_EXPORT_ALL_SYMBOLS for probe DLL - simplifies symbol export during development"

patterns-established:
  - "Qt version abstraction: if(QT_VERSION_MAJOR EQUAL 6) pattern for CMake"
  - "Conditional compilation: QTPILOT_HAS_SPDLOG/QTPILOT_HAS_NLOHMANN_JSON defines"
  - "Logging abstraction: LOG_INFO/LOG_WARN/LOG_ERROR macros with spdlog/QDebug backends"

# Metrics
duration: 16min
completed: 2025-01-29
---

# Phase 01 Plan 01: Build System Setup Summary

**CMake build system with Qt5/Qt6 dual support, vcpkg manifest, and conditional optional dependencies**

## Performance

- **Duration:** 16 min
- **Started:** 2026-01-30T04:27:27Z
- **Completed:** 2026-01-30T04:43:15Z
- **Tasks:** 2
- **Files modified:** 10

## Accomplishments

- Root CMakeLists.txt now supports both Qt5 (5.15+) and Qt6 with automatic detection
- vcpkg.json updated with valid baseline (3bdaa9b...) and Qt dependencies
- Optional dependencies (nlohmann_json, spdlog, gtest) gracefully fallback to Qt alternatives
- All three targets build successfully: probe DLL, launcher executable, test app

## Task Commits

Each task was committed atomically:

1. **Task 1: Create root CMakeLists.txt and CMakePresets.json** - `0240390` (feat)
2. **Task 2: Create vcpkg.json manifest and probe/launcher CMakeLists** - `2f13642` (feat)

## Files Created/Modified

- `CMakeLists.txt` - Root build config with Qt5/Qt6 detection, optional deps handling
- `CMakePresets.json` - Already existed, unchanged (good presets for Windows/Linux)
- `vcpkg.json` - Updated baseline, added qtbase/qtwebsockets deps, made extras optional
- `src/probe/CMakeLists.txt` - Qt version-independent linking, optional deps support
- `src/launcher/CMakeLists.txt` - Qt version-independent linking, output name qtpilot-launch
- `test_app/CMakeLists.txt` - Qt version-independent linking
- `src/probe/core/probe.cpp` - Conditional spdlog/QDebug logging
- `src/probe/core/injector_windows.cpp` - Use qgetenv instead of deprecated getenv
- `src/probe/transport/websocket_server.cpp` - Conditional logging, Qt6 error signal compat
- `src/probe/transport/jsonrpc_handler.cpp` - Conditional nlohmann_json/QJsonDocument JSON parsing

## Decisions Made

1. **Qt6 as primary, Qt5 fallback** - Qt6 is checked first with QUIET, Qt5 only if Qt6 not found
2. **Optional external dependencies** - nlohmann_json and spdlog are nice-to-have, Phase 1 uses Qt built-ins
3. **WINDOWS_EXPORT_ALL_SYMBOLS** - Simplifies DLL development, can switch to explicit __declspec later
4. **Use qgetenv over std::getenv** - Avoids MSVC deprecation warnings, more Qt-idiomatic

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Source files had unconditional spdlog/nlohmann_json includes**
- **Found during:** Task 2 (Build verification)
- **Issue:** Existing source files required spdlog and nlohmann_json unconditionally
- **Fix:** Added conditional compilation with QTPILOT_HAS_SPDLOG/QTPILOT_HAS_NLOHMANN_JSON macros
- **Files modified:** probe.cpp, injector_windows.cpp, websocket_server.cpp, jsonrpc_handler.cpp
- **Verification:** Build succeeds without external dependencies
- **Committed in:** 2f13642 (Task 2 commit)

**2. [Rule 1 - Bug] Deprecated getenv warnings treated as errors**
- **Found during:** Task 2 (Build verification)
- **Issue:** MSVC /WX flags treat getenv deprecation warning as error
- **Fix:** Replaced std::getenv with Qt's qgetenv function
- **Files modified:** injector_windows.cpp
- **Verification:** Build succeeds with no warnings
- **Committed in:** 2f13642 (Task 2 commit)

**3. [Rule 1 - Bug] Qt6 changed QWebSocket::error signal to errorOccurred**
- **Found during:** Task 2 (Build verification)
- **Issue:** Qt6.5+ renamed the error signal
- **Fix:** Added QT_VERSION_CHECK conditional for error signal connection
- **Files modified:** websocket_server.cpp
- **Verification:** Compiles with Qt6.9.1
- **Committed in:** 2f13642 (Task 2 commit)

---

**Total deviations:** 3 auto-fixed (2 bugs, 1 blocking)
**Impact on plan:** All fixes necessary for build to succeed. No scope creep.

## Issues Encountered

None - existing code structure was well-organized, only needed adaptation for optional dependencies.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Build system complete and working on Windows with Qt6
- Ready for Phase 01-02 (probe core implementation)
- vcpkg can be used to install Qt if needed: `vcpkg install qtbase qtwebsockets`
- Alternative: set CMAKE_PREFIX_PATH to Qt installation directory

---
*Phase: 01-foundation*
*Completed: 2025-01-29*
