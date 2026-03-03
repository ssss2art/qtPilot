# Spec: Make QtMcp Launcher Easier to Get Running

## Context

Launching the QtMcp probe requires manually setting `QT_PLUGIN_PATH` and adding Qt's `bin/` to `PATH` before every launch. If you forget, `LoadLibraryW` silently returns NULL and the app runs without any probe — no error tells you what went wrong. This is the #1 friction point for both developers and end users.

## Strategy: Three Layers

### Layer 1: C++ Launcher — Auto-detect & Diagnose

**Files:**
- `src/launcher/qt_env_setup.h` (new)
- `src/launcher/qt_env_setup.cpp` (new)
- `src/launcher/main.cpp` (modify — add `--qt-dir` option, call env setup)
- `src/launcher/injector.h` (modify — add `qtDir` to `LaunchOptions`)
- `src/launcher/CMakeLists.txt` (modify — add new source files)
- `src/shared/process_inject_windows.cpp` (modify — add pre-flight check)

**A) Add `--qt-dir` CLI option**
- Single flag: `--qt-dir C:\Qt\5.15.1\msvc2019_64`
- Derives `QT_PLUGIN_PATH` (`<qt-dir>/plugins`) and PATH entry (`<qt-dir>/bin`) from it
- Highest priority — overrides all auto-detection

**B) Auto-detect Qt environment** (`ensureQtEnvironment()`)
- Called in `main()` after `QCoreApplication` constructed but before `launchWithProbe()`
- Resolution cascade (first valid path wins):
  1. `--qt-dir` CLI flag
  2. Existing `QT_PLUGIN_PATH` / `PATH` env vars (respect user overrides)
  3. `QLibraryInfo::location(PluginsPath)` / `QLibraryInfo::location(BinariesPath)` — the launcher is a Qt app, so this works for build-from-source devs
  4. Scan **target app's directory** for `platforms/qwindows.dll` — handles windeployqt'd apps
  5. Scan launcher's own directory for co-located Qt DLLs and `platforms/`
- Sets env vars via `qputenv()` / `SetEnvironmentVariableW()` before `CreateProcessW`
- Logs what it did: `[qtmcp-launch] Auto-set QT_PLUGIN_PATH=...`

**C) Pre-flight diagnostic check**
- Before remote injection, call `LoadLibraryExW(probeDll, nullptr, 0)` locally (WITH dependency resolution, unlike the existing `DONT_RESOLVE_DLL_REFERENCES` call)
- If it fails, call `GetLastError()` and print actionable message:
  ```
  Error: Probe DLL failed to load locally. Missing dependency.
  GetLastError: 126 (ERROR_MOD_NOT_FOUND)

  Fix: Specify your Qt installation:
    qtmcp-launcher.exe --qt-dir C:\Qt\5.15.1\msvc2019_64 your-app.exe
  ```
- This replaces the current cryptic `Warning: LoadLibraryW returned NULL`

### Layer 2: Python `qtmcp` Tool — Smart Launcher Wrapper

**Files:**
- `python/src/qtmcp/cli.py` (modify — add `--qt-dir` option to `serve`)
- `python/src/qtmcp/server.py` (modify — pass Qt env to subprocess)
- `python/src/qtmcp/qt_env.py` (new — Qt path detection logic)

**A) Add `--qt-dir` option to `qtmcp serve`**
- `qtmcp serve --mode native --target app.exe --qt-dir C:\Qt\5.15.1\msvc2019_64`
- Passed through to the launcher subprocess via `--qt-dir`

**B) Smart environment setup before subprocess launch**
- New module `qt_env.py` with `detect_qt_environment(target_path, qt_dir=None)`
- Detection logic:
  1. If `--qt-dir` provided, use it directly
  2. Scan target app's directory for Qt DLLs → infer Qt prefix
  3. Check if `QT_PLUGIN_PATH` is already set
  4. Look for `platforms/` subdirectory next to target
- Before `asyncio.create_subprocess_exec()` in `server.py`, build an env dict with the detected paths
- Pass the env dict to `create_subprocess_exec(..., env=env)`
- Log: `INFO: Detected Qt at C:\Qt\5.15.1\msvc2019_64 (from target directory)`

### Layer 3: Better Build-Time Defaults

**Files:**
- `src/launcher/CMakeLists.txt` (modify — deploy qwindows.dll, generate sidecar config)
- `tests/CMakeLists.txt` (modify — already deploys qminimal.dll, add qwindows.dll)

**A) Deploy `qwindows.dll` alongside build output**
- Currently only `qminimal.dll` is copied to `build/bin/Release/platforms/`
- Add `qwindows.dll` so the test app and launcher work without external plugins
- This alone would have fixed our session's problem

**B) Generate `qtmcp-launcher.json` sidecar** (optional, low priority)
- CMake `file(GENERATE ...)` writes Qt paths next to the launcher exe
- Launcher reads it as a fallback in the resolution cascade
- Useful for redistributing pre-built binaries

## Implementation Order

1. **Deploy qwindows.dll in CMake** — smallest change, biggest immediate impact
2. **C++ `ensureQtEnvironment()`** — auto-detect via QLibraryInfo + target dir scan
3. **C++ `--qt-dir` option** — explicit override for when auto-detect isn't enough
4. **C++ pre-flight diagnostic** — clear errors instead of silent failure
5. **Python `--qt-dir` passthrough** — smart env setup in `qtmcp serve`

## Verification

1. Build from source, run `qtmcp-launcher.exe --port 9222 --detach qtmcp-test-app.exe` with NO env vars set → probe should inject successfully via QLibraryInfo auto-detection
2. Run with `--qt-dir` pointing to the Qt installation → should work
3. Run with wrong `--qt-dir` → should get clear pre-flight error, not silent failure
4. Run `qtmcp serve --mode native --target qtmcp-test-app.exe` → Python tool handles env setup
5. Run existing tests: `ctest --test-dir build -C Release --output-on-failure`
