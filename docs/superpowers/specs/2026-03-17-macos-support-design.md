# Add macOS Support to qtPilot

## Context

macOS is the only major platform not supported by qtPilot. The CMakeLists.txt explicitly warns "macOS support is not yet implemented" and gates out the launcher build on Apple. Adding macOS CI requires both source code changes (probe init, launcher injection, Qt env detection) and pipeline changes (CI matrix, presets, release packaging). The macOS injection mechanism (DYLD_INSERT_LIBRARIES) is very similar to Linux's LD_PRELOAD, so most new code closely follows existing Linux implementations.

## Files to Modify

**New files:**
- `src/probe/core/probe_init_macos.cpp` -- probe library init (based on probe_init_linux.cpp)
- `src/launcher/injector_macos.cpp` -- DYLD_INSERT_LIBRARIES injection (based on injector_linux.cpp)

**Existing files:**
- `CMakeLists.txt` -- remove macOS warning, enable launcher on Apple, fix arch detection
- `src/probe/CMakeLists.txt` -- add probe_init_macos.cpp source
- `src/launcher/CMakeLists.txt` -- add injector_macos.cpp source
- `src/launcher/main.cpp` -- add .dylib glob pattern in findProbePath()
- `src/launcher/qt_env_setup.cpp` -- macOS framework/dylib detection in 5 functions
- `CMakePresets.json` -- add macos-debug/macos-release presets
- `cmake/qtPilotConfig.cmake.in` -- add .dylib lookup for APPLE
- `cmake/qtPilot_inject_probe.cmake` -- add DYLD_INSERT_LIBRARIES script for APPLE
- `.github/workflows/ci.yml` -- add macOS matrix entries + bundle step
- `.github/workflows/release.yml` -- add macOS platform detection + packaging

## Step 1: Probe init for macOS

Create `src/probe/core/probe_init_macos.cpp` -- nearly identical to `probe_init_linux.cpp`:
- Guard: `#if defined(__APPLE__)`
- Same `__attribute__((constructor))` / `__attribute__((destructor))` pattern
- Same `Q_COREAPP_STARTUP_FUNCTION(qtpilotAutoInit)` deferred init
- Log message mentions DYLD_INSERT_LIBRARIES instead of LD_PRELOAD

Update `src/probe/CMakeLists.txt` line 72-74: add `elseif(APPLE)` clause with `probe_init_macos.cpp`.

## Step 2: Launcher injection for macOS

Create `src/launcher/injector_macos.cpp` -- follows `injector_linux.cpp` pattern:
- Same fork/exec with setenv/execvp
- Uses `DYLD_INSERT_LIBRARIES` instead of `LD_PRELOAD`
- Sets `DYLD_FORCE_FLAT_NAMESPACE=1` for reliable symbol interposition
- Header comment documents SIP limitation (irrelevant for user-built Qt apps)

Update `src/launcher/CMakeLists.txt` line 19-21: add `elseif(APPLE)` clause with `injector_macos.cpp`.

Update `src/launcher/main.cpp` lines 32-39 -- add `.dylib` glob patterns:
```cpp
#ifdef Q_OS_WIN
  const QStringList globPatterns = {"qtPilot-probe*.dll"};
#elif defined(Q_OS_MACOS)
  const QStringList globPatterns = {"libqtPilot-probe*.dylib", "qtPilot-probe*.dylib"};
#else
  const QStringList globPatterns = {"libqtPilot-probe*.so", "qtPilot-probe*.so"};
#endif
```

## Step 3: Qt environment detection for macOS

Update `src/launcher/qt_env_setup.cpp` -- 5 functions need macOS branches:

1. **`hasQtPrefixLayout()`** -- check `lib/QtCore.framework` or `lib/libQt*Core.dylib` (not `bin/Qt*Core.dll`)
2. **`dirContainsQtCore()`** -- check for dylib/framework in the directory
3. **`resolveQtPrefix()`** -- check for `libqcocoa.dylib` platform plugin (not `qwindows.dll`)
4. **`qtPrefixFromPlatformsDir()`** -- same: `libqcocoa.dylib`
5. **`applyEnvironment()`** -- set `DYLD_LIBRARY_PATH` + `QT_PLUGIN_PATH` (macOS libs are in `lib/`, not `bin/`)

## Step 4: CMake build system

**`CMakeLists.txt`:**
- Line 214-216: Remove the macOS warning
- Line 182-189: Add arm64 architecture detection for Apple Silicon (`CMAKE_SYSTEM_PROCESSOR MATCHES "arm64"`)
- Line 279: Change `if(WIN32 OR (UNIX AND NOT APPLE))` to `if(WIN32 OR UNIX)` to include Apple

**`CMakePresets.json`:** Add presets:
- `macos-debug` and `macos-release` with condition `"rhs": "Darwin"`
- Corresponding build and test presets

**`cmake/qtPilotConfig.cmake.in`** line 86-97: Add `elseif(APPLE)` to look for `.dylib` instead of `.so`.

**`cmake/qtPilot_inject_probe.cmake`** line 47-55: Add `elseif(APPLE)` to generate DYLD_INSERT_LIBRARIES script.

## Step 5: CI pipeline

**`.github/workflows/ci.yml`** -- add matrix entries:
```yaml
- { qt: "6.8.0",  os: macos-14, preset: macos-release, platform: macos, compiler: clang, modules: "qtwebsockets" }
- { qt: "6.10.0", os: macos-14, preset: macos-release, platform: macos, compiler: clang, modules: "qtwebsockets" }
```

Starting with Qt 6.8 and 6.10 on ARM64 (macos-14). Qt 5.15 on macOS can be added later if needed (requires macos-13 Intel runner).

Add macOS test app bundle step: copy `.dylib` probe + Qt dylibs + `libqcocoa.dylib` platform plugin + wrapper script with `DYLD_LIBRARY_PATH`.

## Step 6: Release packaging

**`.github/workflows/release.yml`:**
- Platform detection: recognize `macos` from preset name
- Arch detection: default to `arm64` for macOS
- File copying: handle `.dylib` probe and launcher
- Archive: `qtpilot-qt6.8-macos-arm64.tar.gz`

## Verification

1. **Local build check**: `cmake --preset macos-release && cmake --build --preset macos-release` (on a Mac, or verify CMake configure doesn't error)
2. **Unit tests**: `ctest --preset macos-release -LE admin` -- all existing tests should pass with `QT_QPA_PLATFORM=minimal`
3. **CI**: Push to a feature branch and verify the macOS matrix entries build/test successfully
4. **Probe injection**: On a Mac, run `qtPilot-launcher qtPilot-test-app` and verify the probe WebSocket server starts
