# qtPilot

## What This Is

qtPilot is a lightweight, MIT-licensed injection library that enables AI assistants (Claude), test automation frameworks, and debugging tools to inspect and control Qt applications at runtime. It consists of a C++ probe (15,912 LOC) injected into Qt apps via LD_PRELOAD/DLL injection, a WebSocket transport layer with JSON-RPC 2.0, three API modes (Native, Computer Use, Chrome), and a Python MCP server (1,380 LOC) for Claude integration.

## Core Value

Claude can control any Qt application with zero learning curve — the probe exposes familiar APIs (Computer Use, Chrome-style) that Claude already knows.

## Requirements

### Validated

- C++ probe loads via LD_PRELOAD (Linux) and DLL injection (Windows) — v1.0
- WebSocket server accepts connections on configurable port — v1.0
- Object discovery: find by objectName, className, get object tree — v1.0
- Object inspection: properties, methods, signals, geometry — v1.0
- UI interaction: click, sendKeys, screenshot — v1.0
- Signal monitoring: subscribe/unsubscribe, push events — v1.0
- Native API: hierarchical object IDs, full Qt introspection (33 qt.* methods) — v1.0
- Computer Use API: screenshot, coordinates, mouse/keyboard actions (13 cu.* methods) — v1.0
- Chrome API: accessibility tree, refs, form_input, semantic click (8 chr.* methods) — v1.0
- Windows launcher (qtpilot-launch.exe) — v1.0
- Python MCP server with 53 tool definitions — v1.0
- Python client library for automation scripts — v1.0
- QML item introspection and Model/View navigation — v1.0
- Multi-Qt version build system (5.15, 5.15.1-patched, 6.5, 6.8, 6.9) — v1.1
- GitHub Actions CI/CD matrix build (4 Qt versions × 2 platforms) — v1.1
- GitHub Releases with prebuilt probe binaries (16 assets) — v1.1
- vcpkg port — source build (user builds against their Qt) — v1.1
- vcpkg port — binary download (prebuilt from GitHub Releases) — v1.1
- Python MCP server published to PyPI (`pip install qtpilot`) — v1.1

### Active

#### v1.2 macOS Support
- [ ] DYLD_INSERT_LIBRARIES injection on macOS
- [ ] macOS launcher
- [ ] CI builds for macOS (Clang/AppleClang)
- [ ] macOS probe binaries in GitHub Releases

### Out of Scope

- macOS support — deferred, different injection approach required
- Attach to running process — launch-only injection for MVP
- Authentication — rely on localhost binding for security
- Recording/playback — future feature
- Qt 4 support — never
- Mobile (Android/iOS) — future
- Built-in GUI — never (headless tool)
- Plugin system — never

## Context

Shipped v1.1 with distribution infrastructure complete.
Tech stack: C++17, Qt 5.15-6.9, Qt WebSockets, JSON-RPC 2.0, Python 3.8+, FastMCP 2.14.
CI matrix: 8 cells (Qt 5.15, 6.5, 6.8, 6.9 × Windows/Linux) + 2 patched Qt cells.
Distribution: GitHub Releases (16 artifacts), vcpkg overlay ports, PyPI (`pip install qtpilot`).
Known tech debt: SHA512 placeholders in vcpkg ports (update after first release tag).

## Constraints

- **Platform**: Windows and Linux only — macOS requires different injection approach
- **Qt Version**: Qt 5.15.1+, 6.5+ — probe builds against 5.15, 5.15.1-patched, 6.5, 6.8, 6.9
- **Injection**: Launch-only — no attach to running process
- **Security**: Localhost binding default — no authentication
- **Dependencies**: Qt Core, Qt Network, Qt WebSockets modules required

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| LD_PRELOAD (Linux) / DLL injection (Windows) | Standard approach, proven by GammaRay | Good |
| WebSocket transport | Allows multiple clients, browser-compatible | Good |
| JSON-RPC 2.0 | Standard protocol, good tooling support | Good |
| Three API modes | Claude already knows Computer Use and Chrome APIs | Good |
| Python MCP server (not C++) | Faster iteration, MCP SDK available in Python | Good |
| Hierarchical object IDs | Human-readable, reflects actual Qt object tree | Good |
| InitOnce API instead of std::call_once | Avoids TLS issues in injected DLLs on Windows | Good |
| Q_COREAPP_STARTUP_FUNCTION | Auto-init when Qt starts, no manual call needed | Good |
| Ephemeral refs for Chrome Mode | Fresh refs per readPage, avoids stale refs | Good |
| Qt Quick as optional dependency | QTPILOT_HAS_QML compile guard, graceful degradation | Good |
| Minimal DllMain pattern | Only DisableThreadLibraryCalls + flag; defer all Qt work | Good |
| Versioned artifact naming | `qtPilot-probe-qt{M}.{m}.dll` encodes Qt version in filename | Good |
| Manual IMPORTED target | Replaced CMake EXPORT with manual target for versioned paths | Good |
| Qt minimum versions 5.15.1/6.5 | CMake FATAL_ERROR enforces; QT_DISABLE_DEPRECATED_BEFORE | Good |
| OIDC Trusted Publishers | Secure keyless PyPI publishing without API tokens | Good |
| Per-artifact SHA512 in vcpkg | Binary port validates individual downloads | Good |

---
*Last updated: 2026-02-03 after v1.1 milestone*
