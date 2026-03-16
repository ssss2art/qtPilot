# Phase 11 Plan 02: Release Workflow Summary

**One-liner:** Tag-triggered release workflow that calls both CI pipelines, collects all 10 probe binaries, generates SHA256 checksums, and publishes a GitHub Release

## What Was Done

Created `.github/workflows/release.yml` — a complete release automation workflow triggered by pushing a `v*` tag. The workflow orchestrates the full release pipeline:

1. **build-standard** job calls `ci.yml` to produce 8 standard probe binaries (4 Qt versions x 2 platforms)
2. **build-patched** job calls `ci-patched-qt.yml` to produce 2 patched Qt 5.15.1 probe binaries
3. **release** job downloads all 10 artifacts, extracts and renames probes with platform-encoded filenames, generates SHA256SUMS, and creates a GitHub Release via `softprops/action-gh-release@v2`

Release asset naming convention: `qtPilot-probe-{qt_tag}-{platform}.{ext}` (e.g., `qtPilot-probe-qt6.8-linux-gcc.so`, `qtPilot-probe-qt5.15-patched-windows-msvc.dll`). Windows artifacts also include `.lib` import libraries.

## Tasks Completed

| Task | Name | Commit | Files |
|------|------|--------|-------|
| 1 | Create release workflow | daada69 | .github/workflows/release.yml |

## Decisions Made

| Decision | Rationale |
|----------|-----------|
| `fail_on_unmatched_files: true` | Fails release if any expected binary is missing — prevents incomplete releases |
| `generate_release_notes: true` | Auto-generates changelog from commits since last tag — zero manual effort |
| find-based binary discovery | Resilient to path changes; locates .so/.dll under lib/qtpilot/ regardless of nested structure |
| Error-on-missing probe | Script exits with error if any of the 10 probes is not found — catches CI regressions early |

## Deviations from Plan

None — plan executed exactly as written.

## Verification Results

- YAML valid (parsed by Python yaml module)
- Contains `softprops/action-gh-release@v2`
- Contains `sha256sum` checksum generation
- References both `ci.yml` and `ci-patched-qt.yml` as reusable workflows
- Trigger: `push.tags: ['v*']`
- 10 artifact entries in extraction script
- 87 lines (above 60-line minimum)
- `permissions.contents: write` set for release creation

## Release Asset Inventory

When triggered, the workflow produces these release assets:

| # | Filename | Source |
|---|----------|--------|
| 1 | qtPilot-probe-qt5.15-linux-gcc.so | ci.yml |
| 2 | qtPilot-probe-qt5.15-windows-msvc.dll | ci.yml |
| 3 | qtPilot-probe-qt5.15-windows-msvc.lib | ci.yml |
| 4 | qtPilot-probe-qt6.2-linux-gcc.so | ci.yml |
| 5 | qtPilot-probe-qt6.2-windows-msvc.dll | ci.yml |
| 6 | qtPilot-probe-qt6.2-windows-msvc.lib | ci.yml |
| 7 | qtPilot-probe-qt6.8-linux-gcc.so | ci.yml |
| 8 | qtPilot-probe-qt6.8-windows-msvc.dll | ci.yml |
| 9 | qtPilot-probe-qt6.8-windows-msvc.lib | ci.yml |
| 10 | qtPilot-probe-qt6.9-linux-gcc.so | ci.yml |
| 11 | qtPilot-probe-qt6.9-windows-msvc.dll | ci.yml |
| 12 | qtPilot-probe-qt6.9-windows-msvc.lib | ci.yml |
| 13 | qtPilot-probe-qt5.15-patched-linux-gcc.so | ci-patched-qt.yml |
| 14 | qtPilot-probe-qt5.15-patched-windows-msvc.dll | ci-patched-qt.yml |
| 15 | qtPilot-probe-qt5.15-patched-windows-msvc.lib | ci-patched-qt.yml |
| 16 | SHA256SUMS | generated |

## Next Phase Readiness

Phase 11 (Release Automation) is now complete. Both plans delivered:
- 11-01: Made CI workflows reusable via `workflow_call` triggers
- 11-02: Created the release workflow that orchestrates them

The release pipeline is fully automated: push a `v*` tag and a GitHub Release with all 10 probe binaries appears automatically.
