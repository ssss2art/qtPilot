---
phase: 13-pypi-publication
plan: 01
subsystem: cli
tags: [python, cli, download, github-releases, sha256, argparse]

# Dependency graph
requires:
  - phase: 11-release-automation
    provides: GitHub Release workflow with SHA256SUMS
provides:
  - download.py module with platform detection and checksum verification
  - download-probe CLI subcommand
  - Unit tests for download functionality (27 tests)
affects: [13-02-pypi-metadata]

# Tech tracking
tech-stack:
  added: []
  patterns: [CLI subcommand architecture with argparse]

key-files:
  created:
    - python/src/qtpilot/download.py
    - python/tests/test_download.py
  modified:
    - python/src/qtpilot/cli.py

key-decisions:
  - "CLI requires subcommand (qtpilot serve, qtpilot download-probe) - cleaner separation"
  - "Uses only stdlib (urllib, hashlib, pathlib) - no new dependencies"
  - "Platform auto-detection via sys.platform - linux-gcc and windows-msvc"
  - "Checksum verification on by default with --no-verify opt-out"

patterns-established:
  - "CLI subcommand pattern: create_parser() returns parser, cmd_* functions handle subcommands"
  - "Download module pattern: build_*_url functions, verify_checksum, download_probe main entry"

# Metrics
duration: 8min
completed: 2026-02-03
---

# Phase 13 Plan 01: Probe Download CLI Summary

**CLI subcommand to download probe binaries from GitHub Releases with SHA256 verification and platform auto-detection**

## Performance

- **Duration:** 8 min
- **Started:** 2026-02-03T22:34:19Z
- **Completed:** 2026-02-03T22:42:00Z
- **Tasks:** 3
- **Files modified:** 3

## Accomplishments
- Created download.py module with platform detection (linux-gcc, windows-msvc)
- Implemented SHA256 checksum verification from GitHub Releases SHA256SUMS file
- Refactored CLI to subcommand architecture (serve, download-probe)
- Added comprehensive unit tests (27 tests, all mocked, no network calls)

## Task Commits

Each task was committed atomically:

1. **Task 1: Create download module with probe fetching logic** - `1603754` (feat)
2. **Task 2: Add download-probe subcommand to CLI** - `b2c2202` (feat)
3. **Task 3: Add unit tests for download module** - `0aca9f6` (test)

## Files Created/Modified
- `python/src/qtpilot/download.py` - Probe download logic with platform detection, URL building, checksum verification
- `python/src/qtpilot/cli.py` - Refactored to subcommand architecture with serve and download-probe commands
- `python/tests/test_download.py` - 27 unit tests covering all download module functionality

## Decisions Made
- **CLI architecture:** Required subcommands (qtpilot serve, qtpilot download-probe) instead of flat args - cleaner separation, better extensibility
- **No new dependencies:** Used only stdlib (urllib.request, hashlib, pathlib) to keep package lightweight
- **Checksum default:** SHA256 verification enabled by default with --no-verify opt-out for security
- **Version normalization:** Support both 6.8 and 6.8.0 formats, preserve -patched suffix

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

None.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- Download module ready for use in pyproject.toml entry point
- CLI help clearly documents available Qt versions (5.15, 5.15-patched, 6.5, 6.8, 6.9)
- Ready for Phase 13-02: PyPI metadata and publishing setup

---
*Phase: 13-pypi-publication*
*Completed: 2026-02-03*
