# Spec: Qt 5.15.1 Local Dev Support & Admin Elevation

## Context

QtMcp needs two capabilities:

1. **Qt 5.15.1 local build/test** — Developers need to easily build and test against Qt 5.15.1 locally. The compat layer and CMake infrastructure already support Qt 5.15.1+, but there are no developer-facing presets or documented workflows for targeting it specifically.

2. **Admin elevation for injection** — The launcher cannot inject into apps that run as administrator. The current `CreateProcessW` + `CREATE_SUSPENDED` pattern works only at the same privilege level. A new `--run-as-admin` flag is needed.

---

## Part 1: Qt 5.15.1 Local Dev Build/Test

### 1.1 Problem

- CI tests Qt 5.15.2, not 5.15.1 exactly
- No CMake preset exists for Qt 5 builds — developers must manually pass `-DQTMCP_QT_DIR=...`
- Qt 5.15.1 has subtle API differences from 5.15.2 (e.g., `QStringView::toInt()` unavailable, already fixed in c73ba39)
- Private header detection may need fallback scanning for 5.15.1's directory layout

### 1.2 What Already Works

- `src/compat/` handles Qt 5/6 API differences (QMetaType, mouse events, QVariant)
- `CMakeLists.txt` enforces `Qt >= 5.15.1` minimum (lines 167-169)
- Private header fallback scanning handles mismatched version directories (lines 118-148)
- Binary names include Qt version tag: `qtmcp-probe-qt5.15.dll`
- `--qt-version` flag on launcher filters probe by version

### 1.3 Changes Required

#### CMakePresets.json — Add Qt 5 presets

Add two new presets that mirror the existing ones but include a `QTMCP_QT_DIR` cache variable hint:

```json
{
  "name": "qt5-windows-release",
  "displayName": "Qt 5 Windows Release",
  "description": "Windows Release build targeting Qt 5.15.x",
  "inherits": "windows-release",
  "cacheVariables": {
    "QTMCP_QT_DIR": {
      "type": "PATH",
      "value": ""
    }
  }
},
{
  "name": "qt5-release",
  "displayName": "Qt 5 Linux/Mac Release",
  "description": "Release build targeting Qt 5.15.x",
  "inherits": "release",
  "cacheVariables": {
    "QTMCP_QT_DIR": {
      "type": "PATH",
      "value": ""
    }
  }
}
```

Developer usage:
```bash
# Point at your Qt 5.15.1 installation
cmake --preset qt5-windows-release -DQTMCP_QT_DIR=C:/Qt/5.15.1/msvc2019_64
cmake --build --preset qt5-windows-release
ctest --preset qt5-windows-release
```

#### Verification Checklist

When building against Qt 5.15.1, verify:

- [ ] CMake configures successfully and finds Qt 5.15.1
- [ ] Private headers are detected (fallback scanning if needed)
- [ ] Probe DLL compiles and links: `qtmcp-probe-qt5.15.dll`
- [ ] Launcher compiles and links
- [ ] All 13 unit tests pass
- [ ] test_app launches and probe connects via WebSocket
- [ ] No `QStringView::toInt()` or similar Qt 5.15.2+ API usage

---

## Part 2: Admin Elevation (`--run-as-admin`)

### 2.1 Problem

Windows DLL injection via `CreateRemoteThread` requires the injector process to have at least the same privilege level as the target. When a target app requires administrator privileges (via manifest or explicit elevation), the launcher must also be elevated for injection to succeed.

The current launcher has no elevation support — `CreateProcessW` cannot create elevated processes (that requires `ShellExecuteEx` with the `runas` verb), and `ShellExecuteEx` doesn't support `CREATE_SUSPENDED`.

### 2.2 Design: Self-Elevating Launcher

**Approach:** When `--run-as-admin` is passed, the launcher re-launches itself as admin using `ShellExecuteEx` + `runas`. The elevated instance then runs the normal injection flow.

```
User runs:  qtmcp-launch --run-as-admin --port 9222 test_app.exe

  [non-elevated launcher]
       |
       v
  IsUserAnAdmin()? ──yes──> normal CreateProcessW + inject flow
       |
       no
       v
  ShellExecuteEx(runas, "qtmcp-launch",
      "--elevated --port 9222 test_app.exe")
       |
       v
  UAC prompt appears ──denied──> exit(1) with error message
       |
       granted
       v
  [elevated launcher instance]
       |
       v
  normal CreateProcessW(CREATE_SUSPENDED) + inject + resume
```

### 2.3 Implementation Details

#### LaunchOptions — Add flag

```cpp
// src/launcher/injector.h
struct LaunchOptions {
  // ... existing fields ...
  bool runAsAdmin = false;  ///< If true, elevate to admin before launching
};
```

#### main.cpp — Add CLI option and elevation logic

```cpp
QCommandLineOption runAsAdminOption(
    QStringLiteral("run-as-admin"),
    QStringLiteral("Launch target with administrator privileges (Windows only)"));
parser.addOption(runAsAdminOption);

// Internal flag: signals we're already elevated (set by self-relaunch)
QCommandLineOption elevatedOption(
    QStringLiteral("elevated"),
    QStringLiteral("Internal: marks this instance as already elevated"));
elevatedOption.setFlags(QCommandLineOption::HiddenFromHelp);
parser.addOption(elevatedOption);
```

#### Elevation function (Windows-only)

New file: `src/launcher/elevation_windows.cpp`

```cpp
#include <Windows.h>
#include <shellapi.h>

namespace qtmcp {

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
    QString argStr = newArgs.join(QLatin1Char(' '));

    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"runas";
    sei.lpFile = reinterpret_cast<LPCWSTR>(executable.utf16());
    sei.lpParameters = reinterpret_cast<LPCWSTR>(argStr.utf16());
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

}  // namespace qtmcp
```

#### main.cpp — Elevation entry point

Before building `LaunchOptions`, add:

```cpp
#ifdef Q_OS_WIN
if (parser.isSet(runAsAdminOption) && !parser.isSet(elevatedOption)) {
    if (qtmcp::isProcessElevated()) {
        // Already elevated, proceed normally
    } else {
        // Re-launch self as admin
        return qtmcp::relaunchElevated(
            QCoreApplication::applicationFilePath(),
            QCoreApplication::arguments().mid(1));  // skip argv[0]
    }
}
#endif
```

### 2.4 Environment Variable Propagation

`ShellExecuteExW` with `runas` creates a new process with a clean environment -- shell-level variables like `PATH` and `QT_PLUGIN_PATH` are **not** inherited. The launcher solves this by wrapping the elevated call in `cmd.exe /c "set VAR=val && ... && launcher.exe --elevated ..."`, which propagates the following variables across the UAC boundary:

- `PATH` -- required for Qt DLL resolution
- `QT_PLUGIN_PATH` -- required for Qt platform plugins (e.g., `qwindows.dll`)
- `QT_QPA_PLATFORM_PLUGIN_PATH` -- alternative plugin path variable
- `QTMCP_PORT`, `QTMCP_MODE`, `QTMCP_INJECT_CHILDREN`, `QTMCP_ENABLED`, `QTMCP_DISCOVERY_PORT` -- probe configuration

### 2.5 Error Handling

| Scenario | Behavior |
|---|---|
| `--run-as-admin` on Linux | Ignored with warning (Linux uses sudo externally) |
| UAC prompt cancelled | Exit code 1, message "UAC elevation was cancelled by user" |
| Already elevated | Proceed normally (no UAC prompt) |
| `--elevated` without admin | Proceed anyway (user's responsibility) |
| Injection fails after elevation | Same error path as today (TerminateProcess + exit 1) |

### 2.6 Linux Consideration

On Linux, `--run-as-admin` prints a warning and is ignored. Users should use `sudo` or `pkexec` externally:
```bash
sudo qtmcp-launch --port 9222 target_app
```

---

## Part 3: E2E Admin Injection Test

### 3.1 Test: `test_admin_injection`

**File:** `tests/test_admin_injection.cpp`

**Purpose:** Verify the full workflow: launcher elevates, injects probe into test_app, probe WebSocket server responds.

**Gating:** Uses `QSKIP()` when not running elevated. Safe to include in normal `ctest` runs — it simply skips. Run explicitly from an elevated terminal or with `ctest -L admin`.

### 3.2 Test Flow

```cpp
class TestAdminInjection : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        // Skip entire test if not running elevated
#ifdef Q_OS_WIN
        if (!qtmcp::isProcessElevated())
            QSKIP("Test requires administrator privileges - run from elevated terminal");
#else
        if (geteuid() != 0)
            QSKIP("Test requires root privileges - run with sudo");
#endif
        // Locate launcher and test_app from build directory
        // (adjacent to this test executable)
    }

    void testElevatedLaunchAndConnect() {
        // 1. Launch test_app via launcher with --run-as-admin --port 0 --detach
        QProcess launcher;
        launcher.start(launcherPath, {
            "--run-as-admin", "--port", "0", "--detach", "--quiet",
            testAppPath
        });
        QVERIFY(launcher.waitForFinished(15000));
        QCOMPARE(launcher.exitCode(), 0);

        // 2. Discover probe via UDP broadcast (same as production flow)
        //    Wait up to 10 seconds for probe to announce
        QUdpSocket udp;
        udp.bind(QHostAddress::Any, 0);
        // ... listen for QTMCP discovery broadcast ...

        // 3. Connect to probe's WebSocket
        QWebSocket ws;
        ws.open(QUrl(discoveredUrl));
        // ... wait for connected signal ...

        // 4. Send chr_getPageText and verify response
        QJsonObject request;
        request["jsonrpc"] = "2.0";
        request["id"] = 1;
        request["method"] = "chr_getPageText";
        ws.sendTextMessage(QJsonDocument(request).toJson());
        // ... wait for response, verify it contains expected page text ...

        // 5. Cleanup: kill the elevated test_app
    }

    void cleanupTestCase() {
        // Terminate test_app if still running
    }
};
```

### 3.3 CMake Integration

```cmake
# tests/CMakeLists.txt — add at end
qtmcp_add_test(NAME test_admin_injection
    SOURCES test_admin_injection.cpp
    LIBS Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Widgets
         Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::WebSockets
    ENV "QTMCP_ENABLED=0")

# Label so it can be targeted/excluded:  ctest -L admin  or  ctest -LE admin
set_tests_properties(test_admin_injection PROPERTIES LABELS "admin")
```

### 3.4 Running the Test

```bash
# Normal ctest — test auto-skips (not elevated)
ctest --test-dir build -C Release --output-on-failure

# Elevated terminal — run admin tests only
ctest --test-dir build -C Release -L admin --output-on-failure

# Exclude admin tests explicitly
ctest --test-dir build -C Release -LE admin --output-on-failure
```

---

## Implementation Order

| Phase | Work | Files |
|---|---|---|
| **1** | Add `--run-as-admin` / `--elevated` CLI options | `src/launcher/main.cpp` |
| **2** | Implement elevation detection + self-relaunch | `src/launcher/elevation_windows.cpp` (new), `src/launcher/CMakeLists.txt` |
| **3** | Add `runAsAdmin` to `LaunchOptions` | `src/launcher/injector.h` |
| **4** | Wire elevation into launcher main | `src/launcher/main.cpp` |
| **5** | Add Qt 5 CMake presets | `CMakePresets.json` |
| **6** | Write E2E admin test | `tests/test_admin_injection.cpp`, `tests/CMakeLists.txt` |
| **7** | Verify local build with Qt 5.15.1 | Manual verification |
| **8** | Verify admin test from elevated terminal | Manual verification |
| **9** | Update documentation | See Part 4 below |

---

## Part 4: Documentation Updates

### 4.1 `README.md`

- Add `--run-as-admin` to the launcher CLI options table/list
- Mention admin elevation as a supported feature
- Note Qt 5.15.1 as explicitly supported (not just 5.15.x)

### 4.2 `docs/GETTING-STARTED.md`

- Add a section on launching elevated/admin apps:
  ```bash
  # Launch a target app that requires administrator privileges
  qtmcp-launch --run-as-admin --port 9222 MyAdminApp.exe
  ```
- Document the UAC prompt behavior and what happens when cancelled
- Add note that on Linux, use `sudo` instead of `--run-as-admin`

### 4.3 `docs/BUILDING.md`

- Add Qt 5.15.1 local build instructions using the new presets:
  ```bash
  cmake --preset qt5-windows-release -DQTMCP_QT_DIR=C:/Qt/5.15.1/msvc2019_64
  cmake --build --preset qt5-windows-release
  ctest --preset qt5-windows-release
  ```
- Document the `qt5-release` and `qt5-windows-release` presets
- Add a note about where to obtain Qt 5.15.1 (Qt archive, aqtinstall)

### 4.4 `docs/TROUBLESHOOTING.md`

- Add troubleshooting entries for:
  - "Injection fails with access denied" → target may be elevated, use `--run-as-admin`
  - "UAC prompt does not appear" → launcher may already be elevated, or UAC is disabled
  - "Qt 5.15.1 private headers not found" → verify Qt installation path includes `QtCore/5.15.1/QtCore/private/`

### 4.5 `CLAUDE.md`

- Update the `LaunchOptions` struct description to include `runAsAdmin` field
- Add `--run-as-admin` and `--elevated` to the launcher CLI options list
- Add `src/launcher/elevation_windows.cpp` to the repository structure
- Note the `admin` ctest label for filtering E2E admin tests

---

## Out of Scope

- Attaching to already-running elevated processes (future work)
- Auto-detecting target manifest elevation requirements
- CI integration for admin tests (requires self-hosted runners with admin)
- Qt 5.15.1 CI builds (covered by existing ci-patched-qt workflow)
- Linux privilege escalation (users use `sudo` externally)
