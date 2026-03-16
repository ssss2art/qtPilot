# Project Milestones: qtPilot

## v1.1 Distribution & Compatibility (Shipped: 2026-02-03)

**Delivered:** Complete distribution infrastructure — anyone can install qtPilot from pip, vcpkg, or GitHub Releases with CI-tested binaries for Qt 5.15 through 6.9.

**Phases completed:** 8-13 + 11.1 (13 plans total)

**Key accomplishments:**

- Multi-Qt build system with versioned artifact naming (`qtPilot-probe-qt6.8.dll`) and relocatable cmake config
- 8-cell CI matrix testing Qt 5.15, 6.5, 6.8, 6.9 on Windows (MSVC) and Linux (GCC)
- Patched Qt 5.15.1 CI with custom GCC 11+ patches and aggressive caching
- Automated release workflow: tag push → 16 assets with SHA256 checksums → GitHub Release
- Qt5/Qt6 source compatibility: single source tree compiles cleanly on Qt 5.15-6.9
- vcpkg ports: source (build against your Qt) and binary (download prebuilt probe)
- PyPI publication with OIDC Trusted Publishers (`pip install qtpilot`)

**Stats:**

- 139 files changed (+18,196 / -8,841 lines)
- 7 phases (6 planned + 1 inserted), 13 plans, ~32 commits
- 3 days from milestone start to ship (2026-02-01 to 2026-02-03)

**Git range:** `6a0f178` (v1.0 complete) → `ef2058e` (PyPI Publication complete)

**What's next:** v1.2 — macOS support with DYLD_INSERT_LIBRARIES injection

---

## v1.0 MVP (Shipped: 2026-02-01)

**Delivered:** Complete Qt application control library with C++ probe injection, three API modes (Native, Computer Use, Chrome), and Python MCP server for Claude integration.

**Phases completed:** 1-7 (33 plans total)

**Key accomplishments:**

- DLL/LD_PRELOAD injection with WebSocket transport and JSON-RPC 2.0 protocol
- Full Qt object introspection: discovery, properties, methods, signals, UI interaction
- Three API modes: Native (33 qt.* methods), Computer Use (13 cu.* methods), Chrome (8 chr.* methods)
- QML item inspection and Model/View data navigation
- Python MCP server with 53 tool definitions for Claude integration
- 12 automated test suites, 34 UAT tests, all passing

**Stats:**

- 85 source files (71 C++ + 14 Python)
- 17,292 lines of code (15,912 C++ + 1,380 Python)
- 7 phases, 33 plans, 157 commits
- 7 days from initial commit to ship (2026-01-25 to 2026-02-01)

**Git range:** `9284bf5` (Initial commit) → `6a0f178` (test(07): complete UAT)

**What's next:** v1.1 — macOS support, attach to running process, or advanced introspection features

---
