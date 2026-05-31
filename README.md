# qtPilot

Qt MCP server enabling AI assistants to introspect and control Qt applications.

## What is qtPilot?

qtPilot lets Claude and other AI assistants interact with any Qt desktop application. It works by injecting a lightweight probe into the target application that exposes its UI through the [Model Context Protocol (MCP)](https://modelcontextprotocol.io/).

**No source code modifications required** - works with any Qt application.

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
│  │  │  │ Tracker      │  │              │  │                  │   │  │  │
│  │  │  └──────────────┘  └──────────────┘  └────────┬─────────┘   │  │  │
│  │  └───────────────────────────────────────────────│─────────────┘  │  │
│  └──────────────────────────────────────────────────│────────────────┘  │
│                                                     │ WebSocket         │
│  ┌──────────────────────────────────────────────────▼────────────────┐  │
│  │  Qt MCP Server (Python)                                           │  │
│  │  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐   │  │
│  │  │ qtPilot Client    │  │ MCP Tools       │  │ stdio Transport │   │  │
│  │  │ (WebSocket)     │  │                 │  │                 │   │  │
│  │  └─────────────────┘  └─────────────────┘  └─────────────────┘   │  │
│  └───────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
                           ┌─────────────────┐
                           │  Claude / LLM   │
                           └─────────────────┘
```

## Quick Start

### Try it now

```bash
pip install qtpilot
qtpilot download-tools --qt-version 6.8
qtpilot serve --demo
```

This downloads everything you need — probe, launcher, and a bundled test app with Qt runtime. Once running, Claude can interact with the test app. Try asking "Show me the widget tree" or "Fill out the form with my name."

### Option 1: pip install (Recommended)

```bash
pip install qtpilot

# Download probe + launcher for your Qt version
qtpilot download-tools --qt-version 6.8

# Launch your app with the probe and start the MCP server
qtpilot serve --mode native --target /path/to/your-qt-app
```

See [python/README.md](python/README.md) for complete CLI documentation.

### Option 2: Build from Source

```bash
git clone https://github.com/ssss2art/qtPilot.git
cd qtPilot
cmake -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.0/gcc_64
cmake --build build
```

See [docs/BUILDING.md](docs/BUILDING.md) for detailed build instructions.

## Features

- **Three API modes** for different use cases:
  - **Native** - Full Qt object tree introspection
  - **Computer Use** - Screenshot-based interaction (Anthropic API compatible)
  - **Chrome** - Accessibility tree with refs (Claude in Chrome compatible)
- **53 MCP tools** for Qt introspection and automation
- **Works with Qt 5.15.1+ and Qt 6.5+** applications
- **Zero modification** to target applications required
- **Child process injection** - `--inject-children` automatically injects the probe into child processes (Windows: Detours hook on CreateProcessW; Linux: LD_PRELOAD propagation)
- **Admin elevation** - `--run-as-admin` launches target apps with administrator privileges on Windows (auto-elevates via UAC)
- **Cross-platform** - Windows, Linux, and macOS

## Connecting to Claude

### Claude Desktop

Add to `claude_desktop_config.json`:

```json
{
  "mcpServers": {
    "qtpilot": {
      "command": "qtpilot",
      "args": ["serve", "--mode", "native", "--target", "/path/to/your/qt-app"]
    }
  }
}
```

### Claude Code

```bash
claude mcp add --transport stdio qtpilot -- qtpilot serve --mode native --ws-url ws://localhost:9222
```

## Examples

Once connected, just ask Claude what you want to do with your Qt app:

> "Show me the widget tree of this app"

Uses native mode to walk the Qt object hierarchy — great for understanding an unfamiliar UI.

> "Type 'John Doe' into the name field and click Submit"

Claude finds the widget by name, clicks to focus, types the text, then clicks the button.

> "What data is in the table?"

Reads Qt model data directly — no screenshot parsing needed.

> "Take a screenshot and tell me what you see"

Uses computer use mode to capture the window and describe the UI visually.

> "Find all the buttons on this page"

Uses chrome mode's accessibility tree to locate interactive elements by role.

### Try the full test suite

qtPilot includes a Claude Code skill that runs a comprehensive 39-test E2E suite across all three modes, plus logging and recording. To run it against the included test app:

```
/test-mcp-modes
```

See [`.claude/skills/test-mcp-modes/SKILL.md`](.claude/skills/test-mcp-modes/SKILL.md) for details.

## Documentation

| Document | Description |
|----------|-------------|
| [Getting Started](docs/GETTING-STARTED.md) | Installation and first steps |
| [Building from Source](docs/BUILDING.md) | Compile qtPilot yourself |
| [API Reference](qtPilot-specification.md) | Complete tool and protocol documentation |
| [API Modes](qtPilot-compatibility-modes.md) | Detailed mode comparisons |
| [Troubleshooting](docs/TROUBLESHOOTING.md) | Common issues and solutions |
| [Python CLI](python/README.md) | qtpilot command documentation |

## Platform Support

| Platform | Qt 5.15 | Qt 6.5+ | Status |
|----------|---------|---------|--------|
| **Windows x64** | ✅ | ✅ | Supported |
| **Windows x86** | ✅ | - | Supported (Qt 5.15 only) |
| **Linux x64** | ✅ | ✅ | Supported |
| **macOS arm64** | - | ✅ | Supported (tested against Qt 6.10) |

## Requirements

- **Runtime:** Python 3.11+ (for MCP server)
- **Target apps:** Qt 5.15+ or Qt 6.5+
- **Build:** CMake 3.16+, C++17 compiler

## License

MIT License - see [LICENSE](LICENSE) for details.

## Links

- [GitHub Repository](https://github.com/ssss2art/qtPilot)
- [Releases & Probe Downloads](https://github.com/ssss2art/qtPilot/releases)
- [Issue Tracker](https://github.com/ssss2art/qtPilot/issues)
