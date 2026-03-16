# Spec: Rebuild CI System for qtPilot

## Context

The current CI system (4 GitHub Actions workflows, 576 lines) is broken and overly complex for this early-stage project. The Python test job references a nonexistent path (`src/mcp_server/` instead of `python/`), all builds depend on vcpkg which adds fragility, and there are CI-only presets that diverge from local development. The CI needs to be rebuilt from scratch to support the plugin spec (`claude-code-plugin-spec.md`), which requires building C++ probe/launcher binaries for multiple Qt versions and platforms, publishing them to GitHub Releases, and publishing the Python package to PyPI.

The build system must be simplified first (per `build-simplification-spec.md`) since the CI depends on it.

---

## Phase 1: Delete old infrastructure

Delete these files/directories in a single commit:

| Delete | Reason |
|---|---|
| `.github/workflows/ci.yml` | Replacing with new simplified workflow |
| `.github/workflows/ci-patched-qt.yml` | Dropping patched Qt 5.15.1 builds |
| `.github/workflows/release.yml` | Replacing with new workflow |
| `.github/actions/` (entire directory) | Custom build-qt action no longer needed |
| `.ci/` (entire directory) | Qt patches for CI |
| `vcpkg.json` | Removing vcpkg entirely |
| `ports/` (entire directory) | vcpkg port definitions |
| `package.json` | Node.js ws library (unused) |
| `node_modules/` | Node.js modules |

**Keep:** `.github/workflows/publish-pypi.yml` (clean and working)

---

## Phase 2: Simplify build system

### 2.1 Rewrite `CMakePresets.json`

Replace 6 vcpkg-dependent presets with 4 simple presets:

- `base` (hidden): sets `binaryDir`, `installDir`, `CMAKE_EXPORT_COMPILE_COMMANDS`
- `debug`: inherits base, Debug, tests ON (Linux condition)
- `release`: inherits base, Release, tests ON (Linux condition)
- `windows-debug`: inherits base, Debug, VS 2022 x64 generator (Windows condition)
- `windows-release`: inherits base, Release, VS 2022 x64 generator (Windows condition)

Plus matching build presets and test presets (4 each). No vcpkg toolchain reference anywhere.

CI uses `release` / `windows-release` directly — no separate CI-only presets.

### 2.2 Simplify root `CMakeLists.txt` (~365 -> ~200 lines)

**Remove:**
- `QTPILOT_ENABLE_CLANG_TIDY` option + clang-tidy block (lines 24, 272-280)
- `QTPILOT_DEPLOY_QT` option + windeployqt discovery + `qtpilot_deploy_qt()` function (lines 172-210)
- `find_package(nlohmann_json)` block (lines 214-221)
- `find_package(spdlog)` block (lines 223-230)
- `write_basic_package_version_file` call (lines 327-331)
- Versioned install paths: change `QTPILOT_INSTALL_LIBDIR` from `lib/qtpilot/${QTPILOT_QT_VERSION_TAG}` to `lib` (line 153)
- References to clang-tidy, nlohmann_json, spdlog in config summary

**Keep:** Qt5/Qt6 dual detection, `QTPILOT_QT_VERSION_TAG`, compiler warnings, automoc, platform definitions, subdirectories, package config generation (simplified), `QTPILOT_QT_DIR` hint

### 2.3 Rewrite `tests/CMakeLists.txt` (~654 -> ~60 lines)

Create a `qtPilot_add_test(NAME src [LIBS ...] [ENV ...])` helper function that handles:
- Qt5/Qt6 conditional linking
- `target_include_directories` to probe source
- `add_test` with working directory
- `set_tests_properties` with environment
- Windows Qt Test DLL copy post-build

Then define all 13 tests as one-liners:
```cmake
qtPilot_add_test(NAME test_jsonrpc SOURCES test_jsonrpc.cpp)
qtPilot_add_test(NAME test_object_registry SOURCES test_object_registry.cpp ENV "QTPILOT_ENABLED=0")
qtPilot_add_test(NAME test_object_id SOURCES test_object_id.cpp
    LIBS Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Widgets
    ENV "QTPILOT_ENABLED=0;QT_QPA_PLATFORM=minimal")
# ... 10 more with same Gui+Widgets+ENV pattern
```

Keep the qminimal.dll deployment block at the top (lines 1-23).

### 2.4 Simplify `src/probe/CMakeLists.txt`

- Remove nlohmann_json conditional linking (lines 141-144)
- Remove spdlog conditional linking (lines 146-149)
- Remove `qtpilot_deploy_qt(qtPilot_probe)` call (line 165)
- **Keep** `OUTPUT_NAME "qtPilot-probe-${QTPILOT_QT_VERSION_TAG}"` (needed for multi-Qt-version releases)

### 2.5 Simplify `src/launcher/CMakeLists.txt`

- Remove `qtpilot_deploy_qt(qtPilot_launcher)` call (line 52)
- Change output name from `qtpilot-launch-${QTPILOT_QT_VERSION_TAG}` to just `qtPilot-launcher` (launcher is not Qt-version-specific per plugin spec)

### 2.6 Simplify `test_app/CMakeLists.txt`

- Remove `qtpilot_deploy_qt(qtPilot_test_app)` call (line 51)

### 2.7 Simplify `cmake/qtPilotConfig.cmake.in` (~188 -> ~80 lines)

- Remove fallback Qt auto-detection block (lines 26-43)
- Remove debug variant binary detection (lines 112-132 Windows, 146-156 Linux)
- Change library path from `lib/qtpilot/${version_tag}` to `lib/`
- Keep: imported target `qtPilot::Probe`, include dirs, Qt dependency linking

### 2.8 Add `[project.optional-dependencies]` to `python/pyproject.toml`

Add dev dependencies so CI can install pytest:
```toml
[project.optional-dependencies]
dev = ["pytest>=7.0"]
```

---

## Phase 3: Write new CI workflows

### 3.1 New `.github/workflows/ci.yml` (~100 lines)

**Triggers:** push to main (src/**, tests/**, CMakeLists.txt, cmake/**, CMakePresets.json, .github/workflows/**), pull_request to main, workflow_dispatch, workflow_call

**Job 1: `lint`** (unchanged)
- ubuntu-24.04, clang-format-action v4.11.0, check `src/`

**Job 2: `build`** (8-cell matrix, simplified)
- Matrix: 4 Qt versions (5.15.2, 6.5.3, 6.8.0, 6.9.0) x 2 platforms (linux-gcc on ubuntu, windows-msvc)
- Uses `release` preset on Linux, `windows-release` on Windows
- No vcpkg — Qt installed via `jurplel/install-qt-action@v4` (with cache)
- Steps: checkout, install-qt, install X11 deps (Linux only), configure (shell:bash, Qt5/Qt6 path conditional), build, test, install, upload artifact
- No separate configure steps per platform — single bash step with Qt path detection:
  ```yaml
  - name: Configure
    shell: bash
    run: |
      if [ -n "$Qt5_DIR" ]; then
        QT_PATH="$Qt5_DIR"
      else
        QT_PATH="$QT_ROOT_DIR"
      fi
      cmake --preset ${{ matrix.preset }} -DCMAKE_PREFIX_PATH="$QT_PATH"
  ```

**Job 3: `python`** (fixed)
- ubuntu-24.04, Python 3.11
- Path: `cd python` (not `src/mcp_server`)
- `pip install -e ".[dev]"` then `pytest -v`
- No `continue-on-error` — tests must pass

**Dropped:** CodeQL job (add back later), patched Qt builds

### 3.2 New `.github/workflows/release.yml` (~75 lines)

**Trigger:** push tags `v*`

**Job 1:** Reuse `ci.yml` as `workflow_call`

**Job 2: `release`**
- Download all 8 artifacts
- Extract/rename probe binaries: `qtPilot-probe-{qt_tag}-{platform}.{dll/so}`
- Extract launcher binaries from qt6.8 cells (one per platform): `qtPilot-launcher-{platform}.{exe/""}`
- Extract .lib import libraries for Windows probes
- Generate SHA256SUMS
- Create GitHub Release via `softprops/action-gh-release@v2`

### 3.3 Keep `.github/workflows/publish-pypi.yml` as-is

No changes needed — it correctly references `python/` directory.

---

## Phase 4: Verification

1. **Local build:** `cmake --preset windows-release -DCMAKE_PREFIX_PATH=<qt-path>` then build, test, install
2. **Check install layout:** `lib/` has probe DLL, `bin/` has launcher, `include/qtpilot/` has headers
3. **Push to feature branch** and verify all 8 CI matrix cells pass
4. **Test release** with a `v0.1.0-rc1` tag to verify artifact collection and GitHub Release creation

---

## Critical files to modify

| File | Action |
|---|---|
| `CMakePresets.json` | Rewrite (remove vcpkg, 4 presets) |
| `CMakeLists.txt` (root) | Simplify (remove vcpkg/optional deps/windeployqt/clang-tidy) |
| `tests/CMakeLists.txt` | Rewrite (helper function, 654->60 lines) |
| `src/probe/CMakeLists.txt` | Remove optional dep linking + deploy call |
| `src/launcher/CMakeLists.txt` | Remove deploy call, simplify output name |
| `test_app/CMakeLists.txt` | Remove deploy call |
| `cmake/qtPilotConfig.cmake.in` | Simplify (flat lib path, no debug variants) |
| `python/pyproject.toml` | Add dev dependencies |
| `.github/workflows/ci.yml` | New file |
| `.github/workflows/release.yml` | New file |

## Summary: Old vs New

| Aspect | Old | New |
|---|---|---|
| Workflow count | 4 | 3 (drop ci-patched-qt.yml) |
| ci.yml lines | 294 | ~100 |
| Matrix cells | 8 standard + 2 patched = 10 | 8 |
| CMake presets | 6 (+ vcpkg) | 4 (no vcpkg) |
| CI-only presets | ci-linux, ci-windows | None (uses release/windows-release) |
| vcpkg dependency | Yes | No |
| CodeQL | Yes | No (add back later) |
| Patched Qt build | Yes (120+ min timeout) | No (add back later) |
| Custom GH Action | build-qt composite (262 lines) | None |
| Python test path | src/mcp_server (broken) | python/ (correct) |
| tests/CMakeLists.txt | 654 lines | ~60 lines |
| Root CMakeLists.txt | 365 lines | ~200 lines |
| Install paths | lib/qtpilot/qt6.8/ (versioned) | lib/ (flat) |
