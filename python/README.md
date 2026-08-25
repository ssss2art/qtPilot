# qtpilot

MCP server for controlling Qt applications via the qtPilot probe.

qtPilot enables Claude and other MCP-compatible AI assistants to interact with Qt applications through a native probe that exposes the Qt object tree, properties, signals, and visual state.

## Installation

```bash
pip install qtpilot
```

## Quick Start

### Desktop (Injected)

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

### Mobile / Remote (Linked Probe)

On Android and iOS, the probe is linked into a development build of your app (see [docs/MOBILE.md](../docs/MOBILE.md)). Once running on device/emulator, forward the port over USB:

```bash
# Android
adb forward tcp:9222 tcp:9222

# iOS
iproxy 9222 9222

# Connect the MCP server to the probe
qtpilot serve --mode native --ws-url ws://localhost:9222
```

## Features

- **Three API modes**: Native (full Qt access), Computer Use (screenshots + clicks), Chrome (accessibility tree)
- **58 MCP tools** when using `--mode all` (mode-specific tools plus shared session tools)
- **Works with Qt 5.15 and Qt 6.x** applications
- **Zero modification on desktop** (probe is injected); statically linked into development builds on Android/iOS
- **Cross-platform**: Windows, Linux, macOS, Android, and iOS

## Server Modes

```bash
# Native mode - full Qt object tree access
qtpilot serve --mode native --ws-url ws://localhost:9222

# Chrome mode - DevTools / accessibility protocol
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

Or for a mobile / remote device:

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

## Claude Code Configuration

```bash
# Desktop (auto-launch)
claude mcp add --transport stdio qtpilot -- qtpilot serve --mode native --target /path/to/your-qt-app

# Mobile / Remote probe
claude mcp add --transport stdio qtpilot -- qtpilot serve --mode native --ws-url ws://localhost:9222
```

## Requirements

- Python 3.11 or later
- Qt application with qtPilot probe loaded (injected on desktop, linked on mobile)
- Target platforms: Windows, Linux, macOS (may require source build), Android, iOS

## Links

- [Full Documentation](https://github.com/ssss2art/qtPilot#readme)
- [Mobile Documentation](https://github.com/ssss2art/qtPilot/blob/main/docs/MOBILE.md)
- [Releases & Probe Downloads](https://github.com/ssss2art/qtPilot/releases)
- [Issue Tracker](https://github.com/ssss2art/qtPilot/issues)

## License

MIT License - see [LICENSE](https://github.com/ssss2art/qtPilot/blob/main/LICENSE) for details.
