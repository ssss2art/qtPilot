---
phase: 08-cmake-multi-qt-foundation
plan: 02
subsystem: build-system
tags: [cmake, find_package, qt5, qt6, relocatable, imported-target]
dependency-graph:
  requires: [08-01]
  provides: [find_package-qtPilot, qtPilot::Probe-target, qtPilot_inject_probe-helper]
  affects: [09, 10, 11]
tech-stack:
  added: []
  patterns: [manual-IMPORTED-target, version-aware-package-config]
key-files:
  created:
    - cmake/qtPilot_inject_probe.cmake
  modified:
    - cmake/qtPilotConfig.cmake.in
    - CMakeLists.txt
decisions:
  - id: manual-imported-target
    detail: "Replaced CMake EXPORT mechanism with manual IMPORTED target in config file to support versioned lib paths"
  - id: dll-in-libdir
    detail: "Windows DLL installs to lib/qtpilot/qt{M}.{m}/ (not bin/) so config file resolves from one location"
metrics:
  duration: ~8min
  completed: 2026-02-02
---

# Phase 08 Plan 02: Dual Qt Version Package Config Summary

Qt-version-aware find_package(qtPilot) with manual IMPORTED target and qtPilot_inject_probe() helper

## What Was Done

### Task 1: Rewrite qtPilotConfig.cmake.in for dual Qt version support
**Commit:** `2f68349`

Completely rewrote the package config template:
- Auto-detects consumer Qt version from existing Qt6::Core or Qt5::Core targets
- Falls back to finding Qt itself if no targets exist yet
- Extracts major.minor version to resolve `lib/qtpilot/qt{M}.{m}/` path
- Creates `qtPilot::Probe` as a SHARED IMPORTED target with correct library paths
- Handles Windows DLL+implib and Linux .so, plus debug variants
- Links Qt dependencies (Core, Network, WebSockets, Widgets) on the interface
- Does NOT declare nlohmann_json or spdlog as dependencies (internal-only)
- Uses `@PACKAGE_INIT@` and relative paths for full relocatability
- Includes the `qtPilot_inject_probe.cmake` helper module

Created `cmake/qtPilot_inject_probe.cmake`:
- `qtPilot_inject_probe(target)` function links qtPilot::Probe and handles runtime
- On Windows: POST_BUILD copies probe DLL next to target executable
- On Linux: generates LD_PRELOAD helper script

### Task 2: Update root CMakeLists.txt install rules
**Commit:** `a1a736a`

Updated install section:
- Removed `EXPORT qtPilotTargets` from probe install (manual IMPORTED approach)
- Changed RUNTIME destination to versioned lib dir (DLL and .lib in same place)
- Added launcher install to regular `bin/` directory
- Added install rule for `qtPilot_inject_probe.cmake` helper module
- Removed `install(EXPORT qtPilotTargets ...)` entirely

## Verified Install Tree

```
install/
  bin/
    qtpilot-launch-qt6.9.exe
  include/
    qtpilot/
      core/*.h
      api/*.h
      introspection/*.h
      interaction/*.h
      transport/*.h
      accessibility/*.h
  lib/
    qtpilot/
      qt6.9/
        qtPilot-probe-qt6.9.dll
        qtPilot-probe-qt6.9.lib
  share/
    cmake/
      qtPilot/
        qtPilotConfig.cmake
        qtPilotConfigVersion.cmake
        qtPilot_inject_probe.cmake
```

## Decisions Made

| Decision | Rationale |
|----------|-----------|
| Manual IMPORTED target instead of CMake EXPORT | EXPORT mechanism puts targets at fixed paths; manual approach resolves to versioned subdirs at find_package time |
| DLL installs to lib/ not bin/ | Config file searches one directory for both DLL and import lib; simplifies path resolution |
| No qtPilotTargets.cmake generated | Replaced by inline IMPORTED target creation in config file |

## Deviations from Plan

None - plan executed exactly as written.

## Next Phase Readiness

Phase 08 complete. The build system now supports:
- Versioned artifact naming (08-01)
- Dual Qt version package config with relocatable paths (08-02)

Ready for Phase 09 (CI matrix, packaging) which will use this install layout.
