# Automating Complex Multi-Process Desktop Applications — Guide

This guide demonstrates how to automate and introspect a complex, multi-process Qt desktop console application using [qtPilot](https://github.com/ssss2art/qtPilot) MCP tools.

## Architecture & Shape Overview

- **Application Architecture:** Multi-process desktop suite (launcher process, main UI desk process, background server process, critical engine process, and I/O processor service).
- **UI Technology:** Hybrid Qt desktop UI combining `QWidget` toolbars and menus with embedded `QQuickWidget` / `QQuickOffScreenWindow` QML content areas.
- **Process Model:** The launcher process spawns multiple child processes with administrator elevation, requiring child injection (`--inject-children`) and cross-process probe discovery.

## Setup & Injection

### 1. Install qtPilot

```bash
pip install qtpilot
```

### 2. Download the probe

Download the probe matching your target architecture (e.g., Win32 x86 for 32-bit legacy applications or x64 for 64-bit builds):

```bash
qtpilot download-tools --qt-version 5.15 --arch x86 --release v0.1.4 --output "%USERPROFILE%/.qtpilot-x86"
```

### 3. Launch with Probe Injection

Launch the target executable with the launcher probe attached:

```bash
cmd /c "set PATH=C:\app\bin;%PATH% && C:\Users\developer\.qtpilot-x86\qtPilot-launcher.exe C:\app\bin\app-launcher.exe --probe C:\Users\developer\.qtpilot-x86\qtPilot-probe.dll --port 9222 --qt-version 5.15 --inject-children --run-as-admin --detach"
```

Key flags:

- `--run-as-admin` — Required when the target launcher requires elevated privileges (UAC).
- `--inject-children` — Propagates probe injection into all spawned child processes.
- `--detach` — Launcher detaches after injection, leaving the application running.

### 4. Verify Connectivity

```bash
# Check probe port is listening
netstat -an | grep :9222

# Ping the probe
python -c "import asyncio,json,websockets; asyncio.run((lambda: websockets.connect('ws://localhost:9222').__aenter__())())"
```

### 5. Configure MCP Client

Add the MCP server configuration to your project:

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

## Multi-Process Probe Discovery

When the target launcher spawns child processes, each child process with an injected probe opens a probe WebSocket server on an auto-assigned port and announces itself via LAN/local broadcast.

### Discovering Running Probes

Use `qtpilot_list_probes()` to discover all active probe instances:

| Process | Description | Default / Port |
|---------|-------------|----------------|
| `app-launcher` | Primary launcher window | Port 9222 |
| `app-desktop` | Main workspace & desk UI | Auto-assigned port |
| `app-server` | Backend data server | Auto-assigned port |
| `app-core` | Core engine service | Auto-assigned port |
| `app-processor` | I/O processor service | Auto-assigned port |

### Switching Between Processes

To control a specific child process (such as the main desktop UI), connect using its WebSocket URL:

```python
qtpilot_connect_probe(ws_url="ws://127.0.0.1:<desktop-port>")
```

## Interacting with the Desktop UI

The application's main interface is a hybrid of QWidgets and QML:

- **Toolbar & menus:** Standard QWidgets (`QToolButton`, `QPushButton`, custom popups).
- **Workspace areas:** QML / Qt Quick items rendered within `QQuickWidget` or `QQuickWindow`.

### Interacting with QWidget Controls

For QWidget controls, use standard `qt_ui_click` or `qt_methods_invoke`:

```python
# Direct click via UI simulation:
qt_ui_click(objectId="MainWindow/ToolBar/ConnectButton")

# Or direct slot invocation:
qt_methods_invoke(objectId="MainWindow/ToolBar/ConnectButton", method="click")
```

### Interacting with Menus and Popups

Dynamic popup menus are created on-demand when clicking toolbar buttons:

1. Click the menu anchor button to open the popup:

   ```python
   qt_methods_invoke(objectId="MainWindow/DirectoryBar/MenuButton", method="click")
   ```

2. Click the target item within the newly created popup:

   ```python
   qt_methods_invoke(objectId="MainWindow/DirectoryBar/MenuButton/Popup/PreferencesItem", method="click")
   ```

### Virtual Control Panels & Keypads

Complex console apps often provide virtual front panels with dedicated keypads:

```python
# Sequence keypad button clicks for commands:
qt_ui_click(objectId="frontPanel/Keypad/Key_1")
qt_ui_click(objectId="frontPanel/Keypad/Key_Thru")
qt_ui_click(objectId="frontPanel/Keypad/Key_10")
qt_ui_click(objectId="frontPanel/Keypad/Key_At")
qt_ui_click(objectId="frontPanel/Keypad/Key_Full")
qt_ui_click(objectId="frontPanel/Keypad/Key_Enter")
```

## Best Practices & Automation Tips

1. **Multi-Window Environments:** When an application has multiple top-level screens or secondary monitor windows, search by window object name or inspect using `qt_objects_tree()` to identify the correct window handle (`MainWindow~1`, `MainWindow~2`).
2. **Text Input Fields:** For search boxes, form inputs, and editors, prefer `qt_properties_set(objectId="...", name="text", value="...")` over raw keystroke synthesis for deterministic, atomic updates.
3. **Child Process Lifecycles:** When automated workflows close or restart sub-processes, re-run `qtpilot_list_probes()` to detect newly spawned process instances.
4. **Deterministic Replay:** Record multi-step interactions using `qtpilot_log_start(level=2)` and replay them with `qtpilot replay` for automated regression testing across releases.
