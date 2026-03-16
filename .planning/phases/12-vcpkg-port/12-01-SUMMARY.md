---
phase: 12
plan: 01
subsystem: distribution
tags: [vcpkg, cmake, packaging, overlay-port]

dependency_graph:
  requires:
    - 08: CMake install system (qtPilotConfig.cmake)
  provides:
    - vcpkg source overlay port for local install
  affects:
    - 12-02: binary port (sibling distribution method)
    - users installing via vcpkg

tech_stack:
  added:
    - vcpkg overlay port structure
  patterns:
    - vcpkg_from_github source fetch
    - vcpkg_cmake_configure/install build
    - vcpkg_cmake_config_fixup relocation

files:
  key_files:
    created:
      - ports/qtpilot/vcpkg.json
      - ports/qtpilot/portfile.cmake
      - ports/qtpilot/usage
    modified: []

decisions:
  - id: no-qt-deps
    choice: "No Qt dependencies in vcpkg manifest"
    reason: "User provides their own Qt; project CMakeLists.txt handles find_package"
  - id: sha512-placeholder
    choice: "SHA512 0 placeholder in portfile"
    reason: "No release tag yet; hash will be updated when v0.1.0 tag is created"

metrics:
  duration: ~2 minutes
  completed: 2026-02-03
---

# Phase 12 Plan 01: vcpkg Source Overlay Port Summary

**One-liner:** vcpkg source port at ports/qtpilot/ that builds probe from GitHub against user's Qt installation

## What Was Built

Created a vcpkg overlay port that enables users to install qtPilot via:
```bash
vcpkg install qtpilot --overlay-ports=./ports
```

The port:
- Fetches source from GitHub (ssss2art/qtPilot)
- Delegates Qt detection to the project's own CMakeLists.txt
- Disables tests and test app builds for cleaner vcpkg integration
- Relocates CMake config files to vcpkg-standard paths
- Shows usage instructions after install

## Key Files

| File | Purpose |
|------|---------|
| ports/qtpilot/vcpkg.json | Port manifest - declares only cmake helper deps, no Qt |
| ports/qtpilot/portfile.cmake | Build instructions - vcpkg_from_github + cmake workflow |
| ports/qtpilot/usage | Post-install instructions - find_package and injection notes |

## Technical Details

**Port Manifest (vcpkg.json):**
- Name: qtpilot
- Version: 0.1.0
- Dependencies: vcpkg-cmake, vcpkg-cmake-config (host-only)
- NO Qt dependencies - user's Qt installation is used

**Portfile Flow:**
1. `vcpkg_from_github` fetches source (SHA512 placeholder until first release)
2. `vcpkg_cmake_configure` runs CMake with tests disabled
3. `vcpkg_cmake_install` builds and installs
4. `vcpkg_cmake_config_fixup` moves config from share/cmake/qtPilot to share/qtpilot
5. License and usage files installed

**CMake Options Passed:**
- `-DQTPILOT_BUILD_TESTS=OFF` - skip test compilation
- `-DQTPILOT_BUILD_TEST_APP=OFF` - skip test app
- `-DQTPILOT_ENABLE_CLANG_TIDY=OFF` - skip static analysis
- `-DQTPILOT_DEPLOY_QT=OFF` - no windeployqt (vcpkg handles this)

## Decisions Made

1. **No Qt dependencies declared** - The port relies on Qt already being installed on the system (either via vcpkg or externally). This matches the Phase 9 decision to feature-gate Qt deps.

2. **SHA512 placeholder** - Using `SHA512 0` since no release tag exists yet. This is standard vcpkg practice for ports under development.

## Deviations from Plan

None - plan executed exactly as written.

## Next Phase Readiness

The source port is complete. Plan 12-02 will add the binary download port as an alternative installation method for users who want prebuilt binaries.

## Commits

| Hash | Message |
|------|---------|
| 1552a40 | feat(12-01): create vcpkg source overlay port |

---

*Generated: 2026-02-03*
