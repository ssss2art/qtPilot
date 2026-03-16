---
phase: "06"
plan: "03"
subsystem: "api"
tags: ["json-rpc", "qml", "model-view", "native-mode-api"]
dependency_graph:
  requires: ["06-01", "06-02"]
  provides: ["qt.qml.inspect", "qt.models.list", "qt.models.info", "qt.models.data"]
  affects: ["06-04"]
tech_stack:
  added: []
  patterns: ["registerMethod lambda pattern for new API domains"]
key_files:
  created: []
  modified:
    - "src/probe/api/native_mode_api.h"
    - "src/probe/api/native_mode_api.cpp"
decisions:
  - id: "qml-inspect-not-error-for-non-qml"
    description: "qt.qml.inspect returns isQmlItem:false for non-QML objects (not an error)"
    rationale: "Non-QML objects are common; agents should get info without catching exceptions"
  - id: "compile-guard-throws-not-stubs"
    description: "Without QTPILOT_HAS_QML, qt.qml.inspect throws kQmlNotAvailable"
    rationale: "Agent should know QML is unavailable rather than getting silent false"
metrics:
  duration: "4 min"
  completed: "2026-02-01"
---

# Phase 06 Plan 03: QML + Model API Wiring Summary

**Wire QML and Model/View introspection into NativeModeApi with 4 new qt.* JSON-RPC methods using QmlInspector and ModelNavigator from Plans 01/02.**

## What Was Done

### Task 1: Add qt.qml.inspect Method (2aa5f84)

Registered `qt.qml.inspect` on JsonRpcHandler via new `registerQmlMethods()` private method:

- Takes `objectId` parameter, resolves via `resolveObjectParam()`
- When `QTPILOT_HAS_QML` defined: calls `inspectQmlItem(obj)` and returns `{isQmlItem, qmlId, qmlFile, qmlTypeName}`
- When `QTPILOT_HAS_QML` not defined: throws `kQmlNotAvailable` with clear message
- Non-QML objects return `{isQmlItem: false}` (informational, not an error)

### Task 2: Add qt.models.* Methods (7e9fff6)

Registered 3 model methods via new `registerModelMethods()` private method:

**qt.models.list** - No required params. Calls `ModelNavigator::listModels()` directly.

**qt.models.info** - Takes `objectId`. Resolves object, then calls `ModelNavigator::resolveModel()` to handle both direct models and views. Returns `getModelInfo()` result with rows, cols, roles.

**qt.models.data** - Takes `objectId` plus optional `offset`, `limit`, `roles`, `parentRow`, `parentCol`. Role resolution handles both string names (via `resolveRoleName()`) and integer IDs. Throws `kModelRoleNotFound` with available roles listed when string role not found.

All methods follow existing NativeModeApi patterns exactly:
- Anonymous namespace helpers (`parseParams`, `resolveObjectParam`)
- `ResponseEnvelope::wrap()` on all responses
- `JsonRpcException` with `ErrorCode` constants for all errors
- Lambda registration via `m_handler->RegisterMethod()`

## Decisions Made

| Decision | Rationale |
|----------|-----------|
| isQmlItem:false is not an error | Non-QML objects are common; agents get info without exceptions |
| kQmlNotAvailable thrown without QML | Agent knows QML unavailable rather than getting silent false |

## Deviations from Plan

None - plan executed exactly as written.

## Verification

- `cmake --build build/ --config Debug` compiles cleanly
- All 11 test suites pass (100%, 0 failures)
- New methods use ResponseEnvelope::wrap() and ErrorCode constants
- View objectIds auto-resolve to their model in all qt.models.* methods

## Next Phase Readiness

Plan 06-04 (Extended Introspection Testing) can proceed. All 4 new methods are registered and ready for integration testing:
- `qt.qml.inspect` - QML metadata extraction
- `qt.models.list` - Model discovery
- `qt.models.info` - Model metadata
- `qt.models.data` - Paginated data retrieval with role filtering
