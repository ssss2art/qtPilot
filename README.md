# qtPilot

Qt MCP server enabling AI assistants to introspect and control Qt applications.

## What is qtPilot?

qtPilot lets Claude and other AI assistants interact with any Qt application. It works by getting a lightweight probe into the target application, which exposes its UI through the [Model Context Protocol (MCP)](https://modelcontextprotocol.io/).

**On desktop, no source code modifications are required** - the probe is injected into any
already-built Qt application. On Android and iOS, where no platform mechanism can insert a
library into a running app, the probe is instead linked into a development build of your own
app; see [docs/MOBILE.md](docs/MOBILE.md).

```
┌─────────────────────────────────────────────────────────────────────────┐
│  TARGET MACHINE OR DEVICE                                               │
│                                                                         │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │  Qt Application                                                   │  │
│  │  ┌─────────────────────────────────────────────────────────────┐  │  │
│  │  │  qtPilot Probe                                              │  │  │
│  │  │    desktop: INJECTED  (qtPilot-probe.dll / .so / .dylib)    │  │  │
│  │  │    mobile:  LINKED IN (libqtPilot-probe.a, dev build only)  │  │  │
│  │  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────────┐   │  │  │
│  │  │  │ Object       │  │ Introspector │  │ WebSocket Server │   │  │  │
│  │  │  │ Tracker      │  │              │  │ :9222            │   │  │  │
│  │  │  └──────────────┘  └──────────────┘  └────────┬─────────┘   │  │  │
│  │  └───────────────────────────────────────────────┼─────────────┘  │  │
│  └──────────────────────────────────────────────────┼────────────────┘  │
└─────────────────────────────────────────────────────┼───────────────────┘
                                                      │ WebSocket
   ┌──────────────────────────────────────────────────▼────────────────┐
   │  qtPilot MCP Server (Python)                                      │
   │  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐    │
   │  │ Probe Client    │  │ MCP Tools       │  │ stdio Transport │    │
   │  │ (WebSocket)     │  │ (58, 3 modes)   │  │                 │    │
   │  └─────────────────┘  └─────────────────┘  └─────────────────┘    │
   └───────────────────────────────────┬───────────────────────────────┘
                                       │ MCP
                              ┌────────▼────────┐
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

### Option 1: uvx (no install)

[uv](https://docs.astral.sh/uv/) runs qtPilot in a throwaway, isolated
environment — nothing lands in your global or project Python.

```bash
# Run straight from PyPI
uvx qtpilot download-tools --qt-version 6.8
uvx qtpilot serve --mode native --target /path/to/your-qt-app
```

Because each `uvx` invocation resolves its own environment, this is also how you
pick an MCP protocol revision without disturbing anything else:

```bash
# MCP 2026-07-28 (stateless), the default resolve since FastMCP 4 shipped stable.
uvx --from 'qtpilot[mcp-next]' qtpilot serve --mode all
```

To wire it into an MCP client, use [`.mcp.uvx.json`](.mcp.uvx.json) instead of
`.mcp.json`. See [docs/MCP-CONFORMANCE.md](docs/MCP-CONFORMANCE.md) for what
each revision changes.

### Option 2: pip install

```bash
pip install qtpilot

# Download probe + launcher for your Qt version
qtpilot download-tools --qt-version 6.8

# Launch your app with the probe and start the MCP server
qtpilot serve --mode native --target /path/to/your-qt-app
```

See [python/README.md](python/README.md) for complete CLI documentation.

### Option 3: Build from Source

```bash
git clone https://github.com/ssss2art/qtPilot.git
cd qtPilot
cmake -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.0/gcc_64
cmake --build build
```

See [docs/BUILDING.md](docs/BUILDING.md) for detailed build instructions.

### Option 3: Mobile (Android & iOS)

Cross-compile the static probe using Qt's `qt-cmake` wrapper and link it into a development build of your app (see [docs/MOBILE.md](docs/MOBILE.md)). Once running on device or emulator, forward the port over USB and connect:

```bash
# Android
adb forward tcp:9222 tcp:9222

# iOS
iproxy 9222 9222

# Start the MCP server connected to the device probe
qtpilot serve --mode native --ws-url ws://localhost:9222
```

## Features

- **Three API modes** for different use cases:
  - **Native** - Full Qt object tree introspection
  - **Computer Use** - Screenshot and coordinate-based interaction
  - **Chrome** - Browser-style accessibility tree with element references
- **58 MCP tools** across all modes: 27 native, 13 computer-use, 8 accessibility, and 10 shared session tools
- **Works with Qt 5.15.1+ and Qt 6.5+** applications (Qt 5.15 on Windows and Linux; macOS needs Qt 6.5+)
- **Zero modification** to target applications on desktop (the probe is injected); on mobile it is linked into your own development build
- **Child process injection** - `--inject-children` automatically injects the probe into child processes (Windows: Detours hook on CreateProcessW; Linux: LD_PRELOAD propagation)
- **Admin elevation** - `--run-as-admin` launches target apps with administrator privileges on Windows (auto-elevates via UAC)
- **Cross-platform** - Windows, Linux, and macOS by injection; Android and iOS by linking the probe into a development build

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

Or for a mobile/remote device:

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
claude mcp add --transport stdio qtpilot -- qtpilot serve --mode native --target /path/to/your/qt-app

# Mobile / Remote probe
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
| [Mobile (Android/iOS)](docs/MOBILE.md) | Linking the probe into a device build |
| [MCP Tooling](docs/MCP-TOOLS.md) | Current modes, tool surface, resources, and inspection workflow |
| [API Modes](qtPilot-compatibility-modes.md) | Mode selection and current tool families |
| [Probe Protocol Design](qtPilot-specification.md) | Historical design reference for the probe protocol |
| [Troubleshooting](docs/TROUBLESHOOTING.md) | Common issues and solutions |
| [Python CLI](python/README.md) | qtpilot command documentation |

## Platform Support

| Platform | Qt 5.15 | Qt 6.5+ | Probe delivery | Status |
|----------|---------|---------|----------------|--------|
| **Windows x64** | ✅ | ✅ | Injected | Supported |
| **Windows x86** | ✅ | - | Injected | Supported (Qt 5.15 only) |
| **Linux x64** | ✅ | ✅ | Injected | Supported |
| **macOS arm64** | - | ✅ | Injected | Supported (tested against Qt 6.10) |
| **Android** | - | ✅ | Linked into your build | Supported (CI cross-compiles; device runs verified by hand) |
| **iOS (device)** | - | ✅ | Linked into your build | Supported (CI cross-compiles; device runs verified by hand) |

✅ = supported and exercised by CI. `-` = not a supported combination.

**Qt 5.15 on macOS is not supported.** It is not covered by CI and cannot
practically be: open-source Qt 5 ended at 5.15.2, whose macOS build is
x86_64-only, so covering it would need a retired x86_64 runner image or a
commercial Qt licence. The code is not deliberately incompatible and a local
build may well work, but nothing verifies it and regressions will not be caught.
Use Qt 6.5+ on macOS.

Published binary assets vary by release. In particular, macOS users may need to
build the probe and launcher from source; see [docs/MACOS.md](docs/MACOS.md).

Mobile is a different workflow, not just another download: there is no launcher
and no injection, the probe ships as a static library you link into a
development build, and it must never be enabled in a build you distribute. See
[docs/MOBILE.md](docs/MOBILE.md).

## Requirements

- **Runtime:** Python 3.11+ (for MCP server)
- **Target apps:** Qt 5.15+ or Qt 6.5+ (on macOS, Qt 6.5+ only — see Platform Support)
- **Build:** CMake 3.16+, C++17 compiler

## License

MIT License - see [LICENSE](LICENSE) for details.

## Links

- [GitHub Repository](https://github.com/ssss2art/qtPilot)
- [Releases & Probe Downloads](https://github.com/ssss2art/qtPilot/releases)
- [Issue Tracker](https://github.com/ssss2art/qtPilot/issues)
