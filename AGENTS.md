<!-- promptlib:start -->
# AGENTS.md - Guidelines for AI Coding Agents

## Project Architecture & Dual Delivery Model

qtPilot is a Qt-based Model Context Protocol (MCP) server enabling AI assistants to introspect and control Qt applications.

- **Desktop (Windows, Linux, macOS):** Dynamic probe injection (`.dll`, `.so`, `.dylib`) via `qtPilot-launcher` (`DYLD_INSERT_LIBRARIES`, `LD_PRELOAD`, Detours). No source code modification needed.
- **Mobile (Android, iOS):** Static probe linking (`libqtPilot-probe.a`) via CMake `find_package(qtPilot)` and `qtPilot_inject_probe(myapp)` / `target_link_libraries(myapp PRIVATE qtPilot::Probe)`. Built with `qt-cmake`.
- **Security Rule:** The probe opens an unauthenticated WebSocket server (`:9222`). On mobile, it MUST only be linked behind a development-only build flag and NEVER in a distributed release build.
- **Whole-Archive Linking:** Static probe startup relies on `Q_COREAPP_STARTUP_FUNCTION` static initialization. When linking by raw path instead of the CMake imported target, whole-archive force-loading (`-force_load`, `/WHOLEARCHIVE:`, `--whole-archive`) or `qtPilot::ensureInitialized()` is required.

## Engineering & Validation Standards

- **Strict Red/Green TDD:** All assertions and features must be validated with proven, executable tests in a verifiable red/green cycle. Tests must fail before implementation and pass cleanly after. Never write untested assertions or unverified claims.
- **Python Typing & Best Practices (Python 3.11+ / 3.14):**
  - Always include `from __future__ import annotations` at the top of every module.
  - Full, explicit type annotations on all function signatures (parameters and return types). Avoid bare `Any` wherever concrete types can be expressed.
  - Use modern built-in generic syntax: `list[T]`, `dict[K, V]`, `set[T]`, `tuple[T, ...]`, and union syntax `T | None` / `T | U` (never `typing.Optional` / `typing.Union` / `typing.List`).
- **Domain Modeling with Data Classes:**
  - Prefer `@dataclass(frozen=True)` (or `slots=True` where appropriate) over unstructured dictionaries or loose tuples for domain objects, records, and configs.
- **Monadic Result Pipelines & Functional Composition:**
  - Use `Result[T, E]` (`Ok` / `Err`) for multi-step pipelines and fallible operations where failures should be modeled explicitly without untyped exception leakage.
  - Leverage `map`, `map_err`, `flat_map`, and `fold` for clean, composable error handling and deterministic transformations.
- **Fluent Test DSL & Matchers:**
  - Utilize expressive fluent matchers (`expect_replay(result).to_pass()`, `expect_replay(result).to_diverge_at(...)`) and custom domain matchers (e.g., Google Mock `QEXPECT_THAT` / `qt_matchers.h`) to keep test assertions clear, robust, and readable.
- **Zero Warnings Policy:** Zero compiler warnings across all C++ compilers and zero pytest warnings/errors (`filterwarnings = ["error"]`).
- **Strict Confidentiality Rule (MANDATORY):** Driven applications must NEVER be named or identified in commits, PR descriptions, test fixtures, or docs. Always describe strictly by shape (e.g. "large widgets + QGraphicsView desktop app").

## Key Developer Workflows

- See [CLAUDE.md](CLAUDE.md) for build, test, and benchmark instructions.
- See [docs/MOBILE.md](docs/MOBILE.md) for mobile cross-compilation, linking, USB forwarding (`adb forward`, `iproxy`), and platform limits.
- See [docs/BUILDING.md](docs/BUILDING.md) for CMake presets and build flags (`QTPILOT_PROBE_STATIC`).
<!-- promptlib:end -->
