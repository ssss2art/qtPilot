# CLAUDE.md - AI Assistant Guidelines for QtMcp

## Project Overview

**QtMcp** is a Qt-based MCP (Model Context Protocol) server that mimics the Claude Code Chrome API. This project provides a native Qt implementation for integrating with Claude's tooling ecosystem.

- **License**: MIT
- **Language**: C++ with Qt Framework
- **Status**: Active development (v0.3.0)

## Build System

### Expected Build Tools
- **CMake** (3.16+) or **qmake** for Qt projects
- **Qt 6.x** framework (Qt 5.15+ may also work)
- C++17 or later standard

### Build Commands
```bash
# CMake configure + build (set QTMCP_QT_DIR to your local Qt installation)
cmake -B build -DQTMCP_QT_DIR=/path/to/Qt/5.15.x/msvc2019_64
cmake --build build --config Release
```

### Running Tests (Windows)

The Qt directory used during the build is stored in `build/CMakeCache.txt` as `QTMCP_QT_DIR`.
Tests need the Qt `bin/` on PATH and `plugins/` as QT_PLUGIN_PATH, otherwise they fail
with exit code `0xc0000135` (DLL not found).

```bash
# From bash/git-bash — extract Qt dir from CMake cache, then run via cmd //c
# (bash requires //c double-slash which it converts to /c for cmd.exe)
QT_DIR=$(grep "QTMCP_QT_DIR:PATH=" build/CMakeCache.txt | cut -d= -f2)
cmd //c "set PATH=${QT_DIR}\bin;%PATH% && set QT_PLUGIN_PATH=${QT_DIR}\plugins && ctest --test-dir build -C Release --output-on-failure"
```

```bat
:: From native cmd.exe — extract Qt dir from cache and run
for /f "tokens=2 delims==" %Q in ('findstr "QTMCP_QT_DIR:PATH=" build\CMakeCache.txt') do set QT_DIR=%Q
set PATH=%QT_DIR%\bin;%PATH%
set QT_PLUGIN_PATH=%QT_DIR%\plugins
ctest --test-dir build -C Release --output-on-failure
```

### Running Tests (Linux)
```bash
QT_DIR=$(grep "QTMCP_QT_DIR:PATH=" build/CMakeCache.txt | cut -d= -f2)
QT_PLUGIN_PATH="${QT_DIR}/plugins" LD_LIBRARY_PATH="${QT_DIR}/lib" ctest --test-dir build -C Release --output-on-failure
```

**Windows test caveat:** On Windows, env vars set in bash/git-bash do NOT propagate
through ctest to child processes. You **must** use one of these approaches:
1. **From bash/git-bash:** Use `cmd //c` (double-slash — bash converts `//c` to `/c` for cmd.exe).
2. **From native cmd.exe:** Set PATH and QT_PLUGIN_PATH before running ctest.

Env vars set inline (e.g., `PATH=... ctest ...`) do **not** work because ctest spawns
child processes that inherit the Windows environment, not the bash-local overrides.

## Development Conventions

### C++ Code Style

1. **Naming Conventions**
   - Classes: `PascalCase` (e.g., `McpServer`, `RequestHandler`)
   - Functions/Methods: `camelCase` (e.g., `handleRequest`, `parseMessage`)
   - Member variables: `m_` prefix with camelCase (e.g., `m_serverPort`)
   - Constants: `UPPER_SNAKE_CASE` (e.g., `DEFAULT_PORT`)
   - Namespaces: lowercase (e.g., `qtmcp`, `qtmcp::server`)

2. **Qt-Specific Conventions**
   - Use Qt's signal/slot mechanism for async communication
   - Prefer `QString` over `std::string` for Qt integration
   - Use `QObject` parent-child ownership model for memory management
   - Follow Qt's property system conventions with `Q_PROPERTY`

3. **File Organization**
   - One class per header/source file pair
   - Header files use `.h` extension
   - Source files use `.cpp` extension
   - Include guards: `#pragma once` (preferred) or `#ifndef QTMCP_CLASSNAME_H`

4. **Modern C++ Practices**
   - Use smart pointers (`std::unique_ptr`, `std::shared_ptr`) except for Qt-managed objects
   - Prefer `auto` for complex types
   - Use range-based for loops
   - Use `nullptr` instead of `NULL`
   - Mark override methods with `override` keyword

### MCP Protocol Implementation

The MCP (Model Context Protocol) defines how AI assistants communicate with external tools. Key components:

1. **Transport Layer**: Handle stdio/HTTP communication
2. **Message Types**: Request, Response, Notification
3. **JSON-RPC 2.0**: Message format specification

### Error Handling

- Use Qt's `QDebug` for logging during development
- Implement proper error propagation via return values or exceptions
- Include meaningful error messages for MCP protocol errors

## Testing Guidelines

### Test Framework
- Use Qt Test framework (`QTest`) for unit tests
- Tests should be in the `tests/` directory
- Each test file should test a single class or module

### Running Tests
See "Running Tests (Windows)" and "Running Tests (Linux)" in the Build System section above.

```bash
# Run a single test directly (Qt DLLs are copied to the same directory during build):
build/bin/Release/test_jsonrpc.exe
```

### Test Labels
- `admin` -- Tests requiring administrator privileges. Run with `ctest -L admin`. Auto-skipped when not elevated.

## Git Workflow

### Branch Naming
- Feature branches: `feature/description`
- Bug fixes: `fix/description`
- Claude AI branches: `claude/session-id`

### Commit Messages
- Use imperative mood: "Add feature" not "Added feature"
- Keep first line under 72 characters
- Reference issues when applicable

### Pull Request Process
1. Create feature branch from main
2. Implement changes with tests
3. Ensure all tests pass
4. Submit PR with clear description

## Key Implementation Notes

### MCP Server Core Responsibilities
1. Parse incoming JSON-RPC messages
2. Route requests to appropriate handlers
3. Manage tool registrations
4. Handle Chrome API compatibility layer
5. Serialize and send responses

### Chrome API Compatibility
The server should provide compatibility with Claude Code's Chrome extension API, including:
- WebSocket or HTTP transport options
- Proper CORS handling for browser integration
- Message format translation as needed

## Dependencies

### Required
- Qt 6.x (Core, Network modules minimum)
- C++17 compatible compiler (GCC 8+, Clang 7+, MSVC 2019+)

### Optional
- Qt WebSockets module (for WebSocket transport)
- nlohmann/json or Qt's QJsonDocument for JSON parsing
- spdlog or Qt logging for production logging

## Common Tasks for AI Assistants

### When Adding New Features
1. Create header in `include/` or `src/` as appropriate
2. Implement in corresponding `.cpp` file
3. Add unit tests in `tests/`
4. Update CMakeLists.txt if new files added
5. Document public APIs

### When Fixing Bugs
1. Write a failing test that reproduces the bug
2. Fix the issue
3. Verify the test passes
4. Check for similar issues elsewhere

### When Reviewing Code
- Verify Qt object ownership is correct
- Check for memory leaks in non-Qt-managed objects
- Ensure MCP protocol compliance
- Validate error handling completeness

## Quick Reference

| Task | Command/Location |
|------|------------------|
| Build | `cmake --build build --config Release` |
| Test (bash) | Extract `QT_DIR` from cache, then `cmd //c "set PATH=...&& ctest ..."` (see Build System section) |
| Test (cmd.exe) | Set `PATH` and `QT_PLUGIN_PATH` from `QTMCP_QT_DIR` in cache, then `ctest --test-dir build -C Release` |
| Launch | `build/bin/Release/qtmcp-launcher.exe [--qt-dir <path>] app.exe` |
| Source | `src/` directory |
| Tests | `tests/` directory |

## Important Files

- `/home/user/QtMcp/README.md` - Project description
- `/home/user/QtMcp/LICENSE` - MIT License terms
- `/home/user/QtMcp/.gitignore` - Build artifacts exclusion

---

*This document should be updated as the project evolves and new conventions are established.*
