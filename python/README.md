# qtpilot

MCP server for controlling Qt applications via the qtPilot probe.

qtPilot enables Claude and other MCP-compatible AI assistants to interact with Qt desktop applications through a native probe that exposes the Qt object tree, properties, signals, and visual state.

## Installation

```bash
pip install qtpilot
```

## Quick Start

1. **Download the tools** for your Qt version:

```bash
# Download probe + launcher matching your app's Qt version
qtpilot download-tools --qt-version 6.8

# Other available versions: 5.15, 6.5, 6.8, 6.9
qtpilot download-tools --qt-version 5.15

# Extract to a specific directory
qtpilot download-tools --qt-version 6.8 --output ./tools
```

2. **Launch your Qt application** with the probe:

```bash
# Auto-launch target app with probe injection
qtpilot serve --mode native --target /path/to/your-qt-app.exe
```

3. **Connect Claude** to the MCP server via your client configuration.

## Features

- **Three API modes**: Native (full Qt access), Computer Use (screenshots + clicks), Chrome (DevTools-compatible)
- **53 MCP tools** for Qt introspection and automation
- **Works with Qt 5.15 and Qt 6.x** applications
- **Zero modification** to target applications required

## Server Modes

```bash
# Native mode - full Qt object tree access
qtpilot serve --mode native --ws-url ws://localhost:9222

# Chrome mode - DevTools-compatible protocol
qtpilot serve --mode chrome --target /path/to/app.exe

# Computer Use mode - screenshot-based interaction
qtpilot serve --mode cu --ws-url ws://localhost:9222
```

## Claude Desktop Configuration

Add to your `claude_desktop_config.json`:

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

## Claude Code Configuration

```bash
claude mcp add --transport stdio qtpilot -- qtpilot serve --mode native --ws-url ws://localhost:9222
```

## Requirements

- Python 3.11 or later
- Qt application with qtPilot probe loaded
- Windows or Linux (macOS support planned)

## Links

- [Full Documentation](https://github.com/ssss2art/qtPilot#readme)
- [Releases & Probe Downloads](https://github.com/ssss2art/qtPilot/releases)
- [Issue Tracker](https://github.com/ssss2art/qtPilot/issues)

## License

MIT License - see [LICENSE](https://github.com/ssss2art/qtPilot/blob/main/LICENSE) for details.
