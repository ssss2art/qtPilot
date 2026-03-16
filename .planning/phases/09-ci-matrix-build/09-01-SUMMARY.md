---
phase: 09-ci-matrix-build
plan: 01
subsystem: ci
tags: [github-actions, matrix-build, vcpkg, qt-versions]
dependency-graph:
  requires: [08-01, 08-02]
  provides: [matrix-ci-workflow, ci-artifact-upload]
  affects: [11-xx]
tech-stack:
  added: [jurplel/install-qt-action@v4, lukka/run-vcpkg@v11]
  patterns: [matrix-build, include-style-matrix, vcpkg-feature-gating]
key-files:
  created: []
  modified: [.github/workflows/ci.yml, vcpkg.json]
decisions:
  - id: vcpkg-qt-feature-gate
    choice: "Qt deps moved to opt-in vcpkg feature"
    reason: "Prevents 30+ min Qt build from source in CI cells"
  - id: include-style-matrix
    choice: "Use include-style matrix entries (not combinatorial)"
    reason: "Runner OS varies per Qt version (ubuntu-22.04 for Qt 5.15, ubuntu-24.04 for Qt 6.x)"
  - id: qt-root-dir-env
    choice: "Use QT_ROOT_DIR for Qt6, Qt5_DIR for Qt5"
    reason: "install-qt-action@v4 sets QT_ROOT_DIR reliably for both versions"
metrics:
  duration: "~3 minutes"
  completed: 2026-02-02
---

# Phase 9 Plan 01: CI Matrix Build Summary

**One-liner:** 8-cell GitHub Actions matrix (Qt 5.15/6.2/6.8/6.9 x Linux/Windows) with vcpkg feature-gating to avoid building Qt from source in CI.

## Tasks Completed

| # | Task | Commit | Key Changes |
|---|------|--------|-------------|
| 1 | Update vcpkg.json for CI compatibility | 6d77ba8 | Moved qtbase/qtwebsockets to opt-in "qt" feature; empty default deps |
| 2 | Write the matrix CI workflow | ffe5ae9 | 8-cell matrix, install-qt-action@v4, artifact upload, layout verification |

## What Was Built

### vcpkg.json Changes
- Top-level `dependencies` array is now empty (no Qt built by default in manifest mode)
- New `qt` feature contains `qtbase` and `qtwebsockets` for local developers
- `extras` and `tests` features unchanged
- CI sets `VCPKG_MANIFEST_NO_DEFAULT_FEATURES=ON` and `VCPKG_MANIFEST_FEATURES=extras`

### CI Workflow (.github/workflows/ci.yml)
- **8 matrix cells** using `include`-style entries (not combinatorial)
- **Qt versions:** 5.15.2, 6.2.4, 6.8.0, 6.9.0
- **Platforms:** ubuntu-22.04/24.04 (Linux GCC), windows-latest (Windows MSVC)
- **Qt installation:** jurplel/install-qt-action@v4 with caching
- **vcpkg:** lukka/run-vcpkg@v11 installs only nlohmann-json and spdlog
- **fail-fast: false** ensures all cells run to completion
- **Install layout verification** step confirms probe binary exists in versioned `lib/qtpilot/{tag}/` dir
- **Artifact naming:** `qtpilot-{artifact_tag}-{platform}` (e.g., `qtpilot-qt6.9-windows-msvc`)
- **Triggers:** push/PR to main with path filters, plus workflow_dispatch
- **Preserved jobs:** lint (ubuntu-24.04), python (ubuntu-24.04), codeql (ubuntu-24.04 with Qt 6.8)

### Key Design Decisions

1. **Include-style matrix** - Runner OS varies per Qt version (Qt 5.15 needs ubuntu-22.04 for glibc compat)
2. **QT_ROOT_DIR for Qt6** - install-qt-action@v4 sets this reliably; Qt5_DIR used for Qt5 as fallback
3. **Separate configure steps per OS** - Linux uses bash, Windows uses PowerShell with different env var syntax

## Deviations from Plan

None - plan executed exactly as written.

## Verification Results

- YAML validation: passed (python yaml.safe_load)
- Matrix cell count: 8 (confirmed programmatically)
- fail-fast: false (confirmed)
- install-qt-action@v4: 2 usages (build matrix + codeql)
- workflow_dispatch: present
- Path filters: present on push and pull_request
- Artifact names: `qtpilot-${{ matrix.artifact_tag }}-${{ matrix.platform }}`
- Install layout verification: present for both Linux and Windows

## Next Phase Readiness

- Artifact names follow `qtpilot-qt{M}.{m}-{platform}` format expected by Phase 11 (packaging)
- Install layout `lib/qtpilot/qt{M}.{m}/` aligns with Phase 8 conventions
- No blockers for subsequent phases
