# Getting Started with qtPilot

This guide walks you through setting up qtPilot to enable AI assistants to control Qt applications.

## Overview

qtPilot consists of two main components:

1. **The Probe** - A shared library (`qtpilot.dll`/`libqtpilot.so`) that loads into your Qt application and exposes its object tree via WebSocket
2. **The MCP Server** - A Python CLI (`qtpilot`) that connects Claude to the probe

```
┌────────────────────────────┐     ┌──────────────────┐     ┌─────────────┐
│  Qt Application            │     │  qtpilot serve     │     │  Claude     │
│  ┌──────────────────────┐  │ WS  │  (MCP Server)    │ MCP │             │
│  │  qtPilot Probe         │◄─┼─────┤                  │◄────┤             │
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
qtpilot download-probe --qt-version 6.8

# Other available versions: 5.15, 6.5, 6.8, 6.9
qtpilot download-probe --qt-version 5.15

# Override the default compiler if needed (default: gcc13 on Linux, msvc17 on Windows)
qtpilot download-probe --qt-version 6.8 --compiler gcc14
```

See [python/README.md](../python/README.md) for complete CLI documentation.

### Option 2: Download Pre-built Binaries

Download probe binaries directly from [GitHub Releases](https://github.com/ssss2art/qtPilot/releases).

Each release includes probes for each supported Qt version, for both platforms:
- `qtPilot-probe-qt5.15-linux-gcc13.so` / `qtPilot-probe-qt5.15-windows-msvc17.dll`
- `qtPilot-probe-qt6.5-linux-gcc13.so` / `qtPilot-probe-qt6.5-windows-msvc17.dll`
- `qtPilot-probe-qt6.8-linux-gcc13.so` / `qtPilot-probe-qt6.8-windows-msvc17.dll`
- `qtPilot-probe-qt6.9-linux-gcc13.so` / `qtPilot-probe-qt6.9-windows-msvc17.dll`
- `qtPilot-launcher-linux` / `qtPilot-launcher-windows.exe`

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
```

This automatically:
1. Locates the correct probe for your platform
2. Detects the Qt installation and sets up `PATH` / `QT_PLUGIN_PATH` (or use `--qt-dir` to specify)
3. Launches the application with the probe loaded
4. Starts the MCP server

### Method 2: Using `qtpilot-launch` Directly

For more control, use the launcher directly.

The launcher auto-detects your Qt installation and sets `PATH` and `QT_PLUGIN_PATH` automatically. If auto-detection fails, use `--qt-dir` to point at your Qt installation:

**Windows:**
```powershell
# Auto-detect Qt (works when built from source — uses build-time Qt prefix)
qtpilot-launch.exe your-app.exe

# Explicit Qt path (if auto-detect fails)
qtpilot-launch.exe --qt-dir C:\Qt\5.15.1\msvc2019_64 your-app.exe

# --qt-dir is smart — you can point at bin/, plugins/, or the prefix itself
qtpilot-launch.exe --qt-dir C:\Qt\5.15.1\msvc2019_64\bin your-app.exe
```

You can also set `QT_PLUGIN_PATH` manually if you prefer — the launcher respects existing env vars and won't override them.

**Linux:**
```bash
# LD_PRELOAD-based injection
LD_PRELOAD=/path/to/libqtpilot.so ./your-app arg1 arg2
```

To automatically inject the probe into child processes spawned by the target:
```bash
qtpilot-launch.exe --port 0 --inject-children your-app.exe
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
qtpilot-launch.exe --run-as-admin --port 9222 MyAdminApp.exe
```

This triggers a Windows UAC prompt. Once approved, the launcher re-launches itself elevated via `ShellExecuteEx` + `cmd.exe`. The elevated `cmd.exe` window closes when the launcher exits, so **injection logs are not visible**.

The launcher automatically forwards `PATH`, `QT_PLUGIN_PATH`, and all `QTPILOT_*` environment variables across the UAC elevation boundary. The `--qt-dir` flag is also forwarded, and the build-time Qt prefix is compiled into the launcher, so Qt auto-detection works across elevation too.

On Linux, use `sudo` instead:
```bash
sudo qtpilot-launch --port 9222 /path/to/admin-app
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
- **Windows:** Copies the probe DLL next to your executable after each build
- **Linux:** Generates a helper script (`qtpilot-preload-myapp.sh`) that launches your app with `LD_PRELOAD` set

To run with the probe on Linux, use the generated script:
```bash
./build/qtpilot-preload-myapp.sh
```

On Windows, the probe DLL is already next to your exe, so just run your app normally.

**4. Start the MCP server separately:**

```bash
qtpilot serve --mode native --ws-url ws://localhost:9222
```

The `qtPilotConfig.cmake` package auto-detects your project's Qt version (Qt5 or Qt6) and resolves to the matching probe binary, so the same qtPilot install can work with either.

### Environment Variables

The probe reads these environment variables at startup:

| Variable | Default | Description |
|----------|---------|-------------|
| `QTPILOT_PORT` | `9222` | WebSocket server port (use `0` for auto-assignment) |
| `QTPILOT_MODE` | `all` | API mode: `native`, `chrome`, `computer_use`, or `all` |
| `QTPILOT_INJECT_CHILDREN` | unset | Set to `1` to inject probe into child processes |
| `QTPILOT_ENABLED` | unset | Set to `0` to disable the probe |

Example:
```bash
# Linux
QTPILOT_PORT=9999 QTPILOT_MODE=native LD_PRELOAD=/path/to/libqtpilot.so ./your-app

# Windows (via qtpilot-launch)
set QTPILOT_PORT=9999
set QTPILOT_MODE=native
qtpilot-launch.exe your-app.exe
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

### Claude Code

```bash
claude mcp add --transport stdio qtpilot -- qtpilot serve --mode native --ws-url ws://localhost:9222
```

### Verifying the Connection

1. Start your Qt application with the probe loaded
2. Check that the probe started: look for `[qtPilot] Probe initialized` in stderr
3. Connect to Claude and ask it to list available tools
4. Try a simple command: "Take a screenshot of the Qt application"

## Choosing an API Mode

qtPilot supports three API modes, selectable via `--mode`:

### Native Mode (`--mode native`)
Full Qt object tree introspection. Use this for:
- Test automation
- Deep inspection of widget properties
- Signal/slot monitoring
- Programmatic UI control

```bash
qtpilot serve --mode native --target /path/to/app
```

### Computer Use Mode (`--mode cu`)
Screenshot-based interaction using pixel coordinates. Use this for:
- Visual tasks
- Custom widgets without accessibility info
- Games or canvas-based UIs

```bash
qtpilot serve --mode cu --target /path/to/app
```

### Chrome Mode (`--mode chrome`)
Accessibility tree with element references. Use this for:
- Form filling
- Semantic element selection
- When you want Claude to "see" the UI like a web page

```bash
qtpilot serve --mode chrome --target /path/to/app
```

### All Modes (`--mode all`)
Exposes tools from all three modes. Useful for experimentation.

```bash
qtpilot serve --mode all --target /path/to/app
```

## Next Steps

- [API Reference](../qtPilot-specification.md) - Complete tool and protocol documentation
- [API Modes Deep Dive](../qtPilot-compatibility-modes.md) - Detailed mode comparisons
- [Building from Source](BUILDING.md) - Compile qtPilot yourself
- [Troubleshooting](TROUBLESHOOTING.md) - Common issues and solutions
