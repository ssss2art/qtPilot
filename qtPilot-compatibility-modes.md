# qtPilot Compatibility Modes

## Overview

This document proposes adding **two compatibility modes** to qtPilot that mimic Anthropic's established tool APIs. This allows Claude to use qtPilot with **zero learning curve** because the tool schemas match what Claude already knows.

## The Three Modes

| Mode | API Style | Best For |
|------|-----------|----------|
| **Native qtPilot** | Qt-specific introspection | Test automation, deep inspection |
| **Computer Use** | Screenshot + coordinates | Visual tasks, pixel-precise clicking |
| **Chrome** | Accessibility tree + refs | Form filling, semantic interactions |

## Why This Matters

Claude has been extensively trained on these tool schemas:

```
Computer Use:  screenshot, left_click, type, mouse_move, key, scroll
Chrome:        read_page, form_input, click, navigate, get_page_text
```

By using **identical tool names and schemas**, Claude already knows:
- When to take screenshots vs. read the accessibility tree
- How to specify coordinates vs. use element references
- How to type text and fill forms
- How to click elements
- Error recovery patterns

## Mode Comparison

### Mode 1: Native qtPilot API (Default)
- Rich Qt-specific introspection
- Hierarchical object IDs (`QMainWindow/centralWidget/QPushButton#submit`)
- Signal subscriptions and push events
- Full property/method access
- Best for: Test automation, debugging, deep inspection

### Mode 2: Computer Use Mode
- Mimics Anthropic's `computer_20250124` tool
- Screenshot-driven workflow
- Pixel coordinate clicking
- Best for: Visual tasks, games, custom widgets, pixel-precise work

### Mode 3: Chrome Mode
- Mimics Claude in Chrome extension tools
- Accessibility tree with numbered refs (`[7] QPushButton "Submit"`)
- Semantic form filling and clicking
- Best for: Form automation, data entry, structured UI interaction

---

# Computer Use Mode

## Tools (Matches Anthropic's Computer Use API)

### 1. `computer` Tool (Matches `computer_20250124`)

**Schema:**
```json
{
  "name": "computer",
  "type": "computer_20250124",
  "display_width_px": 1920,
  "display_height_px": 1080
}
```

**Actions:**
| Action | Parameters | Description |
|--------|------------|-------------|
| `screenshot` | - | Capture current Qt window state |
| `left_click` | `coordinate: [x, y]` | Click at pixel coordinates |
| `right_click` | `coordinate: [x, y]` | Right-click at coordinates |
| `double_click` | `coordinate: [x, y]` | Double-click at coordinates |
| `mouse_move` | `coordinate: [x, y]` | Move cursor to coordinates |
| `left_click_drag` | `start_coordinate`, `coordinate` | Drag from start to end |
| `type` | `text: string` | Type text at current focus |
| `key` | `text: string` | Send key combo (e.g., "ctrl+s") |
| `scroll` | `coordinate`, `direction`, `amount` | Scroll at location |
| `cursor_position` | - | Get current cursor position |

**Example Request:**
```json
{
  "action": "left_click",
  "coordinate": [450, 200]
}
```

**Example Response:**
```json
{
  "type": "tool_result",
  "content": [
    {"type": "image", "source": {"type": "base64", "data": "..."}}
  ]
}
```

---

# Chrome Mode

## Tools (Matches Claude in Chrome Extension)

### 1. `read_page` Tool

Returns an **accessibility tree** representation of the Qt widget hierarchy, with interactive elements labeled for easy reference.

**Schema:**
```json
{
  "name": "read_page",
  "description": "Get accessibility tree representation of Qt application widgets",
  "input_schema": {
    "type": "object",
    "properties": {
      "include_invisible": {
        "type": "boolean",
        "default": false
      }
    }
  }
}
```

**Example Response:**
```
[1] QMainWindow "My Application" (1920x1080)
├── [2] QMenuBar
│   ├── [3] QMenu "File"
│   ├── [4] QMenu "Edit"
│   └── [5] QMenu "Help"
├── [6] QToolBar
│   ├── [7] QToolButton "New" (clickable)
│   └── [8] QToolButton "Open" (clickable)
├── [9] QWidget "centralWidget"
│   ├── [10] QLabel "Enter your name:"
│   ├── [11] QLineEdit (editable, focused) value=""
│   ├── [12] QPushButton "Submit" (clickable)
│   └── [13] QTextEdit (editable) value="Welcome..."
└── [14] QStatusBar "Ready"

Interactive elements:
- [7] QToolButton "New" at (50, 80) - click to create new document
- [8] QToolButton "Open" at (90, 80) - click to open file
- [11] QLineEdit at (200, 150) - text input field
- [12] QPushButton "Submit" at (200, 200) - submit form
- [13] QTextEdit at (50, 250) - multi-line text editor
```

### 2. `form_input` Tool

Set values in form elements by reference ID.

**Schema:**
```json
{
  "name": "form_input",
  "description": "Set value in a Qt input widget",
  "input_schema": {
    "type": "object",
    "properties": {
      "ref": {
        "type": "integer",
        "description": "Element reference from read_page"
      },
      "value": {
        "type": "string",
        "description": "Value to set"
      }
    },
    "required": ["ref", "value"]
  }
}
```

**Example:**
```json
{"ref": 11, "value": "John Doe"}
```

### 3. `click` Tool (Semantic Click)

Click an element by reference ID (no coordinates needed).

**Schema:**
```json
{
  "name": "click",
  "description": "Click a Qt widget by reference",
  "input_schema": {
    "type": "object",
    "properties": {
      "ref": {
        "type": "integer",
        "description": "Element reference from read_page"
      },
      "button": {
        "type": "string",
        "enum": ["left", "right", "middle"],
        "default": "left"
      }
    },
    "required": ["ref"]
  }
}
```

### 4. `get_page_text` Tool

Extract all visible text content.

**Schema:**
```json
{
  "name": "get_page_text",
  "description": "Extract text content from Qt application",
  "input_schema": {
    "type": "object",
    "properties": {}
  }
}
```

**Example Response:**
```
My Application

File  Edit  Help

[New] [Open]

Enter your name:
[________________]

[Submit]

Welcome to the application. Please enter your information above.

Ready
```

### 5. `navigate` Tool

For Qt applications with navigation (tabs, stacks, etc.).

**Schema:**
```json
{
  "name": "navigate",
  "description": "Navigate within the Qt application",
  "input_schema": {
    "type": "object",
    "properties": {
      "action": {
        "type": "string",
        "enum": ["back", "forward", "tab", "menu"]
      },
      "target": {
        "type": "string",
        "description": "Tab name, menu path, etc."
      }
    }
  }
}
```

### 6. `find` Tool

Find elements using natural language queries (like Chrome's find tool).

**Schema:**
```json
{
  "name": "find",
  "description": "Find Qt widgets matching a natural language description",
  "input_schema": {
    "type": "object",
    "properties": {
      "query": {
        "type": "string",
        "description": "Natural language description of element to find"
      }
    },
    "required": ["query"]
  }
}
```

**Example:**
```json
{"query": "the submit button"}
```

**Response:**
```json
{
  "matches": [
    {"ref": 12, "class": "QPushButton", "text": "Submit", "confidence": 0.95}
  ]
}
```

### 7. `tabs_context` Tool

Get information about Qt application windows/tabs.

**Schema:**
```json
{
  "name": "tabs_context",
  "description": "Get context about Qt application windows",
  "input_schema": {
    "type": "object",
    "properties": {}
  }
}
```

**Response:**
```json
{
  "windows": [
    {"id": 1, "title": "My Application", "active": true, "size": [1920, 1080]},
    {"id": 2, "title": "Settings", "active": false, "size": [400, 300]}
  ],
  "activeWindow": 1
}
```

### 8. `read_console_messages` Tool (Developer Mode)

Read Qt debug output (qDebug, qWarning, etc.).

**Schema:**
```json
{
  "name": "read_console_messages",
  "description": "Read Qt debug/warning/error messages",
  "input_schema": {
    "type": "object",
    "properties": {
      "level": {
        "type": "string",
        "enum": ["all", "debug", "warning", "critical", "fatal"],
        "default": "all"
      },
      "limit": {
        "type": "integer",
        "default": 50
      }
    }
  }
}
```

## Accessibility Tree Generation

The key innovation is generating Chrome-like accessibility trees from Qt's widget hierarchy.

### Mapping Qt to Accessibility Concepts

| Qt Widget | Accessibility Role | Behaviors |
|-----------|-------------------|-----------|
| QPushButton | button | clickable |
| QLineEdit | textbox | editable |
| QTextEdit | textbox | editable, multiline |
| QComboBox | combobox | expandable, selectable |
| QCheckBox | checkbox | checkable |
| QRadioButton | radio | checkable |
| QSlider | slider | adjustable |
| QSpinBox | spinbutton | adjustable, editable |
| QLabel | statictext | - |
| QMenu | menu | expandable |
| QMenuBar | menubar | - |
| QToolButton | button | clickable |
| QListWidget | list | selectable |
| QTableWidget | table | - |
| QTreeWidget | tree | expandable |
| QTabWidget | tablist | - |
| QTabBar | tablist | selectable |

### Reference ID Assignment

Elements get sequential integer IDs for easy reference:
- `[1]` - Root window
- `[2]` - First child
- etc.

This matches how Chrome extension assigns `@e1`, `@e2` etc.

## Implementation in C++ Probe

```cpp
// qtpilot_compat.h - Computer Use compatibility layer

struct ComputerAction {
    QString action;      // screenshot, left_click, type, etc.
    QPoint coordinate;   // for mouse actions
    QString text;        // for type/key actions
    QString direction;   // for scroll
    int amount;          // for scroll
};

class qtPilotCompat {
public:
    // Computer tool actions
    QJsonObject screenshot();
    QJsonObject leftClick(int x, int y);
    QJsonObject rightClick(int x, int y);
    QJsonObject doubleClick(int x, int y);
    QJsonObject mouseMove(int x, int y);
    QJsonObject type(const QString& text);
    QJsonObject key(const QString& combo);
    QJsonObject scroll(int x, int y, const QString& direction, int amount);
    
    // Chrome-like tools
    QString readPage(bool includeInvisible = false);
    QString getPageText();
    QJsonObject formInput(int ref, const QString& value);
    QJsonObject click(int ref, const QString& button = "left");
    
    // Ref tracking
    QMap<int, QObject*> refToObject;
    int nextRef = 1;
    
private:
    QString buildAccessibilityTree(QObject* root, int depth = 0);
    void assignRefs(QObject* root);
};
```

## Protocol Messages

### Request (Computer Action)
```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "computer",
  "params": {
    "action": "left_click",
    "coordinate": [450, 200]
  }
}
```

### Response (With Screenshot)
```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "success": true,
    "screenshot": {
      "type": "base64",
      "media_type": "image/png",
      "data": "iVBORw0KGgo..."
    }
  }
}
```

### Request (Read Page)
```json
{
  "jsonrpc": "2.0",
  "id": 2,
  "method": "read_page",
  "params": {}
}
```

### Response (Accessibility Tree)
```json
{
  "jsonrpc": "2.0",
  "id": 2,
  "result": {
    "tree": "[1] QMainWindow \"My App\"...",
    "refs": {
      "7": {"class": "QToolButton", "name": "New", "x": 50, "y": 80},
      "11": {"class": "QLineEdit", "name": "", "x": 200, "y": 150}
    }
  }
}
```

---

# MCP Tool Definitions

## Computer Use Mode Tools

```json
{
  "tools": [
    {
      "name": "computer",
      "description": "Control Qt application via mouse and keyboard. Take screenshots to see current state. Actions: screenshot, left_click, right_click, double_click, mouse_move, left_click_drag, type, key, scroll, cursor_position.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "action": {
            "type": "string",
            "enum": ["screenshot", "left_click", "right_click", "double_click", 
                     "mouse_move", "left_click_drag", "type", "key", "scroll", "cursor_position"]
          },
          "coordinate": {
            "type": "array",
            "items": {"type": "integer"},
            "description": "[x, y] pixel coordinates for click/move actions"
          },
          "start_coordinate": {
            "type": "array",
            "items": {"type": "integer"},
            "description": "[x, y] start coordinates for drag action"
          },
          "text": {
            "type": "string",
            "description": "Text to type or key combination (e.g., 'ctrl+s')"
          },
          "direction": {
            "type": "string",
            "enum": ["up", "down", "left", "right"],
            "description": "Scroll direction"
          },
          "amount": {
            "type": "integer",
            "description": "Scroll amount in pixels"
          }
        },
        "required": ["action"]
      }
    }
  ]
}
```

## Chrome Mode Tools

```json
{
  "tools": [
    {
      "name": "read_page",
      "description": "Get accessibility tree of Qt widgets with numbered references like [7] QPushButton 'Submit'. Use these refs with click() and form_input().",
      "inputSchema": {
        "type": "object",
        "properties": {
          "include_invisible": {"type": "boolean", "default": false}
        }
      }
    },
    {
      "name": "click",
      "description": "Click a Qt widget by its reference number from read_page",
      "inputSchema": {
        "type": "object",
        "properties": {
          "ref": {"type": "integer", "description": "Element reference from read_page"},
          "button": {"type": "string", "enum": ["left", "right", "middle"], "default": "left"}
        },
        "required": ["ref"]
      }
    },
    {
      "name": "form_input",
      "description": "Set value in a Qt input widget (QLineEdit, QTextEdit, QSpinBox, etc.) by reference",
      "inputSchema": {
        "type": "object",
        "properties": {
          "ref": {"type": "integer", "description": "Element reference from read_page"},
          "value": {"type": "string", "description": "Value to set"}
        },
        "required": ["ref", "value"]
      }
    },
    {
      "name": "get_page_text",
      "description": "Extract all visible text content from the Qt application",
      "inputSchema": {
        "type": "object",
        "properties": {}
      }
    },
    {
      "name": "find",
      "description": "Find Qt widgets matching a natural language description",
      "inputSchema": {
        "type": "object",
        "properties": {
          "query": {"type": "string", "description": "Natural language description like 'submit button' or 'email field'"}
        },
        "required": ["query"]
      }
    },
    {
      "name": "navigate",
      "description": "Navigate within the Qt application (tabs, menus, back/forward)",
      "inputSchema": {
        "type": "object",
        "properties": {
          "action": {"type": "string", "enum": ["back", "forward", "tab", "menu"]},
          "target": {"type": "string", "description": "Tab name or menu path like 'File > Save'"}
        }
      }
    },
    {
      "name": "tabs_context",
      "description": "Get information about Qt application windows",
      "inputSchema": {
        "type": "object",
        "properties": {}
      }
    },
    {
      "name": "read_console_messages",
      "description": "Read Qt debug/warning/error messages (qDebug, qWarning, etc.)",
      "inputSchema": {
        "type": "object",
        "properties": {
          "level": {"type": "string", "enum": ["all", "debug", "warning", "critical"], "default": "all"},
          "limit": {"type": "integer", "default": 50}
        }
      }
    }
  ]
}
```

---

# Usage Patterns Claude Already Knows

## Computer Use Mode Patterns

### Pattern 1: Screenshot-First Exploration
```
Claude: Let me take a screenshot to see the current state.
→ computer(action="screenshot")

Claude: I can see a form with a name field at approximately (200, 150). 
        Let me click on it and enter text.
→ computer(action="left_click", coordinate=[200, 150])
→ computer(action="type", text="John Doe")
→ computer(action="key", text="Tab")
→ computer(action="type", text="john@example.com")
```

### Pattern 2: Visual Verification Loop
```
Claude: I'll click the submit button I see at (300, 250).
→ computer(action="left_click", coordinate=[300, 250])

Claude: Let me verify the action succeeded.
→ computer(action="screenshot")

Claude: I can see a success message appeared. The form was submitted.
```

## Chrome Mode Patterns

### Pattern 3: Accessibility Tree Navigation
```
Claude: Let me read the page structure.
→ read_page()

Response:
[1] QMainWindow "My App"
├── [7] QLineEdit "nameInput" (editable)
├── [8] QLineEdit "emailInput" (editable)  
└── [9] QPushButton "Submit" (clickable)

Claude: I see the form fields. I'll fill them by reference.
→ form_input(ref=7, value="John Doe")
→ form_input(ref=8, value="john@example.com")
→ click(ref=9)
```

### Pattern 4: Natural Language Find
```
Claude: Let me find the submit button.
→ find(query="submit button")

Response: {"matches": [{"ref": 9, "text": "Submit", "confidence": 0.95}]}

Claude: Found it at ref 9. Clicking now.
→ click(ref=9)
```

## Hybrid Patterns (Both Modes)

### Pattern 5: Tree + Visual Verification
```
Claude: Let me understand the layout first.
→ read_page()

Claude: I see the structure. Let me verify visually.
→ computer(action="screenshot")

Claude: The tree matches what I see. I'll use refs for interaction.
→ form_input(ref=7, value="John Doe")
→ click(ref=9)

Claude: Let me verify the result visually.
→ computer(action="screenshot")
```

### Pattern 6: Fallback from Refs to Coordinates
```
Claude: Let me try clicking the custom widget.
→ click(ref=15)

Response: {"error": "Element not interactable - custom paint widget"}

Claude: The widget doesn't support ref-based clicking. 
        Let me use coordinates from the screenshot.
→ computer(action="screenshot")
→ computer(action="left_click", coordinate=[400, 300])
```

## Configuration

Enable compatibility mode via environment variable:

```bash
QTPILOT_MODE=native           # Default - full Qt introspection
QTPILOT_MODE=computer_use     # Screenshot + coordinate mode
QTPILOT_MODE=chrome           # Accessibility tree + refs mode
QTPILOT_MODE=all              # Expose all tools from all modes
QTPILOT_PORT=9999
```

Or via WebSocket command:
```json
{"method": "setMode", "params": {"mode": "chrome"}}
```

## Benefits

1. **Zero Learning Curve** - Claude already knows these patterns
2. **Battle-Tested Schemas** - Anthropic refined these through extensive use
3. **Familiar Error Handling** - Claude knows how to recover from common issues
4. **Triple Mode Flexibility** - Use the right tool for the job
5. **Future Proof** - As Anthropic improves their tools, we can adopt changes

## Comparison: All Three Modes

| Feature | Native qtPilot | Computer Use | Chrome |
|---------|-------------|--------------|--------|
| Learning curve | New API | Zero | Zero |
| Object ID | Hierarchical paths | Coordinates | Integer refs |
| Visual feedback | Optional | Screenshot-driven | Tree-driven |
| Click method | By path | By pixel | By ref |
| Introspection | Full Qt meta-object | Visual only | Widget tree |
| Signal monitoring | Yes | No | No |
| Property access | Full read/write | No | Via form_input |
| Method invocation | Yes | No | No |
| Best for | Test automation | Visual tasks, games | Forms, data entry |
| Claude familiarity | None | High | High |

## When to Use Each Mode

### Use Native qtPilot when:
- Writing automated tests
- Need signal/slot monitoring
- Need to invoke arbitrary methods
- Deep debugging

### Use Computer Use when:
- Custom-painted widgets (no accessibility info)
- Games or canvas-based UIs
- Need pixel-precise clicking
- Visual verification needed

### Use Chrome when:
- Form filling and data entry
- Standard widget interactions
- Want fastest Claude response
- Semantic element selection

## Implementation Priority

### Phase 1: Computer Use Mode
1. `computer` tool (screenshot, click, type, key)
2. Coordinate-based mouse actions
3. Screenshot capture and encoding

### Phase 2: Chrome Mode  
1. `read_page` with accessibility tree generation
2. `form_input` and `click` by reference
3. `get_page_text`
4. `find` with fuzzy matching

### Phase 3: Integration
1. `navigate` for tabs/menus
2. `tabs_context` for multi-window
3. `read_console_messages` for debugging
4. Mode switching at runtime

### Phase 4: MCP Server
1. Tool definitions for all modes
2. Mode selection via config
3. Claude Desktop integration

## Conclusion

By implementing both Computer Use and Chrome compatibility modes, qtPilot becomes immediately usable by Claude without any special training or prompting. 

- **Computer Use mode** lets Claude interact visually, taking screenshots and clicking by coordinates
- **Chrome mode** lets Claude work semantically, reading the widget tree and clicking by reference

Claude will naturally choose the right approach - screenshots for visual verification, accessibility tree for form filling. This is the fastest path to making Qt applications AI-controllable.
