---
phase: 06-extended-introspection
plan: 01
subsystem: introspection
tags: [qml, qt-quick, object-id, cmake, error-codes]
depends_on:
  requires: [05-chrome-mode]
  provides: [qml-inspector, qml-aware-object-id, qml-error-codes, model-error-codes]
  affects: [06-02, 06-03, 06-04]
tech-stack:
  added: [Qt6::Qml, Qt6::Quick]
  patterns: [compile-guard-optional-dependency, stub-fallback-inline]
key-files:
  created:
    - src/probe/introspection/qml_inspector.h
    - src/probe/introspection/qml_inspector.cpp
  modified:
    - CMakeLists.txt
    - src/probe/CMakeLists.txt
    - src/probe/introspection/object_id.cpp
    - src/probe/api/error_codes.h
decisions:
  - id: qml-optional-dependency
    description: "Qt Quick is optional — QTPILOT_HAS_QML compile guard with inline stub fallbacks"
    rationale: "Build must succeed without Qt Quick installed; stubs return safe defaults"
  - id: qml-id-highest-priority
    description: "QML id takes highest priority in generateIdSegment(), above objectName"
    rationale: "QML ids are human-authored, stable identifiers — ideal for object paths"
  - id: short-type-name-for-anonymous-qml
    description: "Anonymous QML items use short type name (Rectangle not QQuickRectangle)"
    rationale: "QQuick prefix is internal implementation detail, not meaningful to users"
metrics:
  duration: 7 min
  completed: 2026-02-01
---

# Phase 06 Plan 01: QML Inspector Infrastructure Summary

QML introspection foundation with optional Qt Quick dependency, QmlItemInfo metadata extraction, QML-aware object ID generation using QML id as highest priority, and error codes for QML and Model domains.

## Tasks Completed

| Task | Name | Commit | Files |
|------|------|--------|-------|
| 1 | CMake optional Qt Quick dependency + QML Inspector + Error Codes | 3086de7 | CMakeLists.txt, src/probe/CMakeLists.txt, qml_inspector.h/.cpp, error_codes.h |
| 2 | QML-aware Object ID Generation and Tree Serialization | 43e6a1a | object_id.cpp |

## What Was Built

### QML Inspector (qml_inspector.h/.cpp)
- `QmlItemInfo` struct: isQmlItem, qmlId, qmlFile, shortTypeName
- `inspectQmlItem()`: extracts QML metadata via QQmlContext::nameForObject() and QQmlEngine::contextForObject()
- `stripQmlPrefix()`: removes "QQuick" prefix from class names (e.g., "QQuickRectangle" -> "Rectangle")
- `isQmlItem()`: quick check via qobject_cast<QQuickItem*>
- Inline stub fallbacks when QTPILOT_HAS_QML is not defined

### QML-Aware Object ID Generation (object_id.cpp)
- Priority order for QQuickItems: QML id > objectName > text > shortTypeName
- QML id replaces className in hierarchical paths when available
- Anonymous QML items use short type name instead of full QQuick-prefixed name
- serializeObjectInfo() and serializeTreeRecursive() include QML metadata fields

### CMake Changes
- `find_package(Qt6 COMPONENTS Qml Quick QUIET)` with QTPILOT_HAS_QML flag
- Conditional `target_link_libraries(qtPilot_probe PUBLIC Qt6::Qml Qt6::Quick)`
- `target_compile_definitions(qtPilot_probe PUBLIC QTPILOT_HAS_QML)`
- Same pattern for Qt5 fallback block
- Configuration summary includes QML support status

### Error Codes (error_codes.h)
- QML errors: kQmlNotAvailable (-32080), kQmlContextNotFound (-32081), kNotQmlItem (-32082)
- Model errors: kModelNotFound (-32090), kModelIndexOutOfBounds (-32091), kModelRoleNotFound (-32092), kNotAModel (-32093)

## Decisions Made

1. **Qt Quick as optional dependency** — QTPILOT_HAS_QML compile guard wraps all QML code; inline stub functions return safe defaults (isQmlItem=false) when Qt Quick is not available.

2. **QML id as highest priority in ID generation** — QML ids are human-authored, stable identifiers that should take precedence over objectName for generating hierarchical paths.

3. **Short type names for anonymous QML items** — "Rectangle" instead of "QQuickRectangle" since the QQuick prefix is an internal implementation detail.

## Deviations from Plan

None — plan executed exactly as written.

## Verification Results

- Build: zero errors (MSVC /W4 /WX)
- Tests: 11/11 suites pass, zero regressions
- QTPILOT_HAS_QML detected correctly on system with Qt Quick installed
- Compile guards present in qml_inspector.h, qml_inspector.cpp, object_id.cpp

## Next Phase Readiness

Plan 06-02 (Model/View navigation) can proceed — error codes for Model domain are ready. Plan 06-03 (QML + Model API wiring) depends on both 06-01 and 06-02.
