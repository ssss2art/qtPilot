# Spec: Package QtMcp as a Claude Code Plugin

## Context

QtMcp already functions as an MCP server that Claude Code can use, but it requires manual configuration in `settings.json`. Packaging it as a Claude Code plugin adds auto-registration of the MCP server, user-facing commands, model-invoked skills, and distribution via a plugin marketplace.

## Plugin Directory Structure

```
plugin/
├── .claude-plugin/
│   └── plugin.json
├── .mcp.json
├── commands/
│   ├── connect.md
│   ├── launch.md
│   └── download-probe.md
├── skills/
│   └── qt-automation/
│       └── SKILL.md
└── agents/
    └── qt-inspector.md
```

This lives inside the QtMcp repo at `plugin/`.

## 1. Plugin Manifest

**File:** `.claude-plugin/plugin.json`

```json
{
  "name": "qtmcp",
  "version": "0.1.0",
  "description": "Control and inspect Qt applications via MCP — introspection, screenshots, input simulation, accessibility trees",
  "author": {
    "name": "Scott Johnson"
  },
  "repository": "https://github.com/ssss2art/QtMcp",
  "license": "MIT",
  "keywords": ["qt", "mcp", "automation", "testing", "gui", "introspection"],
  "commands": "./commands/",
  "skills": "./skills/",
  "agents": "./agents/",
  "mcpServers": "./.mcp.json"
}
```

## 2. MCP Server Configuration

**File:** `.mcp.json`

```json
{
  "qtmcp-native": {
    "command": "uvx",
    "args": ["qtmcp", "serve", "--mode", "native"]
  },
  "qtmcp-cu": {
    "command": "uvx",
    "args": ["qtmcp", "serve", "--mode", "cu"]
  },
  "qtmcp-chrome": {
    "command": "uvx",
    "args": ["qtmcp", "serve", "--mode", "chrome"]
  }
}
```

**Design decisions:**
- Use `uvx` so users don't need to pre-install the Python package
- Register one server per mode — users enable only the modes they need
- The server auto-starts when Claude Code loads the plugin
- Probe connection happens lazily via `qtmcp_list_probes` / `qtmcp_connect_probe` tools

## 3. Commands

### `/qtmcp:connect`

**File:** `commands/connect.md`

Purpose: Guide the user through connecting to a running Qt application.

Behavior:
1. Call `qtmcp_list_probes` to discover running probes
2. If one probe found, auto-connect
3. If multiple, show list and ask user to pick
4. If none, suggest launching an app with `/qtmcp:launch`

### `/qtmcp:launch`

**File:** `commands/launch.md`

Argument: `<path-to-qt-app>`

Purpose: Launch a Qt application with the probe injected and connect to it.

Behavior:
1. Auto-download launcher and probe binaries if not cached
2. Launch target with probe injected
3. Wait for connection confirmation
4. Report connected status and available tools

### `/qtmcp:download-probe`

**File:** `commands/download-probe.md`

Purpose: Download the pre-built probe binary for a specific Qt version.

Behavior:
1. Ask which Qt version (5.15, 6.5, 6.8, 6.9)
2. Call `qtmcp download-probe --qt-version <version>`
3. Report download location

## 4. Skills

### `qt-automation`

**File:** `skills/qt-automation/SKILL.md`

Triggers when the user:
- Asks about testing a Qt application
- Wants to inspect a Qt widget tree
- Asks to automate interactions with a Qt GUI
- Mentions exploring properties, signals, or slots of a Qt app
- Wants to take screenshots of a Qt application

The skill guides Claude to:
1. Check if a probe is connected
2. If not, discover and connect
3. Use the appropriate MCP tools for the task

## 5. Agents

### `qt-inspector`

**File:** `agents/qt-inspector.md`

A specialized subagent for deep Qt UI exploration:
- Walks the widget hierarchy
- Inspects properties and signals
- Navigates item models
- Takes targeted screenshots
- Reports findings in structured format

Tools: MCP tools from qtmcp server + Read, Write (for saving reports)

## 6. Installation

**From GitHub marketplace:**
```
/plugin marketplace add ssss2art/QtMcp
/plugin install qtmcp@ssss2art/QtMcp
```

**Local development:**
```bash
claude --plugin-dir ./plugin
```

**Prerequisites:**
- Python 3.11+ (for `uvx` to work)
- A Qt application to inspect
- Internet access on first use (probe and launcher binaries are auto-downloaded and cached)

## 7. Decisions

### Separate server per mode

Register three MCP servers in `.mcp.json`, one per mode. Users can disable modes they don't need. No code changes required — the existing `--mode` flag already supports `native`, `cu`, and `chrome`.

Tool inventory by server:
- `qtmcp-native`: `qt_*` (33 tools) — object tree, properties, signals, methods, models, QML + `qtmcp_*` (7 tools) — discovery, connection, recording
- `qtmcp-cu`: `cu_*` (13 tools) — screenshots, mouse, keyboard, scroll + `qtmcp_*` (4 tools) — discovery, connection
- `qtmcp-chrome`: `chr_*` (8 tools) — accessibility tree, page text, find elements + `qtmcp_*` (4 tools) — discovery, connection

### Plugin lives at `plugin/` in the repo

The repo root is already occupied by C++ source, Python package, tests, and docs. A `plugin/` subdirectory keeps plugin concerns separate. The marketplace entry points to this subdirectory:

```json
{
  "name": "qtmcp",
  "source": "./plugin"
}
```

### Download probe binaries on demand

Do not bundle pre-built binaries in the plugin. Reasons:
- Probe binaries are platform-specific (Windows DLL vs Linux SO) and Qt-version-specific (5.15, 6.5, 6.8, 6.9) — bundling all combinations bloats the plugin
- The download manager (`python/src/qtmcp/download.py`) already handles this with checksum verification
- The `/qtmcp:download-probe` command and `qtmcp download-probe` CLI provide the user-facing interface
- Binaries are cached locally after first download

### Use `uvx` as the default command

`uvx` provides zero-install experience — no `pip install` prerequisite. First-run cost (~5s to pull from PyPI) is acceptable since `uvx` caches the package afterward. Users who want faster cold starts can override to `pip install qtmcp` and change `.mcp.json` to use `"command": "qtmcp"` directly.

## 8. Auto-Install of C++ Binaries

The plugin auto-downloads pre-built probe and launcher binaries on first use. No user action required.

### Cache directory

All binaries are cached in a persistent, platform-appropriate location:

| Platform | Path |
|---|---|
| Windows | `%LOCALAPPDATA%\qtmcp\bin\` |
| Linux | `~/.local/share/qtmcp/bin/` |

Structure inside:
```
bin/
├── qtmcp-probe-qt6.8-windows-msvc.dll
├── qtmcp-probe-qt5.15-windows-msvc.dll
├── qtmcp-launch-windows-msvc.exe
└── ...
```

Binaries are downloaded once and reused across sessions. The cache persists through plugin updates.

### What gets downloaded

| Binary | When | How Qt version is determined |
|---|---|---|
| **Probe** (.dll/.so) | On first `qtmcp_connect_probe` call | Probe reports its Qt version during WebSocket handshake. If launching a target, the launcher detects Qt version from the target binary. |
| **Launcher** (.exe/binary) | On first `/qtmcp:launch` or `--target` usage | Not Qt-version-specific — one binary per platform. |

### Download flow

```
User says "inspect my Qt app"
  → Claude calls qtmcp_connect_probe(ws_url)
    → Server checks cache for probe binary
      → If missing: downloads from GitHub Releases to cache dir
      → If present: uses cached binary
    → Connects to probe via WebSocket
  → Claude proceeds with inspection tools
```

For `--target` (launch mode):
```
User says "launch myapp.exe and inspect it"
  → Claude calls tool or /qtmcp:launch
    → Server checks cache for launcher binary
      → If missing: downloads launcher from GitHub Releases
    → Server checks cache for probe matching target's Qt version
      → If missing: downloads probe
    → Launches target with probe injected
    → Connects to probe
```

### Code changes required in `python/src/qtmcp/`

1. **`download.py`** — Add:
   - `get_cache_dir() -> Path` — returns platform-appropriate cache directory
   - `ensure_probe(qt_version) -> Path` — checks cache, downloads if missing, returns path
   - `ensure_launcher() -> Path` — checks cache, downloads launcher if missing, returns path
   - `download_launcher(output_dir, release_tag)` — downloads launcher binary from GitHub Releases
   - Cache-aware defaults (download to cache dir instead of CWD)

2. **`server.py`** — Modify lifespan/connection logic:
   - On `connect_to_probe`: after connection, query probe's Qt version, ensure matching probe binary is cached
   - On `--target` launch: call `ensure_launcher()` and `ensure_probe()` before launching

3. **`connection.py`** — Add Qt version detection:
   - After WebSocket handshake, query `qt_version` tool to learn what Qt the target uses
   - Store the version on the `ProbeConnection` object for cache management

### What the plugin does NOT install

- **Qt SDK** — users must have Qt installed for their own applications
- **C++ compiler / CMake** — only needed by contributors building from source
- **Python** — required as a prerequisite (for `uvx` to work)
