---
status: resolved
trigger: "CI build failure: variant_json.cpp still uses Qt6-only APIs when built against Qt 5.15"
created: 2026-02-03T00:00:00Z
updated: 2026-02-03T00:00:00Z
---

## Current Focus

hypothesis: Compat commits exist locally but were never pushed to origin/main
test: Compare git log origin/main vs local main
expecting: Local main has compat commits that origin/main lacks
next_action: Document findings and recommend push

## Symptoms

expected: variant_json.cpp compiles cleanly against Qt 5.15 in CI using compat wrappers
actual: CI errors on Qt 5.15 Windows - `typeId` not member of QVariant, `fromName` not member of QMetaType, `canConvert(QMetaType)` wrong signature
errors:
  - variant_json.cpp(26,28): error C2039: 'typeId': is not a member of 'QVariant'
  - variant_json.cpp(204,37): error C2039: 'fromName': is not a member of 'QMetaType'
  - variant_json.cpp(398,18): error C2664: 'bool QVariant::canConvert(int) const': cannot convert argument 1 from 'QMetaType' to 'int'
reproduction: Run CI on Qt 5.15 Windows build
started: After phase 11.1 was supposed to fix this

## Eliminated

- hypothesis: The compat changes were never made to variant_json.cpp
  evidence: Local file at src/probe/introspection/variant_json.cpp uses value.userType() (line 29), compat::metaTypeIdFromName (line 207), compat::variantCanConvert (line 401), compat::variantConvert (line 402). All three CI-failing APIs are already replaced locally.
  timestamp: 2026-02-03

- hypothesis: There are remaining Qt6-only call sites in other probe .cpp files
  evidence: Grep for `.typeId()`, `QMetaType::fromName`, `canConvert(QMetaType)`, `convert(QMetaType)` across all src/**/*.cpp returns zero matches. Only the compat headers themselves contain these (inside #if QT_VERSION >= 6 blocks), which is correct.
  timestamp: 2026-02-03

## Evidence

- timestamp: 2026-02-03
  checked: git log --oneline origin/main vs local main
  found: |
    origin/main HEAD is at 8194dee (fix(ci): resolve build failures across Qt versions).
    Local main HEAD is at d4fbba4 (test(11.1): complete UAT - 7 passed, 0 issues).
    12 commits on local main are NOT on origin/main:
      d4fbba4 test(11.1): complete UAT - 7 passed, 0 issues
      55dd4ea docs(11.1-02): complete CMake version enforcement and CI matrix plan
      d060fb1 feat(11.1-02): replace Qt 6.2 with Qt 6.5 in CI and release matrices
      55679e1 feat(11.1-02): enforce minimum Qt versions and add deprecation guard
      9e0a890 docs(11.1-01): complete compat headers plan
      d250958 feat(11.1-01): replace Qt6-only API calls with compat layer
      c0fd521 feat(11.1-01): create Qt5/Qt6 compat headers
      20278ef fix(11.1): revise plans based on checker feedback
      2df9ffd docs(11.1): create phase plan
      049be9a docs(11.1): research Qt5/Qt6 source compatibility phase
      5811aa8 docs(11.1): capture phase context for Qt5/Qt6 source compatibility
      ad89c99 test(11): complete UAT - 6 passed, 1 issue
  implication: CI is building from origin/main which lacks ALL phase 11.1 commits including the compat layer

- timestamp: 2026-02-03
  checked: Local variant_json.cpp content
  found: |
    File includes compat/compat_core.h and compat/compat_variant.h (lines 6-7).
    Line 29: uses `value.userType()` (Qt5-safe) instead of `value.typeId()` (Qt6-only).
    Line 207: uses `qtpilot::compat::metaTypeIdFromName()` instead of `QMetaType::fromName()`.
    Lines 401-402: uses `compat::variantCanConvert()` and `compat::variantConvert()` instead of raw Qt6 APIs.
  implication: The compat fixes ARE implemented correctly in local working tree

- timestamp: 2026-02-03
  checked: Compat headers (compat_core.h, compat_variant.h)
  found: |
    Both headers use #if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0) to branch between Qt5 and Qt6 APIs.
    Qt5 fallbacks: QMetaType::type(), v.canConvert(int), v.convert(int), QVariant(int, nullptr)
    Qt6 paths: QMetaType::fromName().id(), v.canConvert(QMetaType), v.convert(QMetaType), QVariant(QMetaType)
  implication: Compat layer is correctly implemented for both Qt versions

- timestamp: 2026-02-03
  checked: Grep across all src/**/*.cpp and src/**/*.h for raw Qt6-only APIs
  found: Zero matches in .cpp files. Only matches in .h files are inside the compat headers themselves (inside Qt6 #if blocks), which is correct and expected.
  implication: No remaining raw Qt6-only call sites exist in local codebase

## Resolution

root_cause: |
  The 12 phase 11.1 commits (c0fd521 through d4fbba4) that create the compat headers and
  replace Qt6-only API calls were never pushed to origin/main. CI is building from origin/main
  at commit 8194dee, which still has the original Qt6-only code. The local codebase is fully
  fixed but the remote is 12 commits behind.

fix: |
  Push local main to origin:
    git push origin main
  This will push all 12 phase 11.1 commits including:
    - c0fd521: compat headers (compat_core.h, compat_variant.h)
    - d250958: replacement of Qt6-only API calls in variant_json.cpp and other files
    - d060fb1/55679e1: CI matrix and CMake version enforcement updates

verification: |
  After push, CI should rebuild against Qt 5.15 and succeed because:
  1. variant_json.cpp now uses value.userType() instead of value.typeId()
  2. compat::metaTypeIdFromName() wraps QMetaType::type() on Qt5
  3. compat::variantCanConvert/Convert() use int-based overloads on Qt5

files_changed: []
