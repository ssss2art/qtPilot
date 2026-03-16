# Plan: Simplify Build System

## Context

The current build system has grown to 17+ build-related files with ~1,500 lines of CMake code, vcpkg integration, 4 CI workflows, 6 CMake presets, and vcpkg port directories — far more infrastructure than this early-stage project needs. The goal is to strip it down to the minimum needed to build, test, and integrate locally.

## Files to DELETE

| File/Directory | Reason |
|---|---|
| `vcpkg.json` | Removing vcpkg entirely |
| `ports/` (entire directory) | vcpkg port definitions no longer needed |
| `.github/` (entire directory) | Removing all CI/CD workflows |
| `.ci/` (entire directory) | Patched Qt build patches for CI |
| `package.json` | Node.js dependency (ws library) — not part of C++ build |
| `node_modules/` | Node.js modules |

## Files to REWRITE or SIMPLIFY

### 1. `CMakePresets.json` — Simplify from 6 to 2 presets

Replace the 6 configure + 6 build + 6 test presets (all vcpkg-dependent) with just 2 configure presets: `debug` and `release`. No vcpkg toolchain reference. Platform-agnostic (works on both Windows and Linux).

New structure:
- **Base preset** (hidden): sets binary/install dirs, compile commands export
- **debug**: inherits base, CMAKE_BUILD_TYPE=Debug, tests ON
- **release**: inherits base, CMAKE_BUILD_TYPE=Release, tests ON
- Matching build and test presets for each
- Windows presets inherit and add VS generator + x64 architecture

### 2. `tests/CMakeLists.txt` — Reduce from 654 lines to ~50 lines

The current file has the exact same ~50-line block copy-pasted 13 times (one per test). Every block repeats:
- `if(QT_VERSION_MAJOR EQUAL 6) ... else() ... endif()` for linking
- `target_include_directories`
- `add_test` with working directory
- `set_tests_properties` with environment
- Windows Qt Test DLL copy post-build command

**Fix**: Create a `qtPilot_add_test(NAME sources... [LIBS extra_libs...])` function, then define all 13 tests as one-liners.

### 3. `CMakeLists.txt` (root) — Minor cleanup

- Remove the `nlohmann_json` and `spdlog` find_package blocks (optional deps that added complexity — the codebase uses QJsonDocument and QDebug already)
- Remove clang-tidy option and detection (linting was a CI concern)
- Remove the windeployqt discovery + `qtpilot_deploy_qt()` function (move to a simpler inline approach or keep but simplify)
- Remove versioned install directories (simplify to plain `lib/` and `bin/`)
- Simplify CMake package config generation (keep install of simplified cmake/ helpers, remove version file)
- Keep: Qt5/Qt6 dual detection, compiler warnings, automoc/autouic, subdirectory structure

### 4. `cmake/` directory — SIMPLIFY (keep for `find_package(qtPilot)` support)

Both files are kept so that other projects can integrate via:
```cmake
find_package(qtPilot REQUIRED)
qtPilot_inject_probe(myapp)
```

**`cmake/qtPilotConfig.cmake.in`** — Simplify from 188 lines to ~50 lines:
- Remove versioned subdirectory resolution (`lib/qtpilot/qt6.9/` → just `lib/`)
- Remove debug variant binary detection (no `d` postfix hunting)
- Remove fallback Qt auto-detection (require consumer to find Qt first)
- Keep: imported target `qtPilot::Probe`, include dir setup, Qt dependency linking

**`cmake/qtPilot_inject_probe.cmake`** — Keep as-is (~57 lines)
- Already clean and focused
- Provides the `qtPilot_inject_probe(TARGET)` convenience function
- Handles Windows DLL copy and Linux LD_PRELOAD script generation

### 5. `src/probe/CMakeLists.txt` — Minor cleanup

- Remove optional nlohmann_json and spdlog linking blocks
- Simplify output naming (drop versioned tag from filename — just `qtPilot-probe`)
- Remove windeployqt call

### 6. `src/launcher/CMakeLists.txt` — Minor cleanup

- Simplify output naming (just `qtPilot-launcher`)
- Remove windeployqt call

### 7. `test_app/CMakeLists.txt` — Minor cleanup

- Remove windeployqt call

## Summary of Changes

| Before | After |
|---|---|
| 6 configure + 6 build + 6 test presets | 4 configure + 4 build + 4 test presets (debug/release × platform) |
| vcpkg.json + 2 port dirs | None |
| 4 CI workflows + custom action | None |
| 654-line tests CMakeLists | ~50 lines with helper function |
| 365-line root CMakeLists | ~200 lines |
| cmake/ dir with 2 helper files (245 lines) | Simplified (~100 lines) |
| nlohmann_json + spdlog detection | Removed |
| clang-tidy integration | Removed |
| Versioned install paths | Simple lib/ and bin/ |
| CMake package config export | Simplified (no versioned paths, no debug variants) |

## Execution Order

1. Delete files: `vcpkg.json`, `ports/`, `.github/`, `.ci/`, `package.json`, `node_modules/`
2. Rewrite `CMakePresets.json` (2 platform-agnostic presets + Windows variants)
3. Rewrite `tests/CMakeLists.txt` with `qtPilot_add_test()` function
4. Simplify `CMakeLists.txt` (root) — remove vcpkg refs, optional deps, clang-tidy, package config, versioned install
5. Simplify `cmake/qtPilotConfig.cmake.in` — remove versioned paths, debug variants, fallback Qt detection
6. Simplify `src/probe/CMakeLists.txt` — remove optional dep linking, simplify naming
7. Simplify `src/launcher/CMakeLists.txt` — simplify naming
8. Simplify `test_app/CMakeLists.txt` — remove windeployqt

## Verification

1. Configure: `cmake --preset debug -DQTPILOT_QT_DIR=<path-to-qt>`
2. Build: `cmake --build --preset debug`
3. Test: `ctest --preset debug`
4. Verify all 13 tests still register and pass
5. Verify probe DLL, launcher executable, and test app all build correctly
