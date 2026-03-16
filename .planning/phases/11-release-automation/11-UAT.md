---
status: complete
phase: 11-release-automation
source: 11-01-SUMMARY.md, 11-02-SUMMARY.md
started: 2026-02-02T12:00:00Z
updated: 2026-02-02T19:06:00Z
---

## Current Test

[testing complete]

## Tests

### 1. CI workflows have workflow_call trigger
expected: Both ci.yml and ci-patched-qt.yml include `workflow_call:` in their `on:` block, making them callable as reusable workflows
result: pass

### 2. Tag push triggers release workflow
expected: `.github/workflows/release.yml` exists and triggers on `push: tags: ['v*']` — pushing a version tag starts the release pipeline
result: pass

### 3. Release workflow calls both CI pipelines
expected: release.yml has two jobs that call `ci.yml` (8 standard binaries) and `ci-patched-qt.yml` (2 patched binaries) via `uses: ./.github/workflows/...`
result: pass

### 4. Release assets use platform-encoded naming
expected: The release job extracts and renames probe binaries to `qtPilot-probe-{qt_tag}-{platform}.{ext}` format (e.g., `qtPilot-probe-qt6.8-linux-gcc.so`, `qtPilot-probe-qt5.15-patched-windows-msvc.dll`)
result: pass

### 5. SHA256 checksums generated
expected: The release job generates a `SHA256SUMS` file containing checksums for all probe binaries, included as a release asset
result: pass

### 6. Fail-safe on missing binaries
expected: The workflow uses `fail_on_unmatched_files: true` and the extraction script errors if any of the expected probes is missing — preventing incomplete releases
result: pass

### 7. CI builds compile across Qt versions
expected: All 8 standard matrix cells (4 Qt versions x 2 platforms) and 2 patched Qt cells build successfully
result: issue
reported: "Source code uses Qt 6.9-only APIs (QAccessible::BlockQuote, QVariant::typeId, QMetaType::fromName, QMetaMethod::parameterTypeName) and missing #include <QJsonDocument> in probe.cpp. Only qt6.9/windows-msvc passes. All other cells fail at Build step. This is a pre-existing source compatibility problem, not a workflow issue."
severity: blocker

## Summary

total: 7
passed: 6
issues: 1
pending: 0
skipped: 0

## Gaps

- truth: "All 8 standard matrix cells and 2 patched Qt cells build successfully"
  status: failed
  reason: "User reported: Source code uses Qt 6.9-only APIs (QAccessible::BlockQuote, QVariant::typeId, QMetaType::fromName, QMetaMethod::parameterTypeName) and missing #include <QJsonDocument> in probe.cpp. Only qt6.9/windows-msvc passes. All other cells fail at Build step. This is a pre-existing source compatibility problem, not a workflow issue."
  severity: blocker
  test: 7
  root_cause: ""
  artifacts: []
  missing: []
  debug_session: ""
