---
phase: 04-computer-use-mode
plan: 04
subsystem: probe-resilience
tags: [error-handling, backward-compat, DLL-injection, legacy-api]
dependency-graph:
  requires: [04-01, 04-02, 04-03]
  provides: [resilient-api-registration, dual-param-legacy-methods]
  affects: [05-chrome-mode]
tech-stack:
  added: []
  patterns: [independent-try-catch, parameter-fallback]
key-files:
  created: []
  modified:
    - src/probe/core/probe.cpp
    - src/probe/transport/jsonrpc_handler.cpp
decisions:
  - id: separate-try-catch
    description: Each API (NativeModeApi, ComputerUseModeApi) wrapped in independent try/catch
    rationale: Failure of one must not prevent the other from registering during DLL injection
  - id: objectId-fallback
    description: Legacy qtpilot.* methods accept both "id" and "objectId" parameter names
    rationale: Clients using qt.* convention naturally use "objectId"; accepting both prevents confusion
metrics:
  duration: 3 min
  completed: 2026-01-31
---

# Phase 04 Plan 04: UAT Gap Closure - Probe Resilience Summary

**One-liner:** Independent try/catch for API registration + objectId param fallback on all 11 legacy methods

## What Was Done

### Task 1: Exception handling around API instantiation
- Wrapped NativeModeApi and ComputerUseModeApi constructors in separate try/catch blocks in `Probe::initialize()`
- Each block catches both `std::exception` and `...` (unknown exceptions)
- Success/failure logged to stderr with `fprintf` (safe pre-Qt-init)
- Added `#include <stdexcept>` to probe.cpp
- **Commit:** 68f9a9a

### Task 2: Backward-compat objectId param support
- Updated all 11 legacy `qtpilot.*` methods that read an object ID parameter
- Each now tries `"id"` first, falls back to `"objectId"` if empty
- Methods updated: getObjectInfo, listProperties, getProperty, setProperty, listMethods, invokeMethod, listSignals, click, sendKeys, screenshot, getGeometry
- Existing clients using `"id"` are completely unaffected
- **Commit:** 9e50143

## Verification

- Build: `cmake --build build` succeeds with no new warnings
- Tests: All 10 test suites pass (100%) with zero regressions
- stderr output now shows API registration success/failure messages
- Legacy methods accept both "id" and "objectId" parameter names

## Deviations from Plan

None - plan executed exactly as written.

## Decisions Made

| Decision | Rationale |
|----------|-----------|
| Separate try/catch per API | NativeModeApi failure (observed in DLL injection) must not block ComputerUseModeApi |
| fprintf for registration logging | Safe before Qt logging is fully initialized in DLL context |
| objectId fallback on all 11 methods | Plan specified getObjectInfo and getGeometry but all legacy methods benefit from consistency |

## Commits

| Hash | Type | Description |
|------|------|-------------|
| 68f9a9a | fix | Exception handling around API instantiation in probe |
| 9e50143 | fix | objectId param fallback to legacy qtpilot.* methods |
