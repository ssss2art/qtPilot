# Admin Elevation & Qt 5.15.1 Support Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add `--run-as-admin` flag to the launcher for injecting into elevated apps, add Qt 5 CMake presets for local dev, write an E2E test, and update all documentation.

**Architecture:** The launcher self-elevates via `ShellExecuteEx` + `runas` when `--run-as-admin` is passed and the process is not already elevated. The elevated instance uses the existing `CreateProcessW` + `CREATE_SUSPENDED` injection path unchanged. A new E2E test verifies the full workflow but gates on admin privileges via `QSKIP`.

**Tech Stack:** C++17, Qt 5.15/6.x, Win32 API (ShellExecuteEx, TokenElevation), CMake presets, QTest framework

**Spec:** `qt515-admin-elevation-spec.md` in the project root contains the full design.

---

### Task 1: Add elevation header

**Files:**
- Create: `src/launcher/elevation_windows.h`

**Step 1: Create the header file**

```cpp
// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#ifdef Q_OS_WIN

#include <QString>
#include <QStringList>

namespace qtpilot {

/// @brief Check whether the current process is running with elevated (admin) privileges.
/// @return true if the process token has TokenElevation set.
bool isProcessElevated();

/// @brief Re-launch the current executable with administrator privileges via UAC.
///
/// Uses ShellExecuteEx with the "runas" verb. Replaces --run-as-admin with --elevated
/// in the argument list so the elevated instance knows it's already elevated.
/// Blocks until the elevated process exits.
///
/// @param executable Path to the current executable.
/// @param args Command-line arguments (without argv[0]).
/// @return Exit code from the elevated process, or 1 on failure.
int relaunchElevated(const QString& executable, const QStringList& args);

}  // namespace qtpilot

#endif  // Q_OS_WIN
```

**Step 2: Commit**

```bash
git add src/launcher/elevation_windows.h
git commit -m "feat: add elevation_windows.h header for admin support"
```

---

### Task 2: Implement elevation functions

**Files:**
- Create: `src/launcher/elevation_windows.cpp`

**Step 1: Create the implementation file**

```cpp
// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

// Windows-only elevation support for the launcher.
// Enables self-elevation via UAC when --run-as-admin is passed.

#include "elevation_windows.h"

#ifdef Q_OS_WIN

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <shellapi.h>

#include <cstdio>

namespace qtpilot {

bool isProcessElevated() {
  BOOL elevated = FALSE;
  HANDLE token = nullptr;
  if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
    TOKEN_ELEVATION elev = {};
    DWORD size = 0;
    if (GetTokenInformation(token, TokenElevation, &elev, sizeof(elev), &size)) {
      elevated = elev.TokenIsElevated;
    }
    CloseHandle(token);
  }
  return elevated != FALSE;
}

int relaunchElevated(const QString& executable, const QStringList& args) {
  // Build argument string: replace --run-as-admin with --elevated
  QStringList newArgs = args;
  newArgs.removeAll(QStringLiteral("--run-as-admin"));
  newArgs.prepend(QStringLiteral("--elevated"));

  // Quote arguments that contain spaces
  QStringList quotedArgs;
  for (const QString& arg : newArgs) {
    if (arg.contains(QLatin1Char(' ')) || arg.contains(QLatin1Char('"'))) {
      QString escaped = arg;
      escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
      quotedArgs.append(QStringLiteral("\"%1\"").arg(escaped));
    } else {
      quotedArgs.append(arg);
    }
  }
  QString argStr = quotedArgs.join(QLatin1Char(' '));

  std::wstring exeW = executable.toStdWString();
  std::wstring argsW = argStr.toStdWString();

  SHELLEXECUTEINFOW sei = {};
  sei.cbSize = sizeof(sei);
  sei.lpVerb = L"runas";
  sei.lpFile = exeW.c_str();
  sei.lpParameters = argsW.c_str();
  sei.nShow = SW_SHOWNORMAL;
  sei.fMask = SEE_MASK_NOCLOSEPROCESS;

  if (!ShellExecuteExW(&sei)) {
    DWORD err = GetLastError();
    if (err == ERROR_CANCELLED) {
      fprintf(stderr, "Error: UAC elevation was cancelled by user\n");
    } else {
      fprintf(stderr, "Error: ShellExecuteEx failed (error %lu)\n", err);
    }
    return 1;
  }

  // Wait for elevated instance to finish
  WaitForSingleObject(sei.hProcess, INFINITE);
  DWORD exitCode = 0;
  GetExitCodeProcess(sei.hProcess, &exitCode);
  CloseHandle(sei.hProcess);
  return static_cast<int>(exitCode);
}

}  // namespace qtpilot

#endif  // Q_OS_WIN
```

**Step 2: Commit**

```bash
git add src/launcher/elevation_windows.cpp
git commit -m "feat: implement isProcessElevated() and relaunchElevated()"
```

---

### Task 3: Add runAsAdmin to LaunchOptions

**Files:**
- Modify: `src/launcher/injector.h:12-20`

**Step 1: Add the field**

Add `runAsAdmin` after `injectChildren` in the `LaunchOptions` struct:

```cpp
struct LaunchOptions {
  QString targetExecutable;     ///< Path to the target executable
  QStringList targetArgs;       ///< Command line arguments for the target
  QString probePath;            ///< Path to the probe library (DLL/SO)
  quint16 port = 9222;          ///< WebSocket port for the probe server
  bool detach = false;          ///< If true, run in background (don't wait)
  bool quiet = false;           ///< If true, suppress startup messages
  bool injectChildren = false;  ///< If true, inject probe into child processes
  bool runAsAdmin = false;      ///< If true, elevate to admin before launching (Windows only)
};
```

**Step 2: Commit**

```bash
git add src/launcher/injector.h
git commit -m "feat: add runAsAdmin field to LaunchOptions"
```

---

### Task 4: Wire elevation into launcher CMakeLists.txt

**Files:**
- Modify: `src/launcher/CMakeLists.txt:5-7` and `9-11`

**Step 1: Add elevation source and header to the build**

In `src/launcher/CMakeLists.txt`, add `elevation_windows.h` to LAUNCHER_HEADERS and the source file conditionally.

Change the LAUNCHER_HEADERS block to:

```cmake
set(LAUNCHER_HEADERS
    injector.h
)
```

becomes:

```cmake
set(LAUNCHER_HEADERS
    injector.h
    elevation_windows.h
)
```

And in the `if(WIN32)` block on line 14-15, add the elevation source:

```cmake
if(WIN32)
    list(APPEND LAUNCHER_SOURCES injector_windows.cpp elevation_windows.cpp)
```

**Step 2: Add shell32 link dependency**

`ShellExecuteExW` requires linking `shell32.lib`. Add after the existing `qtPilot_shared` link on line 40:

```cmake
if(WIN32)
    target_link_libraries(qtPilot_launcher PRIVATE qtPilot_shared shell32)
endif()
```

**Step 3: Build to verify compilation**

Run:
```bash
cmake --build build --config Release --target qtPilot_launcher
```
Expected: Compiles without errors.

**Step 4: Commit**

```bash
git add src/launcher/CMakeLists.txt
git commit -m "build: add elevation_windows to launcher build, link shell32"
```

---

### Task 5: Wire elevation into launcher main.cpp

**Files:**
- Modify: `src/launcher/main.cpp`

**Step 1: Add include at top (after existing includes, line 16)**

Add after `#include <QFileInfo>`:

```cpp
#ifdef Q_OS_WIN
#include "elevation_windows.h"
#endif
```

**Step 2: Add CLI options (after injectChildrenOption, line 125)**

Add after `parser.addOption(injectChildrenOption);`:

```cpp
  QCommandLineOption runAsAdminOption(
      QStringLiteral("run-as-admin"),
      QStringLiteral("Launch target with administrator privileges (Windows only)"));
  parser.addOption(runAsAdminOption);

  // Internal flag: signals this instance was re-launched elevated
  QCommandLineOption elevatedOption(QStringLiteral("elevated"),
                                     QStringLiteral("Internal: already elevated"));
  elevatedOption.setFlags(QCommandLineOption::HiddenFromHelp);
  parser.addOption(elevatedOption);
```

**Step 3: Add elevation check (after `parser.process(app);` on line 135, before positional args check)**

Insert between `parser.process(app);` and the positional args check:

```cpp
  // Handle --run-as-admin: self-elevate if not already elevated
#ifdef Q_OS_WIN
  if (parser.isSet(runAsAdminOption) && !parser.isSet(elevatedOption)) {
    if (!qtpilot::isProcessElevated()) {
      // Re-launch self as admin, forwarding all arguments
      return qtpilot::relaunchElevated(QCoreApplication::applicationFilePath(),
                                     QCoreApplication::arguments().mid(1));
    }
    // Already elevated — fall through to normal launch
  }
#else
  if (parser.isSet(runAsAdminOption)) {
    fprintf(stderr, "Warning: --run-as-admin is only supported on Windows. "
                    "Use sudo on Linux.\n");
  }
#endif
```

**Step 4: Set runAsAdmin on LaunchOptions (after line 150 where injectChildren is set)**

After `options.injectChildren = parser.isSet(injectChildrenOption);` add:

```cpp
  options.runAsAdmin = parser.isSet(runAsAdminOption) || parser.isSet(elevatedOption);
```

**Step 5: Update the startup message (line 217-219)**

Change the existing fprintf that prints Port/Detach/Inject to also show admin:

```cpp
    fprintf(stderr, "[qtpilot-launch] Port: %u, Detach: %s, Inject children: %s, Admin: %s\n",
            static_cast<unsigned>(options.port), options.detach ? "yes" : "no",
            options.injectChildren ? "yes" : "no", options.runAsAdmin ? "yes" : "no");
```

**Step 6: Build and verify**

Run:
```bash
cmake --build build --config Release --target qtPilot_launcher
```
Expected: Compiles without errors.

**Step 7: Commit**

```bash
git add src/launcher/main.cpp
git commit -m "feat: wire --run-as-admin and --elevated into launcher CLI"
```

---

### Task 6: Add Qt 5 CMake presets

**Files:**
- Modify: `CMakePresets.json`

**Step 1: Add Qt 5 configure presets**

Add two new entries at the end of the `configurePresets` array (before the closing `]`), after the `windows-x86-release` preset:

```json
    ,
    {
      "name": "qt5-release",
      "displayName": "Qt 5 Linux/Mac Release",
      "description": "Release build targeting Qt 5.15.x",
      "inherits": "release",
      "cacheVariables": {
        "QTPILOT_QT_DIR": {
          "type": "PATH",
          "value": ""
        }
      }
    },
    {
      "name": "qt5-windows-release",
      "displayName": "Qt 5 Windows Release",
      "description": "Windows release build targeting Qt 5.15.x",
      "inherits": "windows-release",
      "cacheVariables": {
        "QTPILOT_QT_DIR": {
          "type": "PATH",
          "value": ""
        }
      }
    }
```

**Step 2: Add corresponding build presets**

Add at the end of the `buildPresets` array:

```json
    ,
    {
      "name": "qt5-release",
      "configurePreset": "qt5-release"
    },
    {
      "name": "qt5-windows-release",
      "configurePreset": "qt5-windows-release",
      "configuration": "Release"
    }
```

**Step 3: Add corresponding test presets**

Add at the end of the `testPresets` array:

```json
    ,
    {
      "name": "qt5-release",
      "configurePreset": "qt5-release",
      "output": {
        "outputOnFailure": true
      }
    },
    {
      "name": "qt5-windows-release",
      "configurePreset": "qt5-windows-release",
      "configuration": "Release",
      "output": {
        "outputOnFailure": true
      }
    }
```

**Step 4: Verify presets parse correctly**

Run:
```bash
cmake --list-presets
```
Expected: Shows `qt5-windows-release` in the list (on Windows).

**Step 5: Commit**

```bash
git add CMakePresets.json
git commit -m "build: add Qt 5 CMake presets for local development"
```

---

### Task 7: Update the E2E admin test

**Files:**
- Modify: `tests/test_admin_injection.cpp` (already created, needs review/update)
- Modify: `tests/CMakeLists.txt` (already updated)

The test file `tests/test_admin_injection.cpp` and CMakeLists.txt entry already exist from the brainstorming phase. Verify they compile.

**Step 1: Build the test**

Run:
```bash
cmake --build build --config Release --target test_admin_injection
```
Expected: Compiles without errors. If there are compile errors, fix them.

**Step 2: Run the test (non-elevated — should QSKIP)**

Run:
```bash
ctest --test-dir build -C Release -R test_admin_injection --output-on-failure
```
Expected: Test passes (skipped) with message "Test requires administrator privileges".

**Step 3: Commit any fixes**

```bash
git add tests/test_admin_injection.cpp tests/CMakeLists.txt
git commit -m "test: add E2E admin injection test with QSKIP gating"
```

---

### Task 8: Build everything and run existing tests

**Step 1: Full rebuild**

Run:
```bash
cmake -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="<your-qt-path>"
cmake --build build --config Release
```
Expected: All targets build without errors.

**Step 2: Run all non-admin tests**

Run:
```bash
ctest --test-dir build -C Release -LE admin --output-on-failure
```
Expected: All 13 existing tests pass. The admin test is excluded by label.

**Step 3: Commit if any fixups were needed**

---

### Task 9: Update README.md

**Files:**
- Modify: `README.md:69-78`

**Step 1: Add admin elevation to the Features list**

In the Features section (around line 77), add a new bullet after "Child process injection":

```markdown
- **Admin elevation** - `--run-as-admin` launches target apps with administrator privileges on Windows (auto-elevates via UAC)
```

**Step 2: Update Qt version note**

On line 75, change:
```markdown
- **Works with Qt 5.15 and Qt 6.x** applications
```
to:
```markdown
- **Works with Qt 5.15.1+ and Qt 6.5+** applications
```

**Step 3: Commit**

```bash
git add README.md
git commit -m "docs: add admin elevation and Qt 5.15.1 to README features"
```

---

### Task 10: Update docs/GETTING-STARTED.md

**Files:**
- Modify: `docs/GETTING-STARTED.md`

**Step 1: Add admin elevation section**

After the "Method 2: Using `qtpilot-launch` Directly" section (after line 131), add a new subsection:

```markdown
#### Launching Elevated (Administrator) Apps

If the target application requires administrator privileges, use `--run-as-admin`:

```cmd
qtpilot-launch.exe --run-as-admin --port 9222 MyAdminApp.exe
```

This triggers a Windows UAC prompt. Once approved, the launcher elevates itself and injects the probe normally. If the UAC prompt is cancelled, the launcher exits with code 1.

On Linux, use `sudo` instead:
```bash
sudo qtpilot-launch --port 9222 /path/to/admin-app
```
```

**Step 2: Commit**

```bash
git add docs/GETTING-STARTED.md
git commit -m "docs: add admin elevation usage to Getting Started guide"
```

---

### Task 11: Update docs/BUILDING.md

**Files:**
- Modify: `docs/BUILDING.md`

**Step 1: Add Qt 5 presets to the preset table (line 68-74)**

Add two rows to the "Available Presets" table:

```markdown
| `qt5-release` | Linux | Release | Qt 5.15.x targeted build |
| `qt5-windows-release` | Windows | Release | Qt 5.15.x targeted build |
```

**Step 2: Add Qt 5.15.1 build section (after "Building for Multiple Qt Versions" section, around line 240)**

Add a new subsection:

```markdown
### Building for Qt 5.15.1

Use the dedicated Qt 5 presets to build against Qt 5.15.1 locally:

**Windows:**
```powershell
cmake --preset qt5-windows-release -DQTPILOT_QT_DIR="C:\Qt\5.15.1\msvc2019_64"
cmake --build --preset qt5-windows-release
ctest --preset qt5-windows-release
```

**Linux:**
```bash
cmake --preset qt5-release -DQTPILOT_QT_DIR=/opt/Qt/5.15.1/gcc_64
cmake --build --preset qt5-release
ctest --preset qt5-release
```

Qt 5.15.1 is available from the [Qt Archive](https://download.qt.io/archive/qt/5.15/5.15.1/) or via [aqtinstall](https://github.com/miurahr/aqtinstall):
```bash
pip install aqtinstall
aqt install-qt linux desktop 5.15.1
```
```

**Step 3: Add admin test section (after "Running Tests" section, around line 178)**

Add:

```markdown
### Running Admin Tests

The `test_admin_injection` test verifies probe injection into elevated processes. It auto-skips when not running as administrator.

```bash
# Run only admin tests (from elevated terminal)
ctest --test-dir build -C Release -L admin --output-on-failure

# Exclude admin tests
ctest --test-dir build -C Release -LE admin --output-on-failure
```
```

**Step 4: Commit**

```bash
git add docs/BUILDING.md
git commit -m "docs: add Qt 5.15.1 build workflow and admin test instructions"
```

---

### Task 12: Update docs/TROUBLESHOOTING.md

**Files:**
- Modify: `docs/TROUBLESHOOTING.md`

**Step 1: Replace the existing UAC/Elevation section (lines 247-252)**

The existing section at line 247-252 says to use `Start-Process -Verb RunAs`. Replace it with the new `--run-as-admin` approach:

```markdown
#### UAC/Elevation Issues

If the target app requires elevation (runs as administrator), the launcher must also be elevated for injection to succeed.

**Use the `--run-as-admin` flag:**
```cmd
qtpilot-launch.exe --run-as-admin your-app.exe
```

This triggers a UAC prompt automatically. If you cancel the UAC prompt, the launcher exits with code 1 and prints an error message.

**If UAC prompt does not appear:**
- The launcher may already be elevated (running from an admin terminal)
- UAC may be disabled in Windows settings
- Group Policy may be suppressing the prompt

**If injection still fails after elevation:**
- Verify the probe DLL matches the target's Qt version
- Check that the target app itself loads successfully when run as admin
```

**Step 2: Add Qt 5.15.1 private headers troubleshooting**

After the "Qt6::CorePrivate not found" section equivalent, or in the "Platform-Specific Issues" section, find a suitable location and add:

```markdown
#### Qt 5.15.1 Private Headers Not Found

When building against Qt 5.15.1, CMake may fail to find private headers if the directory structure doesn't match expectations.

**Verify private headers exist:**
```bash
ls /path/to/Qt/5.15.1/gcc_64/include/QtCore/5.15.1/QtCore/private/qhooks_p.h
```

If the version subdirectory doesn't match (e.g., headers are under `5.15.0` instead of `5.15.1`), the CMake fallback scanner should handle this automatically. If not, set the Qt path explicitly:
```bash
cmake -B build -DQTPILOT_QT_DIR=/path/to/Qt/5.15.1/gcc_64
```
```

**Step 3: Commit**

```bash
git add docs/TROUBLESHOOTING.md
git commit -m "docs: update troubleshooting for --run-as-admin and Qt 5.15.1"
```

---

### Task 13: Update CLAUDE.md

**Files:**
- Modify: `CLAUDE.md`

**Step 1: Add elevation_windows files to repo structure**

In the `src/launcher/` section of the Repository Structure, add:
```
│   ├── elevation_windows.h      # isProcessElevated(), relaunchElevated() API
│   ├── elevation_windows.cpp    # Win32 UAC elevation via ShellExecuteEx
```

**Step 2: Update launcher CLI options list**

In the "Key Implementation Notes" or wherever CLI options are documented, add `--run-as-admin` and `--elevated` to the launcher's option list.

**Step 3: Update LaunchOptions struct description**

Add `runAsAdmin` to the struct fields listing.

**Step 4: Add admin test label note**

In the Testing Guidelines section, add:
```
### Test Labels
- `admin` — Tests requiring administrator privileges. Run with `ctest -L admin`. Auto-skipped when not elevated.
```

**Step 5: Commit**

```bash
git add CLAUDE.md
git commit -m "docs: update CLAUDE.md with elevation support and admin test labels"
```

---

## Summary

| Task | Description | Files |
|------|-------------|-------|
| 1 | Elevation header | `src/launcher/elevation_windows.h` (create) |
| 2 | Elevation implementation | `src/launcher/elevation_windows.cpp` (create) |
| 3 | LaunchOptions flag | `src/launcher/injector.h` (modify) |
| 4 | CMakeLists.txt build wiring | `src/launcher/CMakeLists.txt` (modify) |
| 5 | Main.cpp CLI + elevation logic | `src/launcher/main.cpp` (modify) |
| 6 | Qt 5 CMake presets | `CMakePresets.json` (modify) |
| 7 | E2E admin test verify/fix | `tests/test_admin_injection.cpp`, `tests/CMakeLists.txt` |
| 8 | Full build + test run | All targets |
| 9 | README.md | Feature list update |
| 10 | GETTING-STARTED.md | Admin elevation usage |
| 11 | BUILDING.md | Qt 5 presets + admin test instructions |
| 12 | TROUBLESHOOTING.md | UAC and Qt 5.15.1 troubleshooting |
| 13 | CLAUDE.md | Repo structure, CLI options, test labels |
