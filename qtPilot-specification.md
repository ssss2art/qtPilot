# qtPilot Specification v1.0

## Executive Summary

**qtPilot** is a lightweight, MIT-licensed injection library for Qt application introspection and automation. It enables AI assistants (like Claude), test automation frameworks, and debugging tools to inspect and control Qt applications at runtime.

---

## Project Identity

| Attribute | Value |
|-----------|-------|
| **Name** | qtPilot |
| **License** | MIT |
| **Repository** | TBD |
| **Version** | 1.0.0 (MVP) |

---

## Goals & Use Cases

### Primary Use Cases

1. **Test Automation** - Running automated UI tests against Qt applications
2. **AI-Assisted Interaction** - Enabling Claude/LLMs to operate Qt applications via MCP
3. **Debugging/Development** - Inspecting live applications during development

### Target Applications

- Applications you control (own source code)
- Third-party applications (closed source)
- Both Widgets and QML/QtQuick hybrid applications

---

## Platform Support

### MVP (v1.0)

| Platform | Status | Priority |
|----------|--------|----------|
| **Windows** | ✅ Supported | Primary |
| **Linux** | ✅ Supported | Primary |
| **macOS** | ❌ Deferred | v1.1 |

### Qt Version Support

| Version | Status |
|---------|--------|
| **Qt 5.15 LTS** | ✅ Primary target |
| **Qt 6.x** | ✅ Secondary (after 5.15 works) |
| **Qt 5.12 and earlier** | ❌ Not supported |

---

## Architecture

### Component Overview

```
┌─────────────────────────────────────────────────────────────────────────┐
│  TARGET MACHINE                                                         │
│                                                                         │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │  Qt Application                                                   │  │
│  │  ┌─────────────────────────────────────────────────────────────┐  │  │
│  │  │  qtPilot Probe (libqtpilot.so / qtpilot.dll)                      │  │  │
│  │  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────────┐   │  │  │
│  │  │  │ Object       │  │ Introspector │  │ WebSocket Server │   │  │  │
│  │  │  │ Tracker      │  │              │  │ (port via env)   │   │  │  │
│  │  │  └──────────────┘  └──────────────┘  └────────┬─────────┘   │  │  │
│  │  └───────────────────────────────────────────────│─────────────┘  │  │
│  └──────────────────────────────────────────────────│────────────────┘  │
│                                                     │ WebSocket         │
│  ┌──────────────────────────────────────────────────▼────────────────┐  │
│  │  Qt MCP Server (Python)                                           │  │
│  │  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐   │  │
│  │  │ qtPilot Client    │  │ MCP Tools       │  │ HTTP/stdio      │   │  │
│  │  │ (WebSocket)     │  │                 │  │ Transport       │   │  │
│  │  └─────────────────┘  └─────────────────┘  └─────────────────┘   │  │
│  └───────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
                           ┌─────────────────┐
                           │  Claude / LLM   │
                           └─────────────────┘
```

### Injection Model

**Launch-only injection** - The probe must be loaded when the application starts.

#### Linux
```bash
LD_PRELOAD=/path/to/libqtpilot.so ./myapp
```

#### Windows
```cmd
qtpilot-launch.exe myapp.exe --app-args foo bar
```

The `qtpilot-launch.exe` wrapper:
1. Sets up environment variables
2. Ensures `qtpilot.dll` is in the DLL search path
3. Launches the target application
4. The DLL auto-initializes via `DllMain`

---

## API Modes

qtPilot supports **three API modes** to provide maximum flexibility:

| Mode | API Style | Best For |
|------|-----------|----------|
| **Native** | Qt-specific introspection | Test automation, debugging |
| **Computer Use** | Screenshot + coordinates | Visual tasks, custom widgets |
| **Chrome** | Accessibility tree + refs | Form filling, AI agents |

### Mode Selection

Set via environment variable:
```bash
QTPILOT_MODE=native           # Default - full Qt introspection
QTPILOT_MODE=computer_use     # Anthropic Computer Use compatible
QTPILOT_MODE=chrome           # Claude in Chrome compatible  
QTPILOT_MODE=all              # All tools from all modes
```

### Why Multiple Modes?

Claude already knows the Computer Use and Chrome extension APIs from extensive training. By providing compatible tool schemas, Claude can control Qt applications with **zero learning curve**.

See `qtPilot-compatibility-modes.md` for complete details on Computer Use and Chrome mode tool definitions.

---

## Communication Protocol

### Transport

**WebSocket** on configurable port (default: `9999`)

### Message Format

JSON-RPC 2.0 style messages over WebSocket.

#### Request
```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "findByObjectName",
  "params": {
    "name": "submitButton"
  }
}
```

#### Response
```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "objects": [
      {
        "id": "QMainWindow/centralWidget/QPushButton#submitButton",
        "className": "QPushButton"
      }
    ]
  }
}
```

#### Push Event (server-initiated)
```json
{
  "jsonrpc": "2.0",
  "method": "event",
  "params": {
    "type": "signalEmitted",
    "data": {
      "object": "QMainWindow/centralWidget/QPushButton#submitButton",
      "signal": "clicked",
      "args": [true]
    }
  }
}
```

---

## Object Identification

### ID Format

**Hierarchical path** - Human-readable, reflects object tree structure.

```
QMainWindow/centralWidget/QVBoxLayout/QPushButton#submitButton
         │          │           │              │
    class name  class name  class name    class#objectName
```

#### Rules

1. Path segments are separated by `/`
2. Each segment is `ClassName` or `ClassName#objectName` if objectName is set
3. Root is typically `QApplication` or first top-level widget
4. If multiple siblings have same class, append index: `QPushButton[0]`, `QPushButton[1]`
5. QML items use their `id` if available: `QQuickItem#myButton`

#### Examples

```
QApplication/QMainWindow/centralWidget/QPushButton#okButton
QApplication/QMainWindow/menuBar/QMenu#fileMenu/QAction#saveAction
QApplication/QQuickWindow/QQuickItem#root/QQuickRectangle#header
```

---

## Environment Variables

| Variable | Purpose | Default | Example |
|----------|---------|---------|---------|
| `QTPILOT_PORT` | WebSocket server port | `9999` | `9999` |
| `QTPILOT_BIND` | Bind address | `127.0.0.1` | `0.0.0.0` |
| `QTPILOT_ENABLED` | Enable/disable probe | `1` | `0` to disable |
| `QTPILOT_LOG_LEVEL` | Logging verbosity | `info` | `debug`, `info`, `warn`, `error` |
| `QTPILOT_MODE` | API mode | `native` | `native`, `computer_use`, `chrome`, `all` |

---

## API Methods

### Object Discovery

#### `findByObjectName`
Find objects by their `objectName` property.

```json
// Request
{"method": "findByObjectName", "params": {"name": "submitButton"}}

// Response
{
  "objects": [
    {"id": "QMainWindow/centralWidget/QPushButton#submitButton", "className": "QPushButton"}
  ]
}
```

#### `findByClassName`
Find objects by their class name.

```json
// Request
{"method": "findByClassName", "params": {"className": "QPushButton"}}

// Response
{
  "objects": [
    {"id": "QMainWindow/centralWidget/QPushButton#ok", "className": "QPushButton"},
    {"id": "QMainWindow/centralWidget/QPushButton#cancel", "className": "QPushButton"}
  ]
}
```

#### `getObjectTree`
Get the full or partial object hierarchy.

```json
// Request
{"method": "getObjectTree", "params": {"root": null, "depth": 3}}

// Response
{
  "tree": {
    "id": "QApplication",
    "className": "QApplication",
    "children": [
      {
        "id": "QApplication/QMainWindow",
        "className": "QMainWindow",
        "objectName": "",
        "children": [...]
      }
    ]
  }
}
```

### Object Inspection

#### `getObjectInfo`
Get detailed information about an object.

```json
// Request
{"method": "getObjectInfo", "params": {"id": "QMainWindow/centralWidget/QPushButton#submit"}}

// Response
{
  "id": "QMainWindow/centralWidget/QPushButton#submit",
  "className": "QPushButton",
  "objectName": "submit",
  "inheritance": ["QPushButton", "QAbstractButton", "QWidget", "QObject"],
  "isWidget": true,
  "visible": true,
  "enabled": true,
  "geometry": {"x": 100, "y": 200, "width": 80, "height": 30},
  "globalPosition": {"x": 500, "y": 400}
}
```

#### `listProperties`
List all properties of an object.

```json
// Request
{"method": "listProperties", "params": {"id": "QMainWindow/centralWidget/QPushButton#submit"}}

// Response
{
  "properties": [
    {"name": "text", "type": "QString", "value": "Submit", "writable": true},
    {"name": "enabled", "type": "bool", "value": true, "writable": true},
    {"name": "visible", "type": "bool", "value": true, "writable": true},
    {"name": "checkable", "type": "bool", "value": false, "writable": true}
  ]
}
```

#### `getProperty`
Get a specific property value.

```json
// Request
{"method": "getProperty", "params": {"id": "...", "property": "text"}}

// Response
{"value": "Submit"}
```

#### `setProperty`
Set a property value.

```json
// Request
{"method": "setProperty", "params": {"id": "...", "property": "text", "value": "Click Me"}}

// Response
{"success": true, "newValue": "Click Me"}
```

#### `listMethods`
List all invokable methods (slots and Q_INVOKABLE).

```json
// Request
{"method": "listMethods", "params": {"id": "..."}}

// Response
{
  "methods": [
    {"name": "click", "signature": "click()", "returnType": "void", "parameters": []},
    {"name": "setText", "signature": "setText(QString)", "returnType": "void", 
     "parameters": [{"name": "text", "type": "QString"}]}
  ]
}
```

#### `invokeMethod`
Invoke a method on an object.

```json
// Request
{"method": "invokeMethod", "params": {"id": "...", "method": "click", "args": []}}

// Response
{"success": true, "result": null}
```

#### `listSignals`
List all signals of an object.

```json
// Request
{"method": "listSignals", "params": {"id": "..."}}

// Response
{
  "signals": [
    {"name": "clicked", "signature": "clicked(bool)", "parameters": [{"name": "checked", "type": "bool"}]},
    {"name": "pressed", "signature": "pressed()", "parameters": []}
  ]
}
```

### UI Interaction

#### `click`
Simulate a mouse click on a widget.

```json
// Request
{"method": "click", "params": {"id": "...", "button": "left", "position": "center"}}

// Optional position: "center" (default), {"x": 10, "y": 5} for offset from top-left

// Response
{"success": true}
```

#### `sendKeys`
Send keyboard input to a widget.

```json
// Request
{"method": "sendKeys", "params": {"id": "...", "text": "Hello World"}}

// Response
{"success": true}
```

#### `screenshot`
Take a screenshot of a widget or the entire screen.

```json
// Request
{"method": "screenshot", "params": {"id": "...", "format": "png"}}
// id is optional; if omitted, captures entire screen

// Response
{
  "success": true,
  "format": "png",
  "width": 800,
  "height": 600,
  "data": "iVBORw0KGgoAAAANSUhEUgAA..." // base64
}
```

#### `getGeometry`
Get widget geometry and screen position.

```json
// Request
{"method": "getGeometry", "params": {"id": "..."}}

// Response
{
  "local": {"x": 10, "y": 20, "width": 100, "height": 30},
  "global": {"x": 510, "y": 320, "width": 100, "height": 30},
  "visible": true,
  "enabled": true
}
```

### Signal Monitoring

#### `subscribeSignals`
Subscribe to signals from a specific object.

```json
// Request
{
  "method": "subscribeSignals",
  "params": {
    "id": "QMainWindow/centralWidget/QPushButton#submit",
    "signals": ["clicked", "pressed", "released"]
  }
}

// Response
{"success": true, "subscriptionId": "sub_1"}
```

#### `unsubscribeSignals`
Unsubscribe from signals.

```json
// Request
{"method": "unsubscribeSignals", "params": {"subscriptionId": "sub_1"}}

// Response
{"success": true}
```

### Push Events

The server pushes these events to subscribed clients:

#### `objectCreated`
```json
{
  "jsonrpc": "2.0",
  "method": "event",
  "params": {
    "type": "objectCreated",
    "data": {
      "id": "QMainWindow/QDialog#newDialog",
      "className": "QDialog",
      "objectName": "newDialog"
    }
  }
}
```

#### `objectDestroyed`
```json
{
  "jsonrpc": "2.0",
  "method": "event",
  "params": {
    "type": "objectDestroyed",
    "data": {
      "id": "QMainWindow/QDialog#newDialog"
    }
  }
}
```

#### `signalEmitted`
```json
{
  "jsonrpc": "2.0",
  "method": "event",
  "params": {
    "type": "signalEmitted",
    "data": {
      "subscriptionId": "sub_1",
      "object": "QMainWindow/centralWidget/QPushButton#submit",
      "signal": "clicked",
      "args": [false]
    }
  }
}
```

---

## QML Support

### Level 1 (MVP)

- QML items appear in the object tree as `QQuickItem` subclasses
- Basic properties accessible via standard introspection
- QML `id` used in hierarchical path if available
- Context properties accessible via `getProperty`

### Limitations in MVP

- No direct access to QML bindings
- No JavaScript evaluation
- No scene graph inspection
- No shader/render inspection

---

## Security Model

### Configurable Binding

- Default: `127.0.0.1` (localhost only)
- Configurable via `QTPILOT_BIND` environment variable
- For remote access: set `QTPILOT_BIND=0.0.0.0`

### No Authentication in MVP

- Rely on network-level security (firewall, SSH tunneling)
- Authentication may be added in future versions

### No Filtering in MVP

- All objects and properties accessible
- No blocklist/allowlist for sensitive data

---

## File Structure

```
qtpilot/
├── CMakeLists.txt
├── README.md
├── LICENSE                    # MIT
├── vcpkg.json                 # vcpkg manifest (post-MVP)
│
├── probe/                     # C++ probe library
│   ├── CMakeLists.txt
│   ├── src/
│   │   ├── qtpilot.cpp          # Entry point, initialization
│   │   ├── qtpilot.h
│   │   ├── object_tracker.cpp # QObject lifecycle tracking
│   │   ├── object_tracker.h
│   │   ├── introspector.cpp   # QMetaObject introspection
│   │   ├── introspector.h
│   │   ├── websocket_server.cpp
│   │   ├── websocket_server.h
│   │   ├── path_builder.cpp   # Hierarchical ID generation
│   │   ├── path_builder.h
│   │   └── platform/
│   │       ├── linux.cpp
│   │       └── windows.cpp
│   └── include/
│       └── qtpilot/
│           └── qtpilot.h        # Public API (optional direct linking)
│
├── launcher/                  # Windows launcher executable
│   ├── CMakeLists.txt
│   └── src/
│       └── main.cpp           # qtpilot-launch.exe
│
├── python/                    # Python client library
│   ├── pyproject.toml
│   └── qtpilot/
│       ├── __init__.py
│       ├── client.py          # WebSocket client
│       └── mcp_tools.py       # MCP tool definitions
│
├── examples/
│   ├── simple_app/            # Test Qt application
│   └── automation_script.py   # Example Python usage
│
└── tests/
    ├── probe/                 # C++ unit tests
    └── integration/           # End-to-end tests
```

---

## Build Requirements

### Probe (C++)

- **CMake** 3.16+
- **Qt** 5.15.x or 6.x
- **C++17** compiler
  - Windows: MSVC 2019+ or MinGW-w64
  - Linux: GCC 9+ or Clang 10+
- **WebSocket library**: Qt WebSockets module (`Qt5WebSockets` / `Qt6WebSockets`)

### Python Client

- **Python** 3.8+
- **websockets** library
- **mcp** library (for MCP integration)

---

## Distribution

### MVP

- **Source distribution** via GitHub
- Users build for their specific Qt version

### Post-MVP

- vcpkg port
- Pre-built binaries for common configurations

---

## Out of Scope (MVP)

| Feature | Status |
|---------|--------|
| macOS support | v1.1 |
| Recording/playback | Future |
| Attach to running process | Future |
| Authentication | Future |
| Qt 4 support | Never |
| Mobile (Android/iOS) | Future |
| Built-in GUI | Never |
| Plugin system | Never |

---

## Success Criteria

MVP is complete when:

1. ✅ Probe loads successfully on Windows and Linux
2. ✅ WebSocket server accepts connections
3. ✅ All API methods work for QWidget-based apps
4. ✅ Basic QML item introspection works
5. ✅ Signal subscription and push events work
6. ✅ Python client can automate a sample Qt app
7. ✅ MCP integration allows Claude to control a Qt app

---

## Appendix: Qt Internal Hooks

The probe uses Qt's internal `qtHookData` array for object tracking:

```cpp
namespace QHooks {
    enum HookIndex {
        HookDataVersion = 0,
        HookDataSize = 1,
        QtVersion = 2,
        AddQObject = 3,      // Called on every QObject construction
        RemoveQObject = 4,   // Called on every QObject destruction
        Startup = 5,         // Called when QCoreApplication starts
    };
}

extern Q_CORE_EXPORT quintptr qtHookData[];
```

This is an undocumented but stable API used by GammaRay and other introspection tools.

For signal monitoring, we use:

```cpp
extern void Q_CORE_EXPORT qt_register_signal_spy_callbacks(QSignalSpyCallbackSet &);
```

---

## Revision History

| Version | Date | Changes |
|---------|------|---------|
| 1.0-draft | 2025-01-24 | Initial specification |
