# Feature Landscape: Distribution & Packaging

**Domain:** Distributing a C++/Qt injection library + Python MCP server
**Researched:** 2026-02-01
**Context:** qtPilot v1.0 is complete. This covers what users expect when consuming the library via vcpkg, GitHub Releases, and pip install.

## Executive Summary

qtPilot has a uniquely challenging distribution story: the C++ probe (DLL/SO) must match the target application's Qt version at the ABI level, the launcher executables are standalone, and the Python MCP server is pure Python with no native extensions. Each distribution channel serves a different user persona and has different table-stakes expectations. The Qt version matrix (5.15, 6.2, 6.5, 6.8, 6.9) combined with platforms (Windows MSVC, Linux GCC) creates a combinatorial explosion that must be managed deliberately.

---

## Channel 1: vcpkg (Source Port / Custom Registry)

### Target User
C++ developers building the probe from source, integrating into their own build system or CI.

### Table Stakes

| Feature | Why Expected | Complexity | Dependencies |
|---------|--------------|------------|--------------|
| **Valid vcpkg.json manifest** | Without it, vcpkg cannot resolve the package | Low | Already exists |
| **portfile.cmake that builds from source** | Core requirement for any vcpkg port | Medium | CMakeLists.txt must be vcpkg-compatible |
| **Proper find_package() support** | Users expect `find_package(qtPilot CONFIG)` + `target_link_libraries(... qtPilot::probe)` | Medium | CMake config already partially exists |
| **vcpkg-cmake and vcpkg-cmake-config dependencies** | Standard vcpkg port convention; required for portfile helpers | Low | None |
| **Usage file** | vcpkg displays this after install; tells user how to consume | Low | None |
| **Feature flags for optional deps** | `extras` (nlohmann-json, spdlog), `tests` (gtest) already defined | Low | Already exists in vcpkg.json |
| **Qt version detection** | Build must find and use the Qt version available in the vcpkg triplet or system | Low | CMakeLists.txt already handles Qt5/6 detection |
| **License file installed** | vcpkg requires `copyright` file in port; CI checks for it | Low | MIT LICENSE exists |
| **Debug/Release configurations** | vcpkg builds both by default; must not break | Low | Standard CMake |
| **vcpkg_cmake_config_fixup** | Ensures CMake config files land in correct locations | Low | portfile.cmake |

### Differentiators

| Feature | Value Proposition | Complexity | Dependencies |
|---------|-------------------|------------|--------------|
| **Custom vcpkg registry on GitHub** | Users add registry URL to vcpkg-configuration.json and get the port without copying files | Medium | GitHub repo + versions database |
| **Binary caching CI workflow** | Pre-built binaries cached via GitHub Packages/NuGet; speeds up consumer builds | Medium | GitHub Actions + NuGet setup |
| **Overlay port documentation** | Quick-start for users who just want to copy the port into their overlay | Low | Docs only |
| **Qt version feature flag** | e.g., `vcpkg install qtpilot[qt6]` to explicitly control which Qt major version | Medium | portfile logic to force Qt version |
| **Version constraints in manifest** | Proper `version-semver` and baseline pinning for reproducible builds | Low | versions database |

### Anti-Features

| Anti-Feature | Why Avoid | What to Do Instead |
|--------------|-----------|-------------------|
| **Submitting to vcpkg curated registry** | Niche tool; unlikely to be accepted; maintenance burden with vcpkg team review cycles | Use custom registry; document overlay port approach |
| **Bundling Qt in the port** | vcpkg already provides Qt; bundling creates version conflicts and massive bloat | Depend on `qtbase` and `qtwebsockets` as declared |
| **Building all Qt versions in one install** | vcpkg does not support multiple versions of the same port simultaneously | One build per triplet; document multi-instance approach for multiple Qt versions |
| **Pre-built binaries in the port** | vcpkg ports build from source; pre-built binaries belong in GitHub Releases | Keep port source-only |

---

## Channel 2: GitHub Releases (Prebuilt Binaries)

### Target User
Users who want to inject the probe into a Qt application without setting up a C++ build environment. Download, extract, run.

### Table Stakes

| Feature | Why Expected | Complexity | Dependencies |
|---------|--------------|------------|--------------|
| **Release archive per Qt version + platform** | Probe DLL/SO must match target's Qt ABI exactly; users need to pick the right one | Medium | CI matrix build |
| **Clear naming convention** | User must identify correct archive instantly: `qtpilot-v1.1.0-qt6.8-win-x64-msvc.zip` | Low | Release automation |
| **Probe DLL/SO included** | The primary artifact; `qtPilot_probe.dll` / `libqtPilot_probe.so` | Low | Build output |
| **Launcher included** | `qtpilot-launch.exe` (Windows) or launcher script (Linux) | Low | Build output |
| **README/QUICKSTART in archive** | User opens zip, sees how to use it immediately | Low | Template |
| **SHA256 checksums** | Security baseline; users verify downloads | Low | CI generates |
| **Changelog / release notes** | What changed since last version | Low | Manual or generated |
| **Both Debug and Release builds** | Debug builds essential for troubleshooting probe issues in target apps | Medium | Doubles CI matrix |
| **Qt DLLs NOT bundled** | Probe loads into target process which already has Qt; bundling causes conflicts | N/A | Architectural decision |

### Differentiators

| Feature | Value Proposition | Complexity | Dependencies |
|---------|-------------------|------------|--------------|
| **GitHub Actions CI matrix** | Automated builds for every Qt version x platform combination on tag push | High | Qt installation in CI, matrix config |
| **Version compatibility table** | Clear docs: "Qt 6.8.0-6.8.x: use qt6.8 build. Qt 6.9.0+: use qt6.9 build" | Low | Testing/docs |
| **Probe version embedding** | `qtPilot_probe.dll` reports its version and compatible Qt version at runtime | Low | Compile-time defines |
| **Single-file launcher with embedded config** | `qtpilot-launch.exe --qt-version 6.8 target.exe` auto-selects correct probe DLL | Medium | Launcher enhancement |
| **Signed binaries** | Code signing for Windows DLLs builds trust | Medium | Code signing certificate, CI integration |
| **GitHub Release via `gh release create`** | Automated release workflow triggered by git tag | Medium | GitHub Actions |
| **Install script** | `install.ps1` / `install.sh` that copies probe + launcher to a sensible location | Low | Script |

### Anti-Features

| Anti-Feature | Why Avoid | What to Do Instead |
|--------------|-----------|-------------------|
| **Bundling Qt runtime DLLs** | Probe injects into apps that already have Qt loaded; extra Qt DLLs cause symbol conflicts | Document that probe uses the target app's Qt |
| **Single "universal" binary** | C++ ABI compatibility makes this impossible across Qt versions | Build per Qt version; document clearly |
| **MSI/NSIS installer** | Overhead for a developer tool; zip extraction is simpler and more flexible | Zip archives + optional install script |
| **Auto-update mechanism** | Complexity explosion; probe is injected, not a standalone app | GitHub Releases + version check tool |
| **Bundling Python MCP server in C++ release** | Different distribution channels serve different users; Python users use pip | Separate release artifacts; cross-reference in docs |

---

## Channel 3: pip install (Python MCP Server)

### Target User
Python developers and AI agent operators who want to connect Claude (or other MCP clients) to a Qt application that already has the probe injected.

### Table Stakes

| Feature | Why Expected | Complexity | Dependencies |
|---------|--------------|------------|--------------|
| **`pip install qtpilot` works** | Basic expectation; pure Python wheel from PyPI | Medium | PyPI account, build pipeline |
| **CLI entry point** | `qtpilot` command available after install (already defined in pyproject.toml) | Low | Already exists |
| **Correct dependency declaration** | `fastmcp>=2.0,<3` and `websockets>=14.0` pinned properly | Low | Already exists |
| **Pure Python wheel** | No native extensions needed; the Python server is pure Python connecting via WebSocket | Low | hatchling build backend handles this |
| **Python version constraint** | `requires-python = ">=3.11"` clearly stated | Low | Already exists |
| **Works with `uvx`** | MCP servers are commonly run via `uvx qtpilot` for zero-install usage | Low | Proper package metadata |
| **sdist + wheel on PyPI** | Both source distribution and wheel published | Low | Standard practice |
| **LICENSE in package** | PyPI requires it; users expect it | Low | Add to hatch config |
| **Package description/README on PyPI** | PyPI page should explain what this is and how to use it | Low | Add `readme` to pyproject.toml |

### Differentiators

| Feature | Value Proposition | Complexity | Dependencies |
|---------|-------------------|------------|--------------|
| **`qtpilot[probe]` extra that bundles probe binaries** | One-stop install: `pip install qtpilot[probe]` downloads platform-specific probe | High | Platform-specific wheels, complex packaging |
| **Claude Desktop config snippet in docs** | Copy-paste JSON for `claude_desktop_config.json` | Low | Docs only |
| **`qtpilot --version` and `qtpilot --help`** | Standard CLI UX | Low | CLI implementation |
| **`qtpilot doctor` command** | Diagnoses connection issues: is probe running? WebSocket reachable? | Medium | CLI enhancement |
| **`qtpilot list-tools`** | Shows available tools without connecting to Claude | Low | CLI enhancement |
| **Typed tool definitions** | Full type hints and docstrings on all 53 tools for IDE support | Low | Already partially done |
| **Probe download helper** | `qtpilot download-probe --qt-version 6.8 --platform windows` fetches from GitHub Releases | Medium | GitHub Releases API integration |
| **MCP config auto-generation** | `qtpilot config` outputs the JSON config block for Claude Desktop | Low | CLI enhancement |
| **Published to PyPI with trusted publisher** | GitHub Actions OIDC-based publishing; no API tokens to manage | Medium | GitHub Actions + PyPI setup |

### Anti-Features

| Anti-Feature | Why Avoid | What to Do Instead |
|--------------|-----------|-------------------|
| **Bundling C++ probe in the wheel by default** | Massive wheel size; platform-specific; probe must match target's Qt anyway | Separate distribution; optional `[probe]` extra or download helper |
| **Pinning exact dependency versions** | Breaks in user environments; pip resolver conflicts | Use compatible ranges: `>=2.0,<3` |
| **Custom transport protocol** | WebSocket is standard; adding alternatives increases surface area | Stick with WebSocket; it works |
| **Auto-launching probe** | Injection requires privilege and target selection; too dangerous to automate silently | Provide clear CLI commands; user must explicitly inject |
| **GUI for the Python package** | Python package is a CLI/server tool; GUI adds massive dependency weight (tkinter/Qt) | CLI-only; let Claude Desktop / other MCP clients be the GUI |
| **Requiring conda** | Adds friction; pure Python package should work with pip/uv | Standard pip install |

---

## Cross-Channel Features

Features that span multiple distribution channels.

### Table Stakes

| Feature | Channels | Why Expected | Complexity |
|---------|----------|--------------|------------|
| **Semantic versioning** | All | Users need to reason about compatibility | Low |
| **CHANGELOG.md** | All | What changed between versions | Low |
| **Version parity** | All | C++ probe v1.1.0 and Python server v1.1.0 should be compatible | Low (process) |
| **Compatibility matrix docs** | All | Which probe version works with which Python server version, which Qt versions | Low (docs) |
| **License consistency** | All | MIT everywhere; no license ambiguity | Low |

### Differentiators

| Feature | Channels | Value Proposition | Complexity |
|---------|----------|-------------------|------------|
| **Monorepo release automation** | All | Single git tag triggers: vcpkg version bump + GitHub Release + PyPI publish | High |
| **Integration test in CI** | All | CI injects probe into test app, connects Python server, runs smoke test | High |
| **Getting Started guide** | All | End-to-end: install probe + install Python server + connect Claude | Medium (docs) |

---

## Qt Version Matrix: The Central Challenge

The probe must match the target application's Qt version at the ABI level. This creates a matrix:

| Qt Version | Windows x64 MSVC | Linux x64 GCC | Notes |
|------------|-------------------|---------------|-------|
| 5.15.x | Build | Build | Legacy; still widely deployed |
| 6.2.x | Build | Build | First Qt6 LTS |
| 6.5.x | Build | Build | Second Qt6 LTS |
| 6.8.x | Build | Build | Current LTS |
| 6.9.x | Build | Build | Latest |

**Total matrix: 5 Qt versions x 2 platforms = 10 build configurations** (20 if debug+release).

### Recommendations for Managing the Matrix

1. **GitHub Releases**: Build all 10 configurations. Name clearly. Users pick.
2. **vcpkg**: Single port; builds against whatever Qt the user's vcpkg instance provides. No matrix needed.
3. **pip**: No matrix needed for the Python server itself. Optional probe download helper handles the matrix.

---

## Feature Dependencies

```
Semantic Versioning (foundation)
    |
    +---> CHANGELOG.md
    |
    +---> Git Tag Automation
              |
              +---> GitHub Actions CI Matrix (C++ builds)
              |         |
              |         +---> GitHub Releases (prebuilt binaries)
              |         |         |
              |         |         +---> SHA256 checksums
              |         |         +---> Release notes
              |         |
              |         +---> vcpkg version database update
              |
              +---> PyPI Publishing (Python wheel)
                        |
                        +---> Trusted publisher (OIDC)
                        +---> CLI entry point
                        +---> Probe download helper (depends on GitHub Releases)

vcpkg Port (independent track)
    |
    +---> portfile.cmake
    +---> usage file
    +---> Custom registry setup
    +---> CI testing of port
```

---

## MVP Recommendation for Distribution

### Phase 1: Foundation (Must Ship)
1. Semantic versioning across all artifacts
2. CHANGELOG.md
3. GitHub Actions CI matrix for C++ builds (all Qt versions x platforms)
4. GitHub Releases with proper naming convention and SHA256 checksums
5. `pip install qtpilot` working from PyPI (pure Python wheel)
6. `qtpilot` CLI with `--version` and `--help`
7. Compatibility matrix documentation

### Phase 2: Developer Experience
8. vcpkg custom registry on GitHub
9. portfile.cmake + usage file
10. `qtpilot doctor` diagnostic command
11. Claude Desktop config snippet in README
12. Getting Started guide (end-to-end)

### Phase 3: Polish
13. Monorepo release automation (tag triggers all channels)
14. Trusted publisher on PyPI
15. `qtpilot download-probe` helper command
16. Integration test in CI
17. Signed Windows binaries (if certificate available)

### Defer
- `qtpilot[probe]` extra with bundled binaries (very high complexity, marginal value)
- Submission to vcpkg curated registry (niche tool)
- MSI/NSIS installer
- Auto-update mechanism

---

## Complexity Estimates

| Feature Category | Complexity | Effort Estimate | Risk Level |
|-----------------|------------|-----------------|------------|
| GitHub Actions CI matrix (5 Qt x 2 platforms) | High | 3-5 days | High (Qt installation in CI is tricky) |
| GitHub Releases automation | Medium | 1-2 days | Low |
| PyPI publishing pipeline | Medium | 1 day | Low (pure Python) |
| vcpkg portfile.cmake | Medium | 1-2 days | Medium (vcpkg conventions) |
| vcpkg custom registry | Medium | 1 day | Low (well-documented) |
| CLI enhancements (doctor, download-probe) | Medium | 2-3 days | Low |
| Monorepo release automation | High | 2-3 days | Medium |
| Integration testing in CI | High | 3-5 days | High (needs Qt app in CI) |
| Windows code signing | Medium | 1 day | Low (if certificate exists) |

---

## Confidence Assessment

| Area | Confidence | Rationale |
|------|------------|-----------|
| vcpkg table stakes | HIGH | Based on official Microsoft vcpkg documentation and maintainer guide |
| GitHub Releases table stakes | HIGH | Well-established patterns; multiple Qt projects use this approach |
| pip install table stakes | HIGH | Standard Python packaging; pyproject.toml already exists |
| Qt version matrix approach | HIGH | ABI compatibility is a hard constraint; no shortcuts |
| CI complexity for Qt matrix | MEDIUM | Qt installation in GitHub Actions runners varies; aqtinstall or setup-qt action needed |
| Probe bundling in pip (anti-feature) | HIGH | Clear consensus: platform-specific binaries do not belong in pure Python wheels |

---

## Sources

### PRIMARY (HIGH confidence)
- [vcpkg Packaging Tutorial](https://learn.microsoft.com/en-us/vcpkg/get_started/get-started-packaging) - Microsoft Learn
- [vcpkg Maintainer Guide](https://learn.microsoft.com/en-us/vcpkg/contributing/maintainer-guide) - Microsoft Learn
- [vcpkg Binary Caching with GitHub Packages](https://learn.microsoft.com/en-us/vcpkg/consume/binary-caching-github-packages) - Microsoft Learn
- [Testing Custom Registry Ports with GitHub Actions](https://learn.microsoft.com/en-us/vcpkg/produce/test-registry-ports-gha) - Microsoft Learn
- [Python Binary Distribution Format](https://packaging.python.org/specifications/binary-distribution-format/) - PyPA
- [Python Wheels](https://pythonwheels.com/) - Community resource
- [MCP Python SDK](https://github.com/modelcontextprotocol/python-sdk) - Official SDK

### SECONDARY (MEDIUM confidence)
- [C++ Packages in 2024](https://medium.com/philips-technology-blog/c-packages-in-2024-179ab0baf9ab) - Philips Technology Blog
- [Registries: Bring Your Own Libraries to vcpkg](https://devblogs.microsoft.com/cppblog/registries-bring-your-own-libraries-to-vcpkg/) - C++ Team Blog
- [What's New in vcpkg (June 2025)](https://devblogs.microsoft.com/cppblog/whats-new-in-vcpkg-june-2025/) - x-gha removal notice
- [swdevio/qt-builds](https://github.com/swdevio/qt-builds) - Qt prebuilt binary naming patterns
- [martinrotter/qt-minimalistic-builds](https://github.com/martinrotter/qt-minimalistic-builds) - Qt prebuilt binary patterns
- [Native dependencies in Python wheels discussion](https://discuss.python.org/t/native-dependencies-in-other-wheels-how-i-do-it-but-maybe-we-can-standardize-something/23913) - Python.org

### TERTIARY (LOW confidence)
- [Bundling DLLs with Windows wheels](https://vinayak.io/2020/10/21/day-51-bundling-dlls-with-windows-wheels-the-package-data-way/) - Blog post (2020, dated but technique still valid)
- [Wheel Variant Support proposal](https://wheelnext.dev/proposals/pepxxx_wheel_variant_support/) - Draft PEP, not yet accepted
