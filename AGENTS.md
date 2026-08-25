<!-- promptlib:start -->
# AGENTS.md - Guidelines for AI Coding Agents

## Project Architecture & Dual Delivery Model

qtPilot is a Qt-based Model Context Protocol (MCP) server enabling AI assistants to introspect and control Qt applications.

- **Desktop (Windows, Linux, macOS):** Dynamic probe injection (`.dll`, `.so`, `.dylib`) via `qtPilot-launcher` (`DYLD_INSERT_LIBRARIES`, `LD_PRELOAD`, Detours). No source code modification needed.
- **Mobile (Android, iOS):** Static probe linking (`libqtPilot-probe.a`) via CMake `find_package(qtPilot)` and `qtPilot_inject_probe(myapp)` / `target_link_libraries(myapp PRIVATE qtPilot::Probe)`. Built with `qt-cmake`.
- **Security Rule:** The probe opens an unauthenticated WebSocket server (`:9222`). On mobile, it MUST only be linked behind a development-only build flag and NEVER in a distributed release build.
- **Whole-Archive Linking:** Static probe startup relies on `Q_COREAPP_STARTUP_FUNCTION` static initialization. When linking by raw path instead of the CMake imported target, whole-archive force-loading (`-force_load`, `/WHOLEARCHIVE:`, `--whole-archive`) or `qtPilot::ensureInitialized()` is required.

## Key Developer Workflows

- See [CLAUDE.md](CLAUDE.md) for build, test, and benchmark instructions.
- See [docs/MOBILE.md](docs/MOBILE.md) for mobile cross-compilation, linking, USB forwarding (`adb forward`, `iproxy`), and platform limits.
- See [docs/BUILDING.md](docs/BUILDING.md) for CMake presets and build flags (`QTPILOT_PROBE_STATIC`).
<!-- promptlib:end -->

