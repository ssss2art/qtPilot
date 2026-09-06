# Getting Started with qtPilot

This guide walks you through setting up qtPilot to enable AI assistants to control Qt applications.

## Overview

qtPilot consists of two main components:

1. **The Probe** - A library that runs inside your Qt application and exposes its object
   tree via WebSocket. On desktop it is a shared library (`.dll`, `.so`, `.dylib`) injected
   into an already-built app; on Android and iOS it is a static library (`.a`) linked into a
   development build of your own app (see [MOBILE.md](MOBILE.md))
2. **The MCP Server** - A Python CLI (`qtpilot`) that connects Claude to the probe

```
┌────────────────────────────┐     ┌──────────────────┐     ┌─────────────┐
│  Qt Application            │     │  qtpilot serve   │     │  Claude     │
│  ┌──────────────────────┐  │ WS  │  (MCP Server)    │ MCP │             │
│  │  qtPilot Probe       │◄─┼─────┤                  │◄────┤             │
│  │  injected or linked  │  │     │                  │     │             │
│  └──────────────────────┘  │     │                  │     │             │
└────────────────────────────┘     └──────────────────┘     └─────────────┘
```

## Installation Options

### Option 1: pip install (Recommended)

The easiest way to get started is using the Python package:

```bash
pip install qtpilot
```

Then download the probe for your Qt version:

```bash
# Download probe matching your app's Qt version
qtpilot download-tools --qt-version 6.8

# Other available versions: 5.15, 6.5, 6.8, 6.9
qtpilot download-tools --qt-version 5.15

# Extract into a specific directory
qtpilot download-tools --qt-version 6.8 --output ./tools
```

See [python/README.md](../python/README.md) for complete CLI documentation.

### Option 2: Download Pre-built Binaries

Download probe binaries directly from [GitHub Releases](https://github.com/ssss2art/qtPilot/releases).

Release assets vary by platform and Qt version. Use `qtpilot download-tools` to
select the correct archive and verify its checksum. Typical artifacts include:
- `qtPilot-probe-qt5.15-linux-gcc13.so` / `qtPilot-probe-qt5.15-windows-msvc17.dll`
- `qtPilot-probe-qt6.5-linux-gcc13.so` / `qtPilot-probe-qt6.5-windows-msvc17.dll`
- `qtPilot-probe-qt6.8-linux-gcc13.so` / `qtPilot-probe-qt6.8-windows-msvc17.dll`
- `qtPilot-probe-qt6.9-linux-gcc13.so` / `qtPilot-probe-qt6.9-windows-msvc17.dll`
- `qtPilot-launcher-linux` / `qtPilot-launcher-windows.exe`

macOS arm64 is supported when built from source, but it is not included in every
published release. Check the release assets before relying on `download-tools`,
and see [MACOS.md](MACOS.md) for build, injection, permissions, and signing details.

### Option 3: Build from Source

See [BUILDING.md](BUILDING.md) for instructions on compiling qtPilot yourself.

## Choosing Your Qt Version

The probe must match your target application's Qt major.minor version. To check what Qt version an application uses:

**Windows:**
```powershell
# Look for Qt DLLs in the application directory
dir "C:\path\to\app" | findstr Qt
# Qt6Core.dll = Qt 6.x, Qt5Core.dll = Qt 5.x
```

**Linux:**
```bash
# Check linked libraries
ldd /path/to/app | grep -i qt
# libQt6Core.so.6 = Qt 6.x, libQt5Core.so.5 = Qt 5.x
```

**macOS:**
```bash
otool -L /path/to/App.app/Contents/MacOS/App | grep -i Qt
```

Available probe versions:
| Qt Version | Probe Name | Default Compiler | Notes |
|------------|------------|-----------------|-------|
| Qt 5.15.x | `qt5.15` | gcc13 / msvc17 | For Qt 5 applications |
| Qt 6.5.x | `qt6.5` | gcc13 / msvc17 | For Qt 6.5 applications |
| Qt 6.8.x | `qt6.8` | gcc13 / msvc17 | For Qt 6.8 applications |
| Qt 6.9.x | `qt6.9` | gcc13 / msvc17 | For Qt 6.9 applications |

The probe must match your application's Qt major.minor version.

## Running Your Application with the Probe

### Method 1: Using `qtpilot serve --target` (Recommended)

The simplest approach - let `qtpilot` handle probe injection automatically:

```bash
# Windows
qtpilot serve --mode native --target "C:\path\to\your-app.exe"

# With explicit Qt path (if auto-detection fails)
qtpilot serve --mode native --target "C:\path\to\your-app.exe" --qt-dir "C:\Qt\5.15.1\msvc2019_64"

# Linux
qtpilot serve --mode native --target /path/to/your-app

# macOS (.app bundles and executable paths are both accepted)
qtpilot serve --mode native --target /path/to/YourApp.app
```

This automatically:
1. Locates the correct probe for your platform
2. Detects the Qt installation and sets up `PATH` / `QT_PLUGIN_PATH` (or use `--qt-dir` to specify)
3. Launches the application with the probe loaded
4. Starts the MCP server

### Method 2: Using `qtPilot-launcher` Directly

For more control, use the launcher directly.

The launcher auto-detects your Qt installation and sets `PATH` and `QT_PLUGIN_PATH` automatically. If auto-detection fails, use `--qt-dir` to point at your Qt installation:

**Windows:**
```powershell
# Auto-detect Qt (works when built from source — uses build-time Qt prefix)
qtPilot-launcher.exe your-app.exe

# Explicit Qt path (if auto-detect fails)
qtPilot-launcher.exe --qt-dir C:\Qt\5.15.1\msvc2019_64 your-app.exe

# --qt-dir is smart — you can point at bin/, plugins/, or the prefix itself
qtPilot-launcher.exe --qt-dir C:\Qt\5.15.1\msvc2019_64\bin your-app.exe
```

You can also set `QT_PLUGIN_PATH` manually if you prefer — the launcher respects existing env vars and won't override them.

**Linux:**
```bash
# LD_PRELOAD-based injection
LD_PRELOAD=/path/to/libqtpilot.so ./your-app arg1 arg2
```

To automatically inject the probe into child processes spawned by the target:
```bash
qtPilot-launcher.exe --port 0 --inject-children your-app.exe
```

#### Pre-flight Diagnostics

If the probe DLL can't load (missing Qt DLLs), the launcher catches this **before** injection and prints an actionable error:

```
[injector] ERROR: Probe DLL failed pre-flight dependency check.
[injector]   Cause: The specified module could not be found. (error 126)
[injector] This usually means Qt runtime DLLs are not on PATH.
[injector] Fix: specify your Qt installation:
[injector]   qtPilot-launcher.exe --qt-dir C:\Qt\6.8.0\msvc2022_64 your-app.exe
```

#### Launching Elevated (Administrator) Apps

There are two ways to launch with admin privileges:

**Option A: From an already-elevated terminal (Recommended)**

Open an Administrator PowerShell or Command Prompt and use `--elevated`:

```powershell
# Launch with --elevated (tells the launcher it's already running as admin)
.\qtPilot-launcher.exe --elevated --inject-children --port 0 .\your-app.exe
```

This is the recommended approach because:
- All launcher output (injection logs, errors) is visible in your terminal
- No transient `cmd.exe` window that closes immediately
- Qt auto-detection works the same as non-elevated launches

**Option B: Using `--run-as-admin` (auto-elevation via UAC)**

```cmd
qtPilot-launcher.exe --run-as-admin --port 9222 MyAdminApp.exe
```

This triggers a Windows UAC prompt. Once approved, the launcher re-launches itself elevated via `ShellExecuteEx` + `cmd.exe`. The elevated `cmd.exe` window closes when the launcher exits, so **injection logs are not visible**.

The launcher automatically forwards `PATH`, `QT_PLUGIN_PATH`, and all `QTPILOT_*` environment variables across the UAC elevation boundary. The `--qt-dir` flag is also forwarded, and the build-time Qt prefix is compiled into the launcher, so Qt auto-detection works across elevation too.

On Linux, use `sudo` instead:
```bash
sudo qtPilot-launcher --port 9222 /path/to/admin-app
```

Then start the MCP server separately:
```bash
qtpilot serve --mode native --ws-url ws://localhost:9222
```

### Method 3: CMake Integration (Link into Your Project)

If you build qtPilot from source, you can link the probe directly into your CMake project. This is useful during development when you always want the probe available.

**1. Build and install qtPilot:**

```bash
cd qtPilot
cmake --preset release -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.0/gcc_64
cmake --build --preset release
cmake --install build/release --prefix /opt/qtpilot
```

On Windows:
```powershell
cmake --preset windows-release -DCMAKE_PREFIX_PATH="C:\Qt\6.8.0\msvc2022_64"
cmake --build --preset windows-release
cmake --install build/windows-release --prefix C:\qtpilot
```

**2. In your project's CMakeLists.txt:**

```cmake
find_package(Qt6 COMPONENTS Core Widgets REQUIRED)
find_package(qtPilot REQUIRED)

add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE Qt6::Core Qt6::Widgets)
qtPilot_inject_probe(myapp)
```

**3. Configure your project with both Qt and qtPilot in the prefix path:**

```bash
cmake -B build -DCMAKE_PREFIX_PATH="/path/to/Qt/6.8.0/gcc_64;/opt/qtpilot"
cmake --build build
```

On Windows:
```powershell
cmake -B build -DCMAKE_PREFIX_PATH="C:\Qt\6.8.0\msvc2022_64;C:\qtpilot"
cmake --build build
```

`qtPilot_inject_probe()` handles the platform details automatically:
- **Windows (shared):** Copies the probe DLL next to your executable after each build
- **Linux (shared):** Generates a helper script (`qtpilot-preload-myapp.sh`) that launches your app with `LD_PRELOAD` set
- **Mobile / Static (`QTPILOT_PROBE_STATIC`):** Links the static archive (`libqtPilot-probe.a`) with whole-archive force loading automatically into the target executable (see [MOBILE.md](MOBILE.md))

To run with the probe on Linux (shared), use the generated script:
```bash
./build/qtpilot-preload-myapp.sh
```

On Windows (shared), the probe DLL is already next to your exe, so just run your app normally.
On mobile or static builds, run your app normally on the device or emulator.

**4. Start the MCP server separately:**

```bash
# Desktop
qtpilot serve --mode native --ws-url ws://localhost:9222

# Mobile (forward port over USB first: adb forward tcp:9222 tcp:9222 or iproxy 9222 9222)
qtpilot serve --mode native --ws-url ws://localhost:9222
```

The `qtPilotConfig.cmake` package auto-detects your project's Qt version (Qt5 or Qt6) and static/shared configuration, resolving to the matching probe binary.

### Environment Variables

The probe reads these environment variables at startup:

| Variable | Default | Description |
|----------|---------|-------------|
| `QTPILOT_PORT` | `9222` | WebSocket server port (use `0` for auto-assignment) |
| `QTPILOT_MODE` | `all` | API mode: `native`, `chrome`, `computer_use`, or `all` |
| `QTPILOT_INJECT_CHILDREN` | unset | Set to `1` to inject probe into child processes |
| `QTPILOT_ENABLED` | unset | Set to `0` to disable the probe |
| `QTPILOT_BIND_ADDRESS` | `any` | Network exposure. `any` (default) or `loopback`. See [Network exposure](#network-exposure) |

The MCP server (the Python side) reads one more:

| Variable | Default | Description |
|----------|---------|-------------|
| `QTPILOT_CALL_TIMEOUT` | `30` | Seconds to wait for a probe response before failing the call. `0` disables the deadline |

### Network exposure

**The probe listens on all interfaces by default**, and announces itself over UDP
discovery, so an instrumented application is reachable from another machine on
the same network without any configuration.

That default is deliberate. Driving applications that run somewhere other than
the machine you are working from is a first-class use of qtPilot, and discovery
is how those instances are found at all — a probe bound to loopback is invisible
to everything except its own host.

#### What that means

`qt.methods.invoke` calls arbitrary slots on the host application, and **the
probe does not authenticate its clients**. Any host that can reach the port can
run code inside the instrumented process. The probe says so once at startup
rather than leaving you to discover it.

Treat an instrumented application the way you would treat any process with a
debug port open: run it on a network you control, and prefer not to leave one
running on a shared network when you are not using it. Authentication is tracked
as R7 in [`observability-testability-gaps.md`](observability-testability-gaps.md)
and does not exist yet.

#### Restricting to one machine

When everything is on the same host — local development, CI, a single-machine
test run — narrow it:

```bash
QTPILOT_BIND_ADDRESS=loopback ./your-app
```

Discovery announcements follow the bind, so a loopback-bound probe announces to
loopback only rather than advertising itself to the network.

An unrecognised value restricts to loopback rather than falling back to the
default. Setting this variable at all means you are trying to narrow the probe,
and a typo silently resolving to "wide open" would discard that intent — a
refused connection is something you can see and fix.

### Deployment topologies

What the default setup supports, and what each shape needs:

| Topology | Reachability | Notes |
|----------|--------------|-------|
| **One app, same machine** | Works as-is | `QTPILOT_BIND_ADDRESS=loopback` is worth setting; nothing is lost |
| **One app, another machine on the LAN** | Works as-is | Connect by address, or let UDP discovery find it |
| **Several apps across several machines** | Works as-is | Each instance announces itself; enumerate them and connect by address. See the concurrency note below |
| **App on a mobile device over USB** | Works as-is | `adb forward` / `iproxy` terminate on the device's loopback, so this works under either setting |
| **App on a mobile device over Wi-Fi** | Works as-is | Reachable by device address, same as any other host |
| **Anything across a routed boundary** | Not supported | UDP discovery is broadcast-scoped and does not cross subnets; connect by address explicitly |

**Concurrency note.** Each probe currently accepts **one client at a time**, and
a stale client blocks a new connection. Enumerating many instances and connecting
to them in turn works today; holding sessions to several at once does not. That
limit is the server, not the network setting — tracked as R6 in
[`observability-testability-gaps.md`](observability-testability-gaps.md).

Example:
```bash
# Linux
QTPILOT_PORT=9999 QTPILOT_MODE=native LD_PRELOAD=/path/to/libqtpilot.so ./your-app

# Windows (via qtPilot-launcher)
set QTPILOT_PORT=9999
set QTPILOT_MODE=native
qtPilot-launcher.exe your-app.exe
```

## Connecting to Claude

### Claude Desktop

Add to your `claude_desktop_config.json`:

**Windows:** `%APPDATA%\Claude\claude_desktop_config.json`
**macOS:** `~/Library/Application Support/Claude/claude_desktop_config.json`

```json
{
  "mcpServers": {
    "qtpilot": {
      "command": "qtpilot",
      "args": ["serve", "--mode", "native", "--target", "C:\\path\\to\\your-app.exe"]
    }
  }
}
```

For a mobile or remote device:

```json
{
  "mcpServers": {
    "qtpilot": {
      "command": "qtpilot",
      "args": ["serve", "--mode", "native", "--ws-url", "ws://localhost:9222"]
    }
  }
}
```

### Claude Code

```bash
# Desktop (auto-launch)
claude mcp add --transport stdio qtpilot -- qtpilot serve --mode native --target /path/to/your-app

# Mobile / Remote probe (connect to forwarded port or device IP)
claude mcp add --transport stdio qtpilot -- qtpilot serve --mode native --ws-url ws://localhost:9222
```

### Verifying the Connection

1. Start your Qt application with the probe loaded
2. Check that the probe started: look for `[qtPilot] Probe initialized` in stderr
3. Connect to Claude and ask it to list available tools
4. Try a simple command: "Take a screenshot of the Qt application"

## Choosing an API Mode

qtPilot supports three focused API modes plus an aggregate `all` mode. Ten
`qtpilot_*` session, logging, and recording tools are present in every mode.

### Native Mode (`--mode native`)
Exposes 27 `qt_*` tools plus the 10 shared tools (37 total). Use this for:
- Test automation
- Deep inspection of widget properties
- Signal/slot monitoring
- Programmatic UI control

```bash
qtpilot serve --mode native --target /path/to/app
```

### Computer Use Mode (`--mode cu`)
Exposes 13 `cu_*` tools plus the 10 shared tools (23 total). Use this for:
- Visual tasks
- Custom widgets without accessibility info
- Games or canvas-based UIs

```bash
qtpilot serve --mode cu --target /path/to/app
```

### Chrome Mode (`--mode chrome`)
Exposes 8 `chr_*` tools plus the 10 shared tools (18 total). Use this for:
- Form filling
- Semantic element selection
- When you want Claude to "see" the UI like a web page

```bash
qtpilot serve --mode chrome --target /path/to/app
```

### All Modes (`--mode all`)
Exposes all 58 tools. Useful for exploration and mixed workflows.

```bash
qtpilot serve --mode all --target /path/to/app
```

## Next Steps

- [MCP Tooling](MCP-TOOLS.md) - Current tool surface and inspection workflow
- [API Modes Deep Dive](../qtPilot-compatibility-modes.md) - Detailed mode comparisons
- [Building from Source](BUILDING.md) - Compile qtPilot yourself
- [Troubleshooting](TROUBLESHOOTING.md) - Common issues and solutions
