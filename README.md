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
- **Cross-platform** - Windows and Linux (macOS planned)

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
| **Linux x64** | ✅ | ✅ | Supported |
| **macOS** | - | - | Planned (v1.1) |

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
