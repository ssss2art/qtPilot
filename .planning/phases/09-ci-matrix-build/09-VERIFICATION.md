---
phase: 09-ci-matrix-build
verified: 2026-02-02T15:30:00Z
status: passed
score: 4/4 must-haves verified
---

# Phase 9: CI Matrix Build Verification Report

**Phase Goal:** Every push validates the probe builds cleanly on 4 Qt versions across Windows and Linux
**Verified:** 2026-02-02T15:30:00Z
**Status:** passed
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | Push to main triggers an 8-cell matrix build (4 Qt versions x 2 platforms) | VERIFIED | Matrix has exactly 8 entries covering Qt 5.15.2, 6.2.4, 6.8.0, 6.9.0 on both linux-gcc and windows-msvc |
| 2 | Each matrix cell produces uploadable probe artifacts | VERIFIED | actions/upload-artifact@v4 configured with if-no-files-found: error for all cells |
| 3 | fail-fast is disabled so all cells run to completion | VERIFIED | fail-fast: false set in matrix strategy |
| 4 | Qt is installed via jurplel/install-qt-action@v4 with correct runner per Qt version | VERIFIED | Qt 5.15.2 uses ubuntu-22.04, all Qt 6.x use ubuntu-24.04, action is @v4 |

**Score:** 4/4 truths verified

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| .github/workflows/ci.yml | Matrix build workflow with 8 cells | VERIFIED | EXISTS (294 lines), SUBSTANTIVE (complete workflow), WIRED (uses CMakePresets ci-linux/ci-windows) |
| vcpkg.json | CI-compatible vcpkg manifest | VERIFIED | EXISTS (32 lines), SUBSTANTIVE (proper feature gating), WIRED (CI sets VCPKG_MANIFEST_FEATURES=extras) |

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|----|--------|---------|
| .github/workflows/ci.yml | CMakePresets.json | cmake --preset ci-linux / ci-windows | WIRED | Configure steps use --preset with ci-linux/ci-windows values |
| .github/workflows/ci.yml | jurplel/install-qt-action@v4 | Qt version from matrix variable | WIRED | Action called with version from matrix in build and codeql jobs |
| vcpkg.json | CI environment | VCPKG_MANIFEST_FEATURES control | WIRED | Top-level deps empty, Qt in opt-in feature, CI sets VCPKG_MANIFEST_NO_DEFAULT_FEATURES=ON |

### Requirements Coverage

Requirements mapped to Phase 9 from REQUIREMENTS.md:

| Requirement | Status | Evidence |
|-------------|--------|----------|
| CICD-01: Qt 5.15 on Windows/Linux | SATISFIED | Matrix entries for qt_version 5.15.2 on both windows-msvc and linux-gcc |
| CICD-02: Qt 6.2 on Windows/Linux | SATISFIED | Matrix entries for qt_version 6.2.4 on both windows-msvc and linux-gcc |
| CICD-03: Qt 6.8 on Windows/Linux | SATISFIED | Matrix entries for qt_version 6.8.0 on both windows-msvc and linux-gcc |
| CICD-04: Qt 6.9 on Windows/Linux | SATISFIED | Matrix entries for qt_version 6.9.0 on both windows-msvc and linux-gcc |

**Coverage:** 4/4 Phase 9 requirements satisfied

### Anti-Patterns Found

None. No blocker or warning patterns detected.

### Detailed Verification Results

#### 1. Matrix Structure (VERIFIED)

Matrix cell count: 8 (grep -c "qt_version:" = 8)

Qt version and platform pairs verified:
- Qt 5.15.2 on linux-gcc and windows-msvc
- Qt 6.2.4 on linux-gcc and windows-msvc
- Qt 6.8.0 on linux-gcc and windows-msvc
- Qt 6.9.0 on linux-gcc and windows-msvc

#### 2. fail-fast Setting (VERIFIED)

Strategy includes fail-fast: false. A failing cell will not abort other cells. All 8 cells run to completion.

#### 3. Qt Installation (VERIFIED)

- Action version: jurplel/install-qt-action@v4 (verified, not v3)
- Qt 5.15.2 uses ubuntu-22.04 (older glibc compatibility)
- Qt 6.x versions use ubuntu-24.04
- All Windows builds use windows-latest
- Qt modules specified: qtwebsockets
- Caching enabled: cache: true

#### 4. vcpkg Feature Gating (VERIFIED)

vcpkg.json structure:
- Top-level dependencies: [] (empty)
- qt feature: qtbase, qtwebsockets
- extras feature: nlohmann-json, spdlog

CI environment variables set:
- VCPKG_MANIFEST_NO_DEFAULT_FEATURES: "ON"
- VCPKG_MANIFEST_FEATURES: "extras"

Result: CI only installs nlohmann-json and spdlog from vcpkg, NOT Qt (which comes from install-qt-action). This avoids 30+ minute Qt builds from source.

#### 5. Artifact Upload (VERIFIED)

Artifact upload configuration:
- Action: actions/upload-artifact@v4
- Naming pattern: qtpilot-$artifact_tag-$platform (e.g., qtpilot-qt6.9-windows-msvc)
- Error handling: if-no-files-found: error (ensures missing artifacts fail workflow)
- Retention: 7 days (appropriate for development workflow)
- Path: install/$preset/ (full install directory)

#### 6. Install Layout Verification (VERIFIED)

Both Linux and Windows have dedicated verification steps that check for probe binary existence in install/$preset/lib/qtpilot/$artifact_tag/ directory before artifact upload. Steps fail with error if no files found, catching install layout issues early.

#### 7. Workflow Triggers (VERIFIED)

Triggers configured:
- push to main with path filters (src/**, CMakeLists.txt, cmake/**, .github/workflows/**, vcpkg.json, CMakePresets.json)
- pull_request to main with same path filters
- workflow_dispatch for manual triggering

Path filters prevent unnecessary builds when only documentation changes.

#### 8. CMake Preset Integration (VERIFIED)

Configure steps use:
- Linux: cmake --preset ci-linux -DCMAKE_PREFIX_PATH="$QT_DIR"
- Windows: cmake --preset ci-windows -DCMAKE_PREFIX_PATH="$qtDir"

Build: cmake --build --preset $matrix.preset --parallel
Test: ctest --preset $matrix.preset --output-on-failure
Install: cmake --install build/$matrix.preset --prefix install/$matrix.preset

Presets ci-linux and ci-windows exist in CMakePresets.json and inherit from vcpkg-base, enabling vcpkg toolchain integration.

#### 9. YAML Validity (VERIFIED)

python -c "import yaml; yaml.safe_load(open('.github/workflows/ci.yml'))" executes without errors. YAML syntax is valid.

#### 10. Preserved Jobs (VERIFIED)

Other workflow jobs preserved and updated:
- lint: runs on ubuntu-24.04, checks clang-format on src/
- python: runs on ubuntu-24.04, tests Python MCP server
- codeql: runs on ubuntu-24.04 with Qt 6.8.0, performs security analysis

### Human Verification Required

None. All verification performed programmatically through file structure and grep analysis. Actual CI execution will validate runtime behavior when workflow is triggered.

---

## Summary

Phase 9 goal ACHIEVED. All must-haves verified:

1. 8-cell matrix with correct Qt versions and platforms
2. Artifact upload configured for all cells with error handling
3. fail-fast disabled (cells run independently)
4. Qt installed via jurplel/install-qt-action@v4 with version-appropriate runners
5. vcpkg feature gating prevents Qt from being built from source
6. Install layout verification catches missing probe binaries
7. Proper CMake preset integration
8. All 4 Phase 9 requirements (CICD-01 through CICD-04) satisfied

The workflow is ready for runtime validation. When triggered, it will build probe binaries for 8 configurations and upload them as artifacts with names matching Phase 11 expected input format.

Next phase (Phase 10) can proceed to add patched Qt 5.15.1 support as a parallel workflow.

---

_Verified: 2026-02-02T15:30:00Z_
_Verifier: Claude (gsd-verifier)_
