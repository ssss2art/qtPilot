---
phase: 10-patched-qt-ci
verified: 2026-02-02T14:35:55Z
status: passed
score: 8/8 must-haves verified
re_verification: false
---

# Phase 10: Patched Qt 5.15.1 CI Verification Report

**Phase Goal:** CI can build and cache a custom-patched Qt 5.15.1 and produce probe binaries against it
**Verified:** 2026-02-02T14:35:55Z
**Status:** PASSED
**Re-verification:** No - initial verification

## Goal Achievement

### Observable Truths

All 8 must-haves VERIFIED:

1. **Composite action exists** - .github/actions/build-qt/action.yml (259 lines) with 14 steps covering download, patch via git apply, configure, build, install, cache with actions/cache@v4

2. **Cache key is safe** - Line 49: key includes hashFiles of patches-dir + runner.os + qt-version + cfgv1, NO restore-keys field

3. **Cache skips build** - 11 occurrences of if: steps.qt-cache.outputs.cache-hit != true on steps 3-13

4. **Patch directory documented** - .ci/patches/5.15.1/README.md (59 lines) with naming convention, format requirements, cache invalidation explanation + .gitkeep

5. **Workflow triggers correctly** - push: branches: [main] + workflow_dispatch with NO pull_request trigger

6. **Probe builds on both platforms** - 2-cell matrix: ubuntu-22.04 + windows-2022, calls composite action, configures probe with CMAKE_PREFIX_PATH from qt-dir output, builds/tests/installs

7. **Composite action integrated** - Line 52: uses: ./.github/actions/build-qt with qt-version: 5.15.1 and patches-dir: .ci/patches/5.15.1, MSVC dev cmd setup before action with toolset 14.29

8. **Workflow is isolated** - Line 21: timeout-minutes: 120, separate workflow file, path filters include .ci/patches/**, artifacts named qt5.15-patched

**Score:** 8/8 truths verified

### Required Artifacts

All 4 artifacts VERIFIED:

- .github/actions/build-qt/action.yml (259 lines, 14 steps, platform-specific configure/build/install)
- .ci/patches/5.15.1/README.md (59 lines, complete documentation)
- .ci/patches/5.15.1/.gitkeep (exists, directory tracker)
- .github/workflows/ci-patched-qt.yml (140 lines, 2-cell matrix, full probe build pipeline)

### Key Links

All 5 key links WIRED:

- Composite action -> actions/cache@v4: cache-hit output used in 11 conditional steps
- Composite action -> .ci/patches/5.15.1/: patches-dir in cache key hash and patch application
- Workflow -> composite action: called with correct inputs, outputs consumed for CMAKE_PREFIX_PATH
- Workflow -> CMakePresets.json: configure/build/test/install use presets
- Workflow -> .ci/patches/5.15.1/: patches-dir from env, path filter triggers on patch changes

### Requirements Coverage

Both requirements SATISFIED:

- CICD-05: Composite action implements full Qt build pipeline with caching
- CICD-06: Workflow matrix covers both platforms with correct MSVC toolset

### Anti-Patterns

None detected. No TODO/FIXME/PLACEHOLDER comments, no stub patterns, no empty implementations.

### Human Verification Required

Four runtime validations needed:

1. **Cache Behavior** - Verify cache hit skips Qt build on second run (30-60 min -> 5 min)
2. **Probe Artifacts** - Verify artifacts contain correct probe binaries in lib/qtpilot/qt5.15/
3. **Workflow Isolation** - Verify ci.yml and ci-patched-qt.yml run independently
4. **Patch Application** - Verify patch application works with real patch file

---

## Verification Details

### Level 1: Existence - ALL PASS

All 4 required files exist with adequate length:
- action.yml: 259 lines
- README.md: 59 lines  
- .gitkeep: present
- ci-patched-qt.yml: 140 lines

### Level 2: Substantive - ALL PASS

**Composite Action:**
- 259 lines exceeds 15-line minimum
- 0 TODO/FIXME/placeholder patterns
- Valid composite action format
- 14 distinct steps covering full pipeline
- Platform-specific steps for Linux and Windows
- Error handling for patch failures and private headers

**Workflow:**
- 140 lines exceeds 10-line minimum  
- 0 TODO/FIXME/placeholder patterns
- Full probe build pipeline (configure, build, test, install, verify, upload)
- Explicit OS versions (ubuntu-22.04, windows-2022)
- MSVC prerequisite satisfied before composite action call

**Patch Directory:**
- README: 59 lines exceeds 5-line minimum
- Complete documentation of conventions
- .gitkeep present for tracking

### Level 3: Wired - ALL PASS

**Composite Action Wiring:**
- qt-version input used in: download URL, cache key, configure, private header path
- patches-dir input used in: cache key hash, patch discovery
- install-prefix input used in: cache path, configure, private header path
- qt-dir output set and consumed by workflow CMAKE_PREFIX_PATH
- cache-hit output used in 11 conditional steps + workflow summary
- actions/cache@v4 integrated with correct key, no restore-keys
- Platform conditions on 8 steps

**Workflow Wiring:**
- Composite action called with ./.github/actions/build-qt
- Inputs provided from env variables
- Outputs consumed for CMAKE_PREFIX_PATH  
- Probe build pipeline complete (configure/build/test/install)
- Artifacts uploaded with patched naming
- Workflow isolated: separate file, different triggers, path filters

---

## Conclusion

**Phase 10 goal ACHIEVED.** All 8 must-haves verified against actual codebase.

**No gaps found.** All artifacts are substantive (no stubs), properly wired, and conform to plan specifications.

**Human verification required** for runtime behavior: cache hits, artifact contents, workflow concurrency, patch application.

The codebase structure is correct and ready for CI execution.

---

_Verified: 2026-02-02T14:35:55Z_
_Verifier: Claude (gsd-verifier)_
_Method: Goal-backward verification with 3-level artifact analysis (existence, substantive, wired)_
