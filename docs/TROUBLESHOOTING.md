# Troubleshooting qtPilot

This guide covers common issues and their solutions when using qtPilot.

## Probe Not Loading

### Symptoms
- Application starts but no `[qtPilot]` messages in stderr
- `qtpilot serve` can't connect to the probe
- No WebSocket server on expected port

### Solutions

#### Qt Version Mismatch

The probe must match your application's Qt major.minor version exactly.

**Check your app's Qt version:**
```bash
# Linux
ldd /path/to/app | grep -i qt

# Windows PowerShell
Get-ChildItem "C:\path\to\app" | Where-Object { $_.Name -match "Qt.*\.dll" }
```

**Download the correct probe:**
```bash
# For Qt 6.8
qtpilot download-probe --qt-version 6.8

# For Qt 5.15
qtpilot download-probe --qt-version 5.15-patched
```

#### Windows: DLL Not Found

The `qtpilot.dll` must be discoverable by Windows DLL search order.

**Solutions:**
1. Use `qtpilot serve --target` which handles paths automatically
2. Put the probe DLL in the same directory as the target app
3. Add the probe directory to PATH

**Verify the DLL can be found:**
```powershell
# Check if dependencies are satisfied
where.exe qtPilot_probe.dll
```

#### Linux: LD_PRELOAD Issues

**Check the preload works:**
```bash
LD_PRELOAD=/path/to/libqtpilot.so ldd /path/to/app
# Should show libqtpilot.so in the list
```

**Common issues:**
- Wrong architecture (32-bit vs 64-bit)
- Missing Qt dependencies for the probe itself
- SELinux or AppArmor blocking preload

**Debug with:**
```bash
LD_DEBUG=libs LD_PRELOAD=/path/to/libqtpilot.so ./app 2>&1 | grep qtpilot
```

#### Probe Loads but Doesn't Initialize

The probe defers initialization until `QCoreApplication` exists. If the app crashes early:

```bash
# Enable verbose stderr output
QTPILOT_PORT=9222 ./your-app 2>&1 | grep qtPilot
```

Look for:
- `[qtPilot] Probe singleton created` - DLL loaded successfully
- `[qtPilot] Object hooks installed` - Full initialization completed
- `[qtPilot] ERROR:` - Initialization failed

## Connection Issues

### Symptoms
- `qtpilot serve` times out connecting
- "Connection refused" errors
- Probe loads but no WebSocket connection

### Solutions

#### Port Already in Use

Check if something else is using the port:

```bash
# Linux
ss -tlnp | grep 9222
netstat -tlnp | grep 9222

# Windows
netstat -ano | findstr 9222
```

**Change the port:**
```bash
# Set via environment variable before launching app
QTPILOT_PORT=9999 ./your-app

# Connect to the new port
qtpilot serve --ws-url ws://localhost:9999
```

#### Firewall Blocking WebSocket

On Windows, ensure the probe can accept connections:

1. Check Windows Firewall settings
2. Add an exception for the port (9222 by default)
3. Or temporarily disable firewall for testing

On Linux, check iptables/nftables/firewalld rules.

#### Wrong WebSocket URL

Ensure the URL matches:
```bash
# Default URL
qtpilot serve --ws-url ws://localhost:9222

# If using a different port
qtpilot serve --ws-url ws://localhost:9999

# If connecting from a different machine
qtpilot serve --ws-url ws://192.168.1.100:9222
```

#### Probe on Different Host

By default, the probe binds to `localhost` only. For remote connections, this is a security feature. Run the MCP server on the same machine as the Qt application.

## Claude Not Seeing Tools

### Symptoms
- Claude says "I don't have access to qtPilot tools"
- No tools appear in Claude's tool list
- MCP server starts but Claude can't use it

### Solutions

#### Check MCP Server Configuration

**Claude Desktop:** Verify `claude_desktop_config.json`:
```json
{
  "mcpServers": {
    "qtpilot": {
      "command": "qtpilot",
      "args": ["serve", "--mode", "native", "--target", "/path/to/app"]
    }
  }
}
```

**Common mistakes:**
- Typo in "mcpServers" (note the capital S)
- Wrong path to target application
- Missing `qtpilot` in PATH

**Claude Code:** Verify with:
```bash
claude mcp list
```

#### Verify Probe is Running

Before connecting Claude, confirm the probe works:

1. Start the app with probe: `qtpilot serve --mode native --target /path/to/app`
2. Check for `[qtPilot] Probe initialized` in output
3. In another terminal, test WebSocket: `wscat -c ws://localhost:9222`

#### Mode Mismatch

If using a specific mode, ensure tools match expectations:

- `--mode native` provides Qt-specific tools like `get_object_tree`
- `--mode chrome` provides web-like tools like `read_page`
- `--mode cu` provides Computer Use tools like `screenshot`
- `--mode all` provides all tools

## Qt Version Detection

### Determining App's Qt Version

**From the binary:**
```bash
# Linux
strings /path/to/app | grep "Qt version"
objdump -p /path/to/app | grep NEEDED | grep -i qt

# Windows
strings "C:\path\to\app.exe" | findstr "Qt version"
```

**From running application:**
```bash
# If app has About dialog, check there
# Or look at loaded libraries:

# Linux
cat /proc/$(pgrep app-name)/maps | grep -i qt

# Windows (use Process Explorer or similar)
```

### Available Probe Versions

| Your App Uses | Download | Default Compiler |
|---------------|----------|-----------------|
| Qt 6.5, 6.6, 6.7, 6.8 | `--qt-version 6.8` | gcc13 (Linux), msvc17 (Windows) |
| Qt 5.15.x | `--qt-version 5.15-patched` | gcc13 (Linux), msvc17 (Windows) |
| Qt 5.12 and earlier | Not supported | - |

## Platform-Specific Issues

### Windows

#### Visual C++ Runtime Missing

The probe requires the Visual C++ Redistributable. Install from:
https://aka.ms/vs/17/release/vc_redist.x64.exe

**Symptoms:**
- "VCRUNTIME140.dll not found"
- "MSVCP140.dll not found"

#### Qt DLLs Not Found

If using a standalone probe (not via `qtpilot serve --target`), Qt DLLs must be available.

**Solutions:**
1. Run `windeployqt` on the probe DLL
2. Ensure Qt bin directory is in PATH
3. Copy Qt DLLs next to the probe

#### UAC/Elevation Issues

If the target app requires elevation (runs as administrator), the launcher must also be elevated for injection to succeed.

**Recommended: Use an admin terminal with `--elevated`**

Open an Administrator PowerShell, set environment variables, and run directly:

```powershell
$env:QT_PLUGIN_PATH = "C:\Qt\5.15.1\msvc2019_64\plugins"
$env:PATH = "C:\path\to\build\bin\Release;C:\Qt\5.15.1\msvc2019_64\bin;" + $env:PATH
.\qtPilot-launcher.exe --elevated --inject-children --port 0 .\your-app.exe
```

This gives you full visibility into injection logs and errors.

**Alternative: Use `--run-as-admin` for auto-elevation**
```cmd
qtpilot-launch.exe --run-as-admin your-app.exe
```

This triggers a UAC prompt automatically. If you cancel the UAC prompt, the launcher exits with code 1 and prints an error message. Note that the elevated process runs in a transient `cmd.exe` window — injection logs are not visible.

**If UAC prompt does not appear:**
- The launcher may already be elevated (running from an admin terminal)
- UAC may be disabled in Windows settings
- Group Policy may be suppressing the prompt

#### DLL Injection Fails (`LoadLibraryW returned NULL`)

This is the most common issue when launching elevated. The probe DLL is injected into the suspended target process via `CreateRemoteThread` + `LoadLibraryW`. If the probe's dependencies (Qt DLLs) cannot be found by the target process's DLL search order, `LoadLibraryW` returns NULL and the probe silently fails to load.

**Symptoms:**
- Launcher output shows `Warning: LoadLibraryW returned NULL (DLL load may have failed)`
- `Warning: Could not find qtPilot-probe-qt5.15.dll in remote process modules`
- Target app starts but no probe is active (no WebSocket listener, no UDP discovery)
- Process exit code `0xC0000139` (`STATUS_ENTRYPOINT_NOT_FOUND`) if the app crashes

**Solution: Ensure Qt DLLs are on PATH**

The suspended process inherits `PATH` from its parent. Add both the probe directory and the Qt `bin` directory:

```powershell
# PowerShell (admin terminal)
$env:PATH = "E:\path\to\build\bin\Release;C:\Qt\5.15.1\msvc2019_64\bin;" + $env:PATH
.\qtPilot-launcher.exe --elevated --port 0 .\your-app.exe
```

```cmd
:: cmd.exe (admin terminal)
set PATH=E:\path\to\build\bin\Release;C:\Qt\5.15.1\msvc2019_64\bin;%PATH%
qtPilot-launcher.exe --elevated --port 0 your-app.exe
```

**Why this happens:**
When `LoadLibraryW` loads the probe DLL in the remote process, it resolves the probe's import table (Qt5Core.dll, Qt5Network.dll, etc.) using the standard Windows DLL search order. If the process was created suspended and hasn't initialized its own search paths yet, only `PATH` and the executable's directory are searched. If Qt DLLs aren't in either location, the load fails.

**Environment variables and `--run-as-admin` elevation:**
The launcher automatically forwards `PATH`, `QT_PLUGIN_PATH`, `QT_QPA_PLATFORM_PLUGIN_PATH`, and all `QTPILOT_*` variables across the UAC boundary. Ensure these are set **before** running the launcher:
```cmd
set QT_PLUGIN_PATH=C:\Qt\5.15.1\msvc2019_64\plugins
set PATH=C:\Qt\5.15.1\msvc2019_64\bin;%PATH%
qtpilot-launch.exe --run-as-admin your-app.exe
```

### Linux

#### LD_PRELOAD Caveats

Some applications clear `LD_PRELOAD` or use `setuid`. This can prevent probe loading.

**Workarounds:**
- Launch the app differently (avoid sudo, avoid setuid wrappers)
- Modify the app's launch script to preserve LD_PRELOAD

#### Library Path Issues

If the probe can't find Qt libraries:
```bash
export LD_LIBRARY_PATH=/path/to/Qt/6.8.0/gcc_64/lib:$LD_LIBRARY_PATH
LD_PRELOAD=/path/to/libqtpilot.so ./your-app
```

#### Wayland vs X11

qtPilot should work with both, but some features (especially screenshots) may behave differently. Test with:
```bash
QT_QPA_PLATFORM=xcb ./your-app  # Force X11
QT_QPA_PLATFORM=wayland ./your-app  # Force Wayland
```

## Runtime Errors

### "QCoreApplication not created yet"

The probe tried to initialize before the app created its `QCoreApplication`. This usually resolves automatically, but if it persists:

1. Ensure the app creates `QCoreApplication` early
2. Check for static initialization issues in the app

### "Failed to start WebSocket server"

The port is likely in use. Check with:
```bash
# Linux
ss -tlnp | grep 9222

# Windows
netstat -ano | findstr 9222
```

Kill the conflicting process or use a different port:
```bash
QTPILOT_PORT=9999 ./your-app
```

### Crash on Startup

Enable debug output:
```bash
# Linux
QTPILOT_PORT=9222 gdb -ex run -ex bt ./your-app

# Windows (with Visual Studio)
devenv /debugexe your-app.exe
```

Common causes:
- Qt version mismatch between probe and app
- Missing dependencies
- Incompatible compiler flags (debug vs release)

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

## Getting Help

If you're still stuck:

1. **Check existing issues:** https://github.com/ssss2art/qtPilot/issues
2. **Open a new issue** with:
   - Operating system and version
   - Qt version (app and probe)
   - Full error messages/stack traces
   - Steps to reproduce

## Debug Information to Collect

When reporting issues, include:

```bash
# System info
uname -a  # Linux
systeminfo  # Windows

# Qt version
qmake --version
# or
/path/to/Qt/bin/qmake --version

# Probe loading attempt
QTPILOT_PORT=9222 LD_PRELOAD=/path/to/probe ./app 2>&1 | head -50

# ldd output (Linux)
ldd /path/to/libqtpilot.so
ldd /path/to/your-app

# Dependencies (Windows)
dumpbin /dependents qtPilot_probe.dll
```
