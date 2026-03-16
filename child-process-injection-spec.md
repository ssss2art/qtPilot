# Spec: Child Process Probe Injection

## Status

| Phase | Description | Status |
|-------|-------------|--------|
| Phase 1 | Port 0 Auto-Assignment (Foundation) | **DONE** — committed `76ed8b0` |
| Phase 2 | Windows Child Process Injection (Detours) | **DONE** — committed `6f8c920` |
| Phase 3 | CLI / Public API (`--inject-children`) | **DONE** — committed `6f8c920` |

### Phase 1 Verification (2026-02-25)

- [x] **Port 0 test:** Launched with `--port 0`, got ephemeral port 56317, visible in `qtpilot_list_probes`
- [x] **Two probes test:** Two instances with `--port 0` got ports 56317 and 56321, both discovered
- [x] **All 13 unit tests pass** (Release build, `ctest`)

### Phase 2 & 3 Verification (2026-02-25)

- [x] **Build:** Clean with `/W4 /WX` (zero errors, zero warnings)
- [x] **All 13 unit tests pass** (Release build, `ctest`)
- [x] **Launcher `--inject-children`:** Flag appears in `--help`, startup message shows `Inject children: yes`
- [x] **Child injection E2E:** Launched test app with `--inject-children --port 0`, clicked "Spawn Child Process" button, child appeared in `qtpilot_list_probes` on its own ephemeral port, connected via MCP, read child UI tree, typed into child text boxes — all working
- [x] **Recursive env:** Children inherit `QTPILOT_PORT=0` and `QTPILOT_INJECT_CHILDREN=1`
- [x] **QTPILOT_EXPORT fixes:** Replaced `WINDOWS_EXPORT_ALL_SYMBOLS` with explicit exports on all public APIs

---

## Context

When a Qt app is launched with the qtPilot probe and that app spawns child processes (e.g., via `QProcess`), those children don't have the probe. On Linux, `LD_PRELOAD` naturally propagates but all children try to bind the same `QTPILOT_PORT`, causing failures. On Windows, there's no propagation at all. This plan adds automatic probe injection into child processes.

## Approach Summary

1. **Port 0 auto-assignment** — Let the OS pick an ephemeral port. Discovery broadcaster already announces the actual port, so the Python MCP server discovers all probes automatically.
2. **Windows: Detours hook on `CreateProcessW`** — When enabled, the probe hooks `CreateProcessW` to inject itself into child processes before they run.
3. **Linux: Just fix port conflicts** — `LD_PRELOAD` already propagates; setting `QTPILOT_PORT=0` after init is the only fix needed.
4. **Opt-in via `--inject-children`** — Controlled by `QTPILOT_INJECT_CHILDREN=1` env var, exposed as a CLI flag.

---

## Phase 1: Port 0 Auto-Assignment (Foundation) — DONE

### 1.1 WebSocketServer — support port 0

**File:** `src/probe/transport/websocket_server.cpp`

In `start()`, after `m_server->listen()` succeeds, read back the actual port:
```cpp
if (m_port == 0) {
    m_port = m_server->serverPort();
}
```

### 1.2 Probe — update m_port after server starts

**File:** `src/probe/core/probe.cpp`

After `m_server->start()` succeeds (line ~147), sync the port:
```cpp
m_port = m_server->port();  // actual port if 0 was requested
```

The `DiscoveryBroadcaster` is already created after this (line 160) with `m_port`, so it will get the actual port.

### 1.3 Probe — set QTPILOT_PORT=0 for children

**File:** `src/probe/core/probe.cpp`

After successful server start, override the env var so any child process auto-assigns:
```cpp
qputenv("QTPILOT_PORT", "0");
```

This is the **entire Linux fix** — `LD_PRELOAD` propagates naturally, and now children get their own port.

### 1.4 Launcher — allow port 0 in CLI validation

**File:** `src/launcher/main.cpp` (line 149)

Change `portValue <= 0` to `portValue < 0`:
```cpp
if (!portOk || portValue < 0 || portValue > 65535) {
```

---

## Phase 2: Windows Child Process Injection (Detours)

### 2.1 Add Microsoft Detours dependency

**File:** `CMakeLists.txt` (top-level)

Use FetchContent to pull Detours from `microsoft/Detours` on GitHub, or vendor the ~4 source files into `third_party/detours/`.

**File:** `src/probe/CMakeLists.txt`

Conditionally link Detours on Windows:
```cmake
if(WIN32)
    target_link_libraries(qtPilot-probe PRIVATE detours)
    target_sources(qtPilot-probe PRIVATE core/child_injector_windows.cpp)
endif()
```

### 2.2 Save probe DLL path in DllMain

**File:** `src/probe/core/probe_init_windows.cpp`

Add a global `wchar_t g_probeDllPath[MAX_PATH]` and populate it in `DllMain` `DLL_PROCESS_ATTACH`:
```cpp
GetModuleFileNameW(hModule, g_probeDllPath, MAX_PATH);
```

Expose via `const wchar_t* getProbeDllPath();`

### 2.3 Factor out injection logic into shared utility

Extract the core injection sequence from `src/launcher/injector_windows.cpp` (lines 208-400) into a reusable function:

**New file:** `src/shared/process_inject_windows.h`
```cpp
namespace qtpilot {
bool injectProbeDll(HANDLE hProcess, DWORD processId, const wchar_t* dllPath, bool quiet = true);
}
```

**New file:** `src/shared/process_inject_windows.cpp`

Contains: VirtualAllocEx, WriteProcessMemory, CreateRemoteThread(LoadLibraryW), findRemoteModule, call qtpilotProbeInit — extracted from the existing launcher code.

Both the launcher (`src/launcher/injector_windows.cpp`) and the probe's child injector link against this shared code. Refactor `launchWithProbe()` to call `injectProbeDll()` instead of inlining the injection steps.

**Reusable code from:** `src/launcher/injector_windows.cpp`
- `findRemoteModule()` (lines 125-143)
- `HandleGuard` RAII class (lines 39-71)
- `printWindowsError()` (lines 106-119)
- Injection sequence (lines 208-400)

### 2.4 Create child process hook module

**New file:** `src/probe/core/child_injector_windows.h`
```cpp
namespace qtpilot {
void installChildProcessHook();
void uninstallChildProcessHook();
}
```

**New file:** `src/probe/core/child_injector_windows.cpp`

Core logic:
- `Real_CreateProcessW` — pointer to original function
- `Hooked_CreateProcessW` — forces `CREATE_SUSPENDED`, calls real function, injects probe via `injectProbeDll()`, resumes if needed
- `installChildProcessHook()` — `DetourTransactionBegin/Attach/Commit`
- `uninstallChildProcessHook()` — `DetourTransactionBegin/Detach/Commit`

The environment for children already has `QTPILOT_PORT=0` (set in Phase 1.3) and `QTPILOT_INJECT_CHILDREN=1` (inherited), so probe auto-assigns a port and children of children also get hooked recursively.

### 2.5 Wire hook into Probe lifecycle

**File:** `src/probe/core/probe.cpp`

In `initialize()`, after server starts:
```cpp
#ifdef Q_OS_WIN
qputenv("QTPILOT_PORT", "0");
if (qgetenv("QTPILOT_INJECT_CHILDREN") == "1") {
    installChildProcessHook();
}
#endif
```

In `shutdown()`:
```cpp
#ifdef Q_OS_WIN
uninstallChildProcessHook();
#endif
```

---

## Phase 3: CLI / Public API

### 3.1 LaunchOptions struct

**File:** `src/launcher/injector.h`

Add field:
```cpp
bool injectChildren = false;
```

### 3.2 Launcher CLI

**File:** `src/launcher/main.cpp`

Add `--inject-children` option. When set, add `QTPILOT_INJECT_CHILDREN=1` to the target's environment.

### 3.3 Launcher injector implementations

**File:** `src/launcher/injector_windows.cpp`

Before `CreateProcessW`, if `options.injectChildren`:
```cpp
SetEnvironmentVariableW(L"QTPILOT_INJECT_CHILDREN", L"1");
```

**File:** `src/launcher/injector_linux.cpp`

In the `setenv` block:
```cpp
if (options.injectChildren)
    setenv("QTPILOT_INJECT_CHILDREN", "1", 1);
```

### 3.4 Python CLI (optional follow-up)

**File:** `python/src/qtpilot/cli.py` — Add `--inject-children` flag
**File:** `python/src/qtpilot/server.py` — Pass flag through to launcher

---

## What Needs No Changes

- **Discovery protocol** — already supports multiple probes (keyed by pid + port)
- **Python MCP server** — `qtpilot_list_probes` / `qtpilot_connect_probe` already handle multiple probes
- **JSON-RPC handler, API modes** — per-probe instance, no conflicts

---

## Verification

1. ~~**Port 0 test:** Launch test app with `--port 0`, verify it starts on an ephemeral port and appears in `qtpilot_list_probes`~~ **PASSED**
2. ~~**Two probes test:** Launch two test apps with `--port 0`, verify both appear in discovery with different ports~~ **PASSED**
3. **Child injection (Windows):** Launch test app with `--inject-children --port 0`, have it spawn a child via QProcess, verify child appears in `qtpilot_list_probes` with its own port — *blocked on Phase 2 & 3*
4. **Child injection (Linux):** Same test — `LD_PRELOAD` propagates, child gets `QTPILOT_PORT=0`, appears in discovery — *blocked on Phase 3*
5. **Non-Qt child:** Verify injecting into a non-Qt child doesn't crash (probe loads but doesn't initialize since no QCoreApplication) — *blocked on Phase 2*
6. **Recursive:** Parent spawns child, child spawns grandchild — verify all three appear in discovery — *blocked on Phase 2 & 3*

---

## File Summary

| File | Action |
|------|--------|
| `src/probe/transport/websocket_server.cpp` | Modify — port 0 readback |
| `src/probe/core/probe.cpp` | Modify — port sync, env override, hook wiring |
| `src/probe/core/probe_init_windows.cpp` | Modify — save DLL path |
| `src/launcher/main.cpp` | Modify — allow port 0, add `--inject-children` |
| `src/launcher/injector.h` | Modify — add `injectChildren` field |
| `src/launcher/injector_windows.cpp` | Modify — refactor to use shared inject, set env |
| `src/launcher/injector_linux.cpp` | Modify — set `QTPILOT_INJECT_CHILDREN` env |
| `src/shared/process_inject_windows.h` | **New** — shared injection API |
| `src/shared/process_inject_windows.cpp` | **New** — shared injection impl (extracted from launcher) |
| `src/probe/core/child_injector_windows.h` | **New** — hook API |
| `src/probe/core/child_injector_windows.cpp` | **New** — Detours hook impl |
| `src/probe/CMakeLists.txt` | Modify — add Detours + new sources |
| `CMakeLists.txt` | Modify — FetchContent Detours |
