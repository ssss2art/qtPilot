# qtPilot MVP Implementation Plan

## Executive Summary

This document outlines the implementation plan for qtPilot MVP - a Qt application introspection and automation library with MCP (Model Context Protocol) integration for Claude AI.

### Scope Summary
| Item | Decision |
|------|----------|
| Platforms | Windows + Linux (simultaneous) |
| Qt Version | 5.15.2 |
| API Modes | All three (Native, Computer Use, Chrome) |
| Python MCP Server | Yes (MVP scope) |
| Test App | Create new + test against existing apps |
| Starting Point | From scratch |
| Build System | CMake 3.x + vcpkg |
| CI/CD | GitHub Actions |
| Code Style | Google C++ Style Guide |

---

## Project Structure

```
qtPilot/
├── CMakeLists.txt                 # Root CMake configuration
├── CMakePresets.json              # CMake presets for Windows/Linux
├── vcpkg.json                     # vcpkg manifest
├── .github/
│   └── workflows/
│       ├── ci.yml                 # Main CI pipeline
│       ├── release.yml            # Release builds
│       └── codeql.yml             # Security scanning
├── cmake/
│   ├── qtPilotConfig.cmake.in       # Package config template
│   ├── CompilerWarnings.cmake     # Compiler warning flags
│   └── GoogleStyle.cmake          # Clang-format integration
├── src/
│   ├── probe/                     # C++ probe library
│   │   ├── CMakeLists.txt
│   │   ├── core/
│   │   │   ├── probe.h            # Main probe class
│   │   │   ├── probe.cpp
│   │   │   ├── injector.h         # Platform injection helpers
│   │   │   ├── injector_linux.cpp
│   │   │   └── injector_windows.cpp
│   │   ├── transport/
│   │   │   ├── websocket_server.h
│   │   │   ├── websocket_server.cpp
│   │   │   ├── jsonrpc_handler.h
│   │   │   └── jsonrpc_handler.cpp
│   │   ├── introspection/
│   │   │   ├── object_registry.h   # Object tracking & ID assignment
│   │   │   ├── object_registry.cpp
│   │   │   ├── meta_inspector.h    # Qt meta-object introspection
│   │   │   ├── meta_inspector.cpp
│   │   │   ├── widget_tree.h       # Widget hierarchy traversal
│   │   │   └── widget_tree.cpp
│   │   ├── modes/
│   │   │   ├── mode_interface.h    # Abstract mode interface
│   │   │   ├── native_mode.h       # Native qtPilot API
│   │   │   ├── native_mode.cpp
│   │   │   ├── computer_use_mode.h # Computer Use compatibility
│   │   │   ├── computer_use_mode.cpp
│   │   │   ├── chrome_mode.h       # Chrome compatibility
│   │   │   └── chrome_mode.cpp
│   │   ├── actions/
│   │   │   ├── mouse_controller.h  # Mouse simulation
│   │   │   ├── mouse_controller.cpp
│   │   │   ├── keyboard_controller.h
│   │   │   ├── keyboard_controller.cpp
│   │   │   ├── screenshot_capture.h
│   │   │   └── screenshot_capture.cpp
│   │   └── accessibility/
│   │       ├── a11y_tree_builder.h # Accessibility tree generation
│   │       ├── a11y_tree_builder.cpp
│   │       ├── ref_manager.h       # Reference ID management
│   │       └── ref_manager.cpp
│   ├── launcher/                   # Windows launcher wrapper
│   │   ├── CMakeLists.txt
│   │   ├── main.cpp
│   │   └── process_injector.cpp
│   └── mcp_server/                 # Python MCP server
│       ├── pyproject.toml
│       ├── qtpilot/
│       │   ├── __init__.py
│       │   ├── server.py           # MCP server implementation
│       │   ├── client.py           # WebSocket client to probe
│       │   ├── tools/
│       │   │   ├── __init__.py
│       │   │   ├── native.py       # Native mode tools
│       │   │   ├── computer_use.py # Computer Use tools
│       │   │   └── chrome.py       # Chrome mode tools
│       │   └── config.py
│       └── tests/
│           └── ...
├── test_app/                       # Test Qt application
│   ├── CMakeLists.txt
│   ├── main.cpp
│   ├── mainwindow.h
│   ├── mainwindow.cpp
│   └── mainwindow.ui
├── tests/                          # C++ unit tests
│   ├── CMakeLists.txt
│   ├── test_object_registry.cpp
│   ├── test_meta_inspector.cpp
│   ├── test_jsonrpc.cpp
│   └── integration/
│       └── test_full_flow.cpp
├── docs/
│   ├── getting-started.md
│   ├── api-reference.md
│   └── claude-integration.md
├── .clang-format                   # Google style clang-format
├── .clang-tidy                     # Static analysis config
└── README.md
```

---

## Phase 1: Project Foundation (Week 1-2)

### 1.1 Repository Setup

#### Tasks:
1. **Initialize CMake project structure**
   - Root CMakeLists.txt with project definition
   - CMakePresets.json for Windows/Linux configurations
   - Minimum CMake version: 3.16 (for Qt5 support)

2. **Configure vcpkg integration**
   ```json
   // vcpkg.json
   {
     "name": "qtpilot",
     "version": "0.1.0",
     "dependencies": [
       "nlohmann-json",
       "gtest",
       "spdlog"
     ]
   }
   ```
   - Note: Qt will be found via system/pre-installed Qt, not vcpkg

3. **Setup GitHub Actions CI**
   - Build matrix: Ubuntu 22.04 + Windows Server 2022
   - Qt 5.15.2 installation via `jurplel/install-qt-action`
   - Clang-format check
   - Clang-tidy static analysis
   - Unit test execution
   - Build artifact packaging

4. **Code style configuration**
   - `.clang-format` with Google style base
   - `.clang-tidy` with core checks enabled
   - Pre-commit hooks (optional)

#### Deliverables:
- [ ] CMakeLists.txt compiles empty probe library
- [ ] CI builds successfully on both platforms
- [ ] clang-format enforced in CI

### 1.2 Core Probe Skeleton

#### Tasks:
1. **Create probe library entry point**
   ```cpp
   // probe.h
   namespace qtpilot {

   class Probe : public QObject {
     Q_OBJECT
    public:
     static Probe* Instance();
     bool Initialize(int port = 9999);
     void Shutdown();

    private:
     Probe();
     std::unique_ptr<WebSocketServer> server_;
   };

   }  // namespace qtpilot
   ```

2. **Implement library initialization**
   - Linux: `__attribute__((constructor))` for auto-init
   - Windows: `DllMain` with `DLL_PROCESS_ATTACH`
   - Environment variable config: `QTPILOT_PORT`, `QTPILOT_MODE`

3. **Setup WebSocket server**
   - Use `QWebSocketServer` from Qt WebSockets module
   - JSON-RPC 2.0 message parsing
   - Connection management

#### Deliverables:
- [ ] Probe auto-initializes when loaded
- [ ] WebSocket server accepts connections on configured port
- [ ] Basic ping/pong JSON-RPC works

---

## Phase 2: Qt Introspection Layer (Week 2-3)

### 2.1 Object Registry

#### Tasks:
1. **Implement object tracking**
   ```cpp
   class ObjectRegistry : public QObject {
    public:
     // Register object, returns assigned ID
     QString RegisterObject(QObject* obj);

     // Lookup by ID
     QObject* FindObject(const QString& object_id);

     // Generate hierarchical ID: QMainWindow/centralWidget/QPushButton#name
     QString GenerateObjectId(QObject* obj);

    signals:
     void ObjectCreated(const QString& object_id);
     void ObjectDestroyed(const QString& object_id);

    private:
     QHash<QString, QPointer<QObject>> id_to_object_;
     QHash<QObject*, QString> object_to_id_;
   };
   ```

2. **Widget tree discovery**
   - Hook into `QCoreApplication::instance()`
   - Traverse `QApplication::topLevelWidgets()`
   - Monitor `QEvent::ChildAdded` / `QEvent::ChildRemoved`
   - Handle dynamic object creation/destruction

3. **Object ID generation**
   - Format: `ClassName/childName/ClassName#objectName`
   - Handle unnamed objects with index: `QWidget[0]`
   - Cache IDs for performance

#### Deliverables:
- [ ] Can enumerate all widgets in running Qt app
- [ ] Stable object IDs persist across calls
- [ ] Object destruction properly tracked

### 2.2 Meta-Object Introspection

#### Tasks:
1. **Property inspection**
   ```cpp
   class MetaInspector {
    public:
     // Get all properties with values
     QJsonObject GetProperties(QObject* obj);

     // Get single property
     QVariant GetProperty(QObject* obj, const QString& name);

     // Set property (with type coercion)
     bool SetProperty(QObject* obj, const QString& name,
                      const QJsonValue& value);

     // List available properties
     QJsonArray ListProperties(QObject* obj);
   };
   ```

2. **Method introspection**
   - List invokable methods via `QMetaObject`
   - Parse method signatures
   - Support `Q_INVOKABLE` and slots

3. **Signal introspection**
   - List signals via `QMetaObject`
   - Parse signal parameter types

4. **Type conversion**
   - JSON → QVariant conversion
   - Handle common types: QString, int, bool, QColor, QRect, etc.
   - QVariant → JSON serialization

#### Deliverables:
- [ ] Can read all properties of any QObject
- [ ] Can set writable properties
- [ ] Can list methods and signals

---

## Phase 3: Native Mode API (Week 3-4)

### 3.1 JSON-RPC Method Handlers

Implement all Native mode methods from specification:

#### Discovery Methods:
```cpp
// getApplicationInfo
QJsonObject GetApplicationInfo();
// Response: {appName, appVersion, qtVersion, pid, platform}

// getRootObjects
QJsonArray GetRootObjects();
// Response: [{objectId, className, objectName, visible, geometry}...]

// getChildren
QJsonArray GetChildren(const QString& object_id);

// findObjects
QJsonArray FindObjects(const QString& class_name,
                       const QString& name_pattern);
```

#### Inspection Methods:
```cpp
// getObjectInfo
QJsonObject GetObjectInfo(const QString& object_id);

// getProperties
QJsonObject GetProperties(const QString& object_id,
                          const QStringList& names = {});

// getMethods
QJsonArray GetMethods(const QString& object_id);

// getSignals
QJsonArray GetSignals(const QString& object_id);
```

#### Manipulation Methods:
```cpp
// setProperty
bool SetProperty(const QString& object_id,
                 const QString& property,
                 const QJsonValue& value);

// invokeMethod
QJsonValue InvokeMethod(const QString& object_id,
                        const QString& method,
                        const QJsonArray& args);
```

#### Signal Monitoring:
```cpp
// subscribeSignal
int SubscribeSignal(const QString& object_id,
                    const QString& signal);

// unsubscribeSignal
void UnsubscribeSignal(int subscription_id);

// Push notification when signal emits
void OnSignalEmitted(int subscription_id, const QJsonArray& args);
```

#### UI Interaction:
```cpp
// click
bool Click(const QString& object_id,
           const QString& button = "left");

// sendKeys
bool SendKeys(const QString& object_id,
              const QString& text,
              const QStringList& modifiers = {});

// screenshot
QJsonObject Screenshot(const QString& object_id = "");
```

#### Deliverables:
- [ ] All 15+ Native mode JSON-RPC methods implemented
- [ ] Unit tests for each method
- [ ] Integration test with test app

---

## Phase 4: Computer Use Mode (Week 4-5)

### 4.1 Screenshot Capture

#### Tasks:
1. **Implement screenshot capture**
   ```cpp
   class ScreenshotCapture {
    public:
     // Capture specific widget or entire window
     QImage Capture(QWidget* widget = nullptr);

     // Encode to base64 PNG
     QString EncodeBase64(const QImage& image);

     // Scale if needed
     QImage ScaleToMax(const QImage& image, int max_width, int max_height);
   };
   ```

2. **Handle high-DPI displays**
   - Use `QScreen::grabWindow()` or `QWidget::grab()`
   - Account for device pixel ratio
   - Return actual pixel dimensions

### 4.2 Mouse Controller

#### Tasks:
1. **Platform-agnostic mouse simulation**
   ```cpp
   class MouseController {
    public:
     void MoveTo(int x, int y);
     void LeftClick(int x, int y);
     void RightClick(int x, int y);
     void DoubleClick(int x, int y);
     void Drag(int start_x, int start_y, int end_x, int end_y);
     QPoint GetCursorPosition();
   };
   ```

2. **Linux implementation**
   - Use `QTest::mouseClick()` for in-process widgets
   - Or XTest extension for system-level events

3. **Windows implementation**
   - Use `QTest::mouseClick()` for in-process widgets
   - Or `SendInput()` API for system-level events

### 4.3 Keyboard Controller

#### Tasks:
1. **Key simulation**
   ```cpp
   class KeyboardController {
    public:
     void Type(const QString& text);
     void SendKey(const QString& key_combo);  // "ctrl+s", "Return"

    private:
     Qt::Key ParseKey(const QString& key_name);
     Qt::KeyboardModifiers ParseModifiers(const QString& combo);
   };
   ```

2. **Key combo parsing**
   - Support: `ctrl`, `alt`, `shift`, `meta`/`win`/`cmd`
   - Support special keys: `Return`, `Tab`, `Escape`, `F1`-`F12`, arrows

### 4.4 Computer Use Tool Handler

#### Tasks:
1. **Implement `computer` tool**
   ```cpp
   class ComputerUseMode : public ModeInterface {
    public:
     QJsonObject HandleAction(const QJsonObject& params) override;

    private:
     QJsonObject DoScreenshot();
     QJsonObject DoLeftClick(int x, int y);
     QJsonObject DoRightClick(int x, int y);
     QJsonObject DoDoubleClick(int x, int y);
     QJsonObject DoMouseMove(int x, int y);
     QJsonObject DoDrag(int x1, int y1, int x2, int y2);
     QJsonObject DoType(const QString& text);
     QJsonObject DoKey(const QString& combo);
     QJsonObject DoScroll(int x, int y, const QString& dir, int amount);
     QJsonObject DoGetCursorPosition();
   };
   ```

2. **Response format**
   - Always include screenshot after actions (matching Anthropic API)
   - Return base64-encoded PNG

#### Deliverables:
- [ ] Screenshot capture working on both platforms
- [ ] Mouse actions working (click, drag, move)
- [ ] Keyboard actions working (type, key combos)
- [ ] Full `computer` tool matching Anthropic schema

---

## Phase 5: Chrome Mode (Week 5-6)

### 5.1 Accessibility Tree Builder

#### Tasks:
1. **Generate accessibility tree representation**
   ```cpp
   class A11yTreeBuilder {
    public:
     struct TreeNode {
       int ref;
       QString class_name;
       QString name;
       QString role;
       QRect geometry;
       QStringList behaviors;  // clickable, editable, etc.
       QVector<TreeNode> children;
     };

     TreeNode BuildTree(QWidget* root);
     QString FormatTree(const TreeNode& root);  // ASCII tree format
   };
   ```

2. **Widget to role mapping**
   | Qt Widget | Role | Behaviors |
   |-----------|------|-----------|
   | QPushButton | button | clickable |
   | QLineEdit | textbox | editable |
   | QTextEdit | textbox | editable, multiline |
   | QCheckBox | checkbox | checkable |
   | QComboBox | combobox | expandable |
   | QSlider | slider | adjustable |
   | QLabel | statictext | - |
   | QMenu | menu | expandable |

3. **Format output** (matching Chrome extension style)
   ```
   [1] QMainWindow "My App" (1920x1080)
   ├── [2] QMenuBar
   │   ├── [3] QMenu "File"
   │   └── [4] QMenu "Edit"
   ├── [5] QWidget "centralWidget"
   │   ├── [6] QLabel "Name:"
   │   ├── [7] QLineEdit (editable) value=""
   │   └── [8] QPushButton "Submit" (clickable)
   └── [9] QStatusBar "Ready"
   ```

### 5.2 Reference Manager

#### Tasks:
1. **Assign and track integer refs**
   ```cpp
   class RefManager {
    public:
     // Assign refs during tree build
     int AssignRef(QObject* obj);

     // Lookup object by ref
     QObject* GetObject(int ref);

     // Clear refs (call before each read_page)
     void Reset();

    private:
     QHash<int, QPointer<QObject>> ref_to_object_;
     int next_ref_ = 1;
   };
   ```

2. **Ref stability**
   - Refs are ephemeral (reset each `read_page` call)
   - Sequential assignment during tree traversal

### 5.3 Chrome Mode Tools

#### Tasks:
1. **Implement all Chrome mode tools**
   ```cpp
   class ChromeMode : public ModeInterface {
    public:
     // read_page - returns accessibility tree
     QJsonObject ReadPage(bool include_invisible = false);

     // click - click by ref
     QJsonObject Click(int ref, const QString& button = "left");

     // form_input - set value by ref
     QJsonObject FormInput(int ref, const QString& value);

     // get_page_text - extract all visible text
     QString GetPageText();

     // find - natural language element search
     QJsonObject Find(const QString& query);

     // navigate - tabs, menus, back/forward
     QJsonObject Navigate(const QString& action, const QString& target);

     // tabs_context - window information
     QJsonObject TabsContext();

     // read_console_messages - Qt debug output
     QJsonObject ReadConsoleMessages(const QString& level, int limit);
   };
   ```

2. **Form input handling**
   - QLineEdit: `setText()`
   - QTextEdit: `setPlainText()`
   - QSpinBox: `setValue()`
   - QComboBox: `setCurrentText()`
   - QCheckBox: `setChecked()`

3. **Natural language find**
   - Simple fuzzy matching on text/name
   - Return confidence scores

#### Deliverables:
- [ ] Accessibility tree generation working
- [ ] All 8 Chrome mode tools implemented
- [ ] Ref-based clicking and form input working

---

## Phase 6: Python MCP Server (Week 6-7)

### 6.1 Project Setup

#### Tasks:
1. **Create Python package structure**
   ```
   src/mcp_server/
   ├── pyproject.toml
   ├── qtpilot/
   │   ├── __init__.py
   │   ├── server.py
   │   ├── client.py
   │   └── tools/
   ```

2. **Dependencies (pyproject.toml)**
   ```toml
   [project]
   name = "qtpilot"
   version = "0.1.0"
   dependencies = [
     "mcp>=1.0.0",
     "websockets>=12.0",
     "pydantic>=2.0",
   ]
   ```

### 6.2 WebSocket Client

#### Tasks:
1. **Implement probe client**
   ```python
   class qtPilotClient:
       def __init__(self, host: str = "localhost", port: int = 9999):
           self.uri = f"ws://{host}:{port}"
           self.ws = None
           self._request_id = 0

       async def connect(self):
           self.ws = await websockets.connect(self.uri)

       async def call(self, method: str, params: dict = None) -> dict:
           self._request_id += 1
           request = {
               "jsonrpc": "2.0",
               "id": self._request_id,
               "method": method,
               "params": params or {}
           }
           await self.ws.send(json.dumps(request))
           response = await self.ws.recv()
           return json.loads(response)
   ```

### 6.3 MCP Server Implementation

#### Tasks:
1. **Implement MCP server with all tools**
   ```python
   from mcp.server import Server
   from mcp.types import Tool, TextContent, ImageContent

   server = Server("qtpilot")
   client = qtPilotClient()

   @server.list_tools()
   async def list_tools() -> list[Tool]:
       mode = os.getenv("QTPILOT_MODE", "all")
       tools = []

       if mode in ("native", "all"):
           tools.extend(NATIVE_TOOLS)
       if mode in ("computer_use", "all"):
           tools.extend(COMPUTER_USE_TOOLS)
       if mode in ("chrome", "all"):
           tools.extend(CHROME_TOOLS)

       return tools

   @server.call_tool()
   async def call_tool(name: str, arguments: dict):
       result = await client.call(name, arguments)
       # Format response based on tool type
       ...
   ```

2. **Tool definitions**
   - Native mode: 15+ tools
   - Computer Use: 1 tool with actions
   - Chrome mode: 8 tools

3. **Response formatting**
   - Text responses as `TextContent`
   - Screenshots as `ImageContent` with base64 data

### 6.4 Claude Desktop Integration

#### Tasks:
1. **Create config example**
   ```json
   {
     "mcpServers": {
       "qtpilot": {
         "command": "python",
         "args": ["-m", "qtpilot.server"],
         "env": {
           "QTPILOT_HOST": "localhost",
           "QTPILOT_PORT": "9999",
           "QTPILOT_MODE": "all"
         }
       }
     }
   }
   ```

2. **Startup script**
   - Launch target Qt app with probe injected
   - Start MCP server
   - Connect to Claude Desktop

#### Deliverables:
- [ ] Python MCP server package complete
- [ ] All tools exposed via MCP protocol
- [ ] Claude Desktop integration documented and tested

---

## Phase 7: Test Application (Week 7)

### 7.1 Create Test Qt Application

#### Tasks:
1. **Design comprehensive test UI**
   - Main window with menus, toolbars, status bar
   - Form with various input types:
     - QLineEdit (single-line text)
     - QTextEdit (multi-line text)
     - QSpinBox (numeric)
     - QComboBox (dropdown)
     - QCheckBox, QRadioButton
     - QSlider
     - QPushButton
   - Tab widget with multiple pages
   - List/Table/Tree widgets
   - Custom painted widget (for Computer Use testing)
   - Dialog that can be opened

2. **Add testable behaviors**
   - Form submission that shows result
   - Menu actions that change state
   - Signals that can be monitored
   - Properties that can be read/written

3. **Include edge cases**
   - Deeply nested widgets
   - Dynamic widget creation
   - Modal dialogs
   - Hidden widgets

#### Deliverables:
- [ ] Test app with comprehensive widget coverage
- [ ] App compiles on Windows and Linux
- [ ] Documented test scenarios

---

## Phase 8: Testing & Documentation (Week 8)

### 8.1 Unit Tests

#### C++ Tests (Google Test):
```cpp
// test_object_registry.cpp
TEST(ObjectRegistryTest, GeneratesStableIds) { ... }
TEST(ObjectRegistryTest, TracksObjectDestruction) { ... }

// test_meta_inspector.cpp
TEST(MetaInspectorTest, ReadsAllProperties) { ... }
TEST(MetaInspectorTest, SetsWritableProperties) { ... }

// test_jsonrpc.cpp
TEST(JsonRpcTest, ParsesValidRequests) { ... }
TEST(JsonRpcTest, RejectsInvalidRequests) { ... }
```

#### Python Tests (pytest):
```python
# test_client.py
async def test_connect_to_probe(): ...
async def test_call_method(): ...

# test_tools.py
async def test_computer_screenshot(): ...
async def test_chrome_read_page(): ...
```

### 8.2 Integration Tests

#### Tasks:
1. **End-to-end test flow**
   - Launch test app with probe
   - Connect via WebSocket
   - Execute all API methods
   - Verify responses

2. **Claude simulation tests**
   - Replay recorded Claude interactions
   - Verify expected behavior

### 8.3 Documentation

#### Tasks:
1. **Getting Started Guide**
   - Installation (Windows/Linux)
   - Basic usage
   - First automation script

2. **API Reference**
   - All JSON-RPC methods
   - All MCP tools
   - Request/response examples

3. **Claude Integration Guide**
   - Claude Desktop setup
   - Mode selection guidance
   - Example prompts and workflows

#### Deliverables:
- [ ] 80%+ code coverage
- [ ] All integration tests passing
- [ ] Documentation complete

---

## CI/CD Pipeline

### GitHub Actions Workflow

```yaml
# .github/workflows/ci.yml
name: CI

on: [push, pull_request]

jobs:
  build-linux:
    runs-on: ubuntu-22.04
    steps:
      - uses: actions/checkout@v4

      - name: Install Qt
        uses: jurplel/install-qt-action@v3
        with:
          version: '5.15.2'
          modules: 'qtwebsockets'

      - name: Setup vcpkg
        uses: lukka/run-vcpkg@v11

      - name: Configure
        run: cmake --preset linux-release

      - name: Build
        run: cmake --build --preset linux-release

      - name: Test
        run: ctest --preset linux-release

  build-windows:
    runs-on: windows-2022
    steps:
      - uses: actions/checkout@v4

      - name: Install Qt
        uses: jurplel/install-qt-action@v3
        with:
          version: '5.15.2'
          modules: 'qtwebsockets'

      - name: Setup vcpkg
        uses: lukka/run-vcpkg@v11

      - name: Configure
        run: cmake --preset windows-release

      - name: Build
        run: cmake --build --preset windows-release

      - name: Test
        run: ctest --preset windows-release

  lint:
    runs-on: ubuntu-22.04
    steps:
      - uses: actions/checkout@v4
      - name: clang-format check
        uses: jidicula/clang-format-action@v4
        with:
          clang-format-version: '17'

  python:
    runs-on: ubuntu-22.04
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-python@v5
        with:
          python-version: '3.11'
      - name: Install and test
        run: |
          cd src/mcp_server
          pip install -e ".[dev]"
          pytest
```

---

## Risk Mitigation

### Technical Risks

| Risk | Impact | Mitigation |
|------|--------|------------|
| Qt WebSocket stability | High | Extensive testing, fallback to raw TCP |
| Platform input simulation differences | Medium | Abstract behind interface, platform-specific implementations |
| High-DPI screenshot issues | Medium | Test on various DPI settings, use device pixel ratio |
| Object lifetime tracking | High | Use QPointer, extensive destroyed signal handling |
| MCP protocol compatibility | Medium | Test with multiple Claude clients |

### Schedule Risks

| Risk | Impact | Mitigation |
|------|--------|------------|
| Windows injection complexity | Medium | Start simple (launcher), defer DLL injection |
| Scope creep | High | Strict MVP definition, defer non-essential features |
| Qt version differences | Low | Focus on 5.15.2, document any 6.x differences |

---

## MVP Definition of Done

### Core Functionality
- [ ] Probe library loads into Qt 5.15.2 app on Windows and Linux
- [ ] WebSocket server accepts connections
- [ ] All Native mode methods functional
- [ ] Computer Use mode fully working
- [ ] Chrome mode fully working
- [ ] Python MCP server connects to probe
- [ ] All tools exposed via MCP

### Quality
- [ ] Unit tests passing (80%+ coverage)
- [ ] Integration tests passing
- [ ] CI/CD pipeline green
- [ ] No critical/high security issues
- [ ] Code passes clang-tidy checks
- [ ] Google C++ style enforced

### Documentation
- [ ] README with quick start
- [ ] API reference
- [ ] Claude Desktop integration guide

### Artifacts
- [ ] Linux: `libqtpilot.so` probe library
- [ ] Windows: `qtpilot.dll` + `qtPilot-launcher.exe`
- [ ] Python: `qtpilot` package on PyPI (or installable)

---

## Post-MVP Roadmap (Not in Scope)

These items are explicitly **out of scope** for MVP:

1. macOS support
2. Qt 6.x testing/support
3. DLL injection (Windows uses launcher only)
4. Remote connections (localhost only for MVP)
5. Authentication/security
6. Multiple simultaneous connections
7. Record/playback functionality
8. Visual element highlighting
9. Performance profiling tools

---

## Appendix: Key Technical Decisions

### Why Qt WebSockets over raw sockets?
- Native Qt integration
- Handles message framing
- Works well with event loop
- No additional dependencies

### Why JSON-RPC 2.0?
- Standard protocol
- Simple request/response model
- Supports notifications (for signals)
- Easy to debug

### Why Python for MCP server?
- MCP SDK is Python-first
- Easy to modify/extend
- Fast development iteration
- Claude ecosystem familiarity

### Why launcher wrapper over DLL injection (Windows)?
- Simpler implementation
- No admin rights needed
- More reliable
- DLL injection can be added post-MVP

