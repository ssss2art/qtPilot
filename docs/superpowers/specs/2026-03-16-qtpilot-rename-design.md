# Project Rename: qtpilot to qtPilot

**Date:** 2026-03-16
**Status:** Approved
**Scope:** Full project rename — binaries, C++ namespace, Python package, MCP tools, CMake, env vars, docs, GitHub repo

## Naming Convention

| Context | Name |
|---------|------|
| C++ namespace | `qtPilot` |
| C++ qualified names | `qtPilot::` |
| Binary names | `qtPilot-launcher`, `qtPilot-probe`, `qtPilot-test-app` |
| Python package (distribution + import) | `qtpilot` (lowercase per PEP convention) |
| Python console script | `qtpilot` |
| MCP tool names | `qtpilot_` (lowercase snake_case) |
| MCP server name | `qtPilot` |
| Log prefix | `[qtPilot]` |
| Display/docs | `qtPilot` |
| CMake project name | `qtPilot` |
| CMake variables | `QTPILOT_` |
| CMake targets | `qtPilot_probe`, `qtPilot_launcher`, etc. |
| CMake exported target | `qtPilot::Probe` |
| Env variables | `QTPILOT_` |
| DLL export macro | `QTPILOT_EXPORT` |
| Wire protocol (JSON-RPC methods) | `qtPilot.*` (breaking change) |
| Discovery protocol | `qtPilot-discovery` (breaking change) |
| GitHub repo | `ssss2art/qtPilot` |

## Breaking Changes

This is a clean break. Old probes and new servers (or vice versa) are **not compatible**.
- Discovery protocol string changes from `qtPilot-discovery` to `qtPilot-discovery`
- JSON-RPC method names change from `qtpilot.*` to `qtPilot.*`
- CMake `find_package(qtPilot)` becomes `find_package(qtPilot)`
- Probe and server versions must match after this rename

## Approach

Big-bang scripted rename: all replacements in a single pass, verified by build and tests, committed as one atomic change.

## File/Directory Renames

| From | To |
|------|----|
| `python/src/qtpilot/` | `python/src/qtpilot/` |
| `cmake/qtPilot_inject_probe.cmake` | `cmake/qtPilot_inject_probe.cmake` |
| `cmake/qtPilotConfig.cmake.in` | `cmake/qtPilotConfig.cmake.in` |
| `qtPilot-specification.md` | `qtPilot-specification.md` |
| `qtPilot-compatibility-modes.md` | `qtPilot-compatibility-modes.md` |
| `qt515-tools/qtPilot-launcher.exe` | `qt515-tools/qtPilot-launcher.exe` |
| `qt515-tools/qtPilot-probe.dll` | `qt515-tools/qtPilot-probe.dll` |

## Text Replacement Order

Replacements are applied case-sensitively, longest-match-first to prevent partial substitutions.

**Excluded from replacement:** `.git/`, `build/`, `node_modules/`, `*.exe`, `*.dll`, `*.whl`, `*.tar.gz`, `package-lock.json`, `python/dist/`

**Note on `.planning/`:** Historical planning docs ARE included in the rename for grep-clean verification. They are not externally published, so updating them is safe.

| # | From | To | Notes |
|---|------|----|-------|
| 1 | `qtPilot-specification` | `qtPilot-specification` | Filename references |
| 2 | `qtPilot-compatibility` | `qtPilot-compatibility` | Filename references |
| 3 | `qtPilot_inject_probe` | `qtPilot_inject_probe` | CMake module/function |
| 4 | `qtPilot-test-app` | `qtPilot-test-app` | Binary name |
| 5 | `qtPilot-launcher` | `qtPilot-launcher` | Binary name |
| 6 | `qtPilot_test_app` | `qtPilot_test_app` | CMake target |
| 7 | `qtPilot_launcher` | `qtPilot_launcher` | CMake target |
| 8 | `qtPilot_add_test` | `qtPilot_add_test` | CMake test function |
| 9 | `qtPilot-probe` | `qtPilot-probe` | Probe binary/DLL name (also catches `libqtPilot-probe`) |
| 10 | `qtPilot_probe` | `qtPilot_probe` | CMake target |
| 11 | `qtPilot_shared` | `qtPilot_shared` | CMake target |
| 12 | `qtPilot-names` | `qtPilot-names` | Default name map file |
| 13 | `qtPilot-log-` | `qtPilot-log-` | Log file prefix |
| 14 | `qtPilot-discovery` | `qtPilot-discovery` | Wire protocol identifier |
| 15 | `QTPILOT_` | `QTPILOT_` | CMake vars, env vars, C++ macros |
| 16 | `qtPilot` | `qtPilot` | CMake project name, display name, `qtPilot::Probe` target |
| 17 | `qtPilot` | `qtPilot` | PascalCase variant in docs/URLs |
| 18 | `qtpilot_log_` | `qtpilot_log_` | Middleware prefix check |
| 19 | `qtpilot_` | `qtpilot_` | MCP tool names, CMake internal vars (lowercase) |
| 20 | `qtpilot.` | `qtpilot.` | Python imports (`from qtpilot.server`), JSON-RPC methods |
| 21 | `qtpilot` | `qtpilot` | Python package name, `_pkg_version()`, remaining lowercase refs |
| 22 | `ssss2art/qtPilot` | `ssss2art/qtPilot` | GitHub repo URLs (already handled by #17 but explicit) |

**Post-replacement fixups** (applied after the table above):

These correct cases where the table produces the wrong casing:

| From (after initial rename) | To | Reason |
|----|----|----|
| `namespace qtpilot` | `namespace qtPilot` | C++ namespace needs camelCase |
| `qtpilot::` | `qtPilot::` | C++ qualified names |
| `// namespace qtpilot` | `// namespace qtPilot` | Closing namespace comments |
| `using namespace qtpilot` | `using namespace qtPilot` | Using declarations |
| `[qtPilot]` (in log strings) | `[qtPilot]` | Log prefix display name |
| `"qtpilot"` (in .mcp.json `"name"` field) | `"qtPilot"` | MCP server display name |
| `qtPilot-probe` (in binary output names) | `qtPilot-probe` | Binary naming |
| `qtPilot-launcher` (in binary output names) | `qtPilot-launcher` | Binary naming |
| `qtPilot-test-app` (in binary output names) | `qtPilot-test-app` | Binary naming |

## Execution Steps

1. **Rename files/directories** — `git mv` for tracked files
2. **Run text replacements** — Apply the ordered replacement table, then fixups
3. **CMake reconfigure** — Delete `build/` and reconfigure from scratch
4. **Build** — `cmake --build build --config Release`
5. **Run C++ tests** — Verify all 15 tests pass
6. **Verify Python** — Check `from qtpilot.server import ...` works
7. **Commit** — Single atomic commit
8. **Rename GitHub repo** — `gh repo rename qtPilot`
9. **Update git remote** — Point to new repo URL
10. **Push** — Push to renamed remote

## Verification

- All 15 C++ tests pass
- `cmake --build` succeeds with no warnings about old names
- `grep -ri "qtpilot" --include="*.cpp" --include="*.h" --include="*.py" --include="*.cmake" --include="*.toml" --include="*.json" --include="*.md" .` returns zero hits (excluding `.git/`, `build/`, `node_modules/`, `python/dist/`)
- Python `from qtpilot.server import create_mcp_server` works
- `.mcp.json` references `qtPilot`
- `CLAUDE.md` references correct binary names
- Binaries are named `qtPilot-launcher.exe`, `qtPilot-probe-*.dll`, `qtPilot-test-app.exe`
- `.github/workflows/` reference correct artifact names
- `pyproject.toml` has `name = "qtpilot"`, script `qtpilot = "qtpilot.cli:main"`, version-file `src/qtpilot/_version.py`

## Out of Scope

- PyPI package republication (can be done separately)
- Updating external references to the old repo (GitHub redirects handle this)
- Backward compatibility with pre-rename probes/servers (clean break)
