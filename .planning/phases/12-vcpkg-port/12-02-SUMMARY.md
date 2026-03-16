---
phase: 12-vcpkg-port
plan: 02
subsystem: distribution
tags: [vcpkg, binary-port, cmake, packaging]
dependency-graph:
  requires: [12-01]
  provides: [binary overlay port at ports/qtpilot-bin/]
  affects: [users wanting prebuilt probe without compilation]
tech-stack:
  added: []
  patterns: [vcpkg_download_distfile, IMPORTED target, per-artifact SHA512]
key-files:
  created:
    - ports/qtpilot-bin/vcpkg.json
    - ports/qtpilot-bin/portfile.cmake
    - ports/qtpilot-bin/qtPilotConfig.cmake
    - ports/qtpilot-bin/usage
  modified: []
decisions:
  - id: per-artifact-hash
    choice: SHA512 hash per-artifact in portfile
    reason: vcpkg standard, enables per-Qt-version downloads
  - id: detect-then-download
    choice: Detect Qt first, download single matching artifact
    reason: Minimal disk usage, user gets only what they need
  - id: qt62-excluded
    choice: Qt 6.2 not in available versions
    reason: Dropped in Phase 11.1 per CI matrix alignment
metrics:
  duration: ~10 min
  completed: 2026-02-03
---

# Phase 12 Plan 02: vcpkg Binary Overlay Port Summary

**One-liner:** Binary overlay port downloads prebuilt probe from GitHub Releases with per-Qt-version SHA512 validation

## What Was Built

Created `ports/qtpilot-bin/` directory with four files enabling prebuilt probe installation via vcpkg:

1. **vcpkg.json** - Port manifest declaring `qtpilot-bin` with Windows/Linux support
2. **portfile.cmake** - Downloads matching probe from GitHub Releases based on detected Qt
3. **qtPilotConfig.cmake** - Creates qtPilot::Probe IMPORTED target identically to source port
4. **usage** - Post-install instructions showing find_package pattern

## Key Design Decisions

### Per-Artifact SHA512 Hashes
Each Qt version x platform combination has its own SHA512 hash variable:
```cmake
set(SHA512_qt5.15_windows-msvc_dll "0")
set(SHA512_qt6.9_linux-gcc_so "0")
```
Placeholder `0` values documented for release-time update.

### Detect-Then-Download Pattern
```cmake
find_package(Qt6 QUIET COMPONENTS Core)
# ... detect Qt version
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)" _qt_mm "${_qt_version}")
# ... download only matching artifact
vcpkg_download_distfile(PROBE_BINARY
    URLS "${_download_url}"  # single artifact URL
```

### Qt 6.2 Exclusion
Available versions list explicitly excludes Qt 6.2 with Phase 11.1 reference:
```cmake
# NOTE: Qt 6.2 was dropped in Phase 11.1 (source compatibility refactor).
set(_available_versions "5.15;6.5;6.8;6.9")
```

### Install Layout Match
Binary probe installed to identical path as source port:
- Linux: `lib/qtpilot/qt{M}.{m}/libqtPilot-probe-qt{M}.{m}.so`
- Windows: `lib/qtpilot/qt{M}.{m}/qtPilot-probe-qt{M}.{m}.dll`

## Commits

| Hash | Description |
|------|-------------|
| a57f31c | feat(12-02): add vcpkg.json and portfile.cmake for binary port |
| 8af241a | feat(12-02): add qtPilotConfig.cmake and usage for binary port |

## Deviations from Plan

None - plan executed exactly as written.

## Testing Notes

Port cannot be fully tested until:
1. A GitHub Release tag exists (v0.1.0 or later)
2. SHA512 hashes are calculated and filled in portfile.cmake

Manual verification performed:
- vcpkg.json is valid JSON with correct name
- portfile.cmake has Qt detection, single-artifact download, correct install layout
- qtPilotConfig.cmake creates qtPilot::Probe IMPORTED target with versioned path lookup
- usage file documents find_package and mentions source port alternative

## Next Phase Readiness

Phase 12 complete. Both source and binary overlay ports available at `ports/qtpilot/` and `ports/qtpilot-bin/`.

**Before first release:**
- Calculate SHA512 hashes for all 8 release artifacts
- Update placeholder values in `ports/qtpilot-bin/portfile.cmake`
- Update `ports/qtpilot/portfile.cmake` SHA512 for source archive
