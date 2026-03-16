# Architecture Patterns

**Domain:** Qt introspection/injection tools - Distribution & Packaging
**Researched:** 2026-02-01 (v1.1 distribution milestone)
**Supersedes:** 2026-01-29 v1.0 runtime architecture (preserved below)

---

## Part A: Distribution & Packaging Architecture (v1.1)

### Overview

The v1.1 distribution architecture adds multi-Qt-version builds, CI/CD, and multiple packaging channels to the existing v1.0 codebase. The core constraint is: **the probe DLL/SO is ABI-bound to a specific Qt version.** A probe built against Qt 5.15.2 cannot be loaded into a Qt 6.8 application. This means every distribution artifact must encode the Qt version it targets.

```
                    DISTRIBUTION ARCHITECTURE

  Source Code (single)
       |
       v
  CMake Build System (Qt-version-aware)
       |
       +--- Qt 5.15.2 build -------> qtPilot-probe-qt5.15.2-{os}-{arch}.{dll/so}
       +--- Qt 5.15.1-patched -----> qtPilot-probe-qt5.15.1p-{os}-{arch}.{dll/so}
       +--- Qt 6.2.x build --------> qtPilot-probe-qt6.2-{os}-{arch}.{dll/so}
       +--- Qt 6.8.x build --------> qtPilot-probe-qt6.8-{os}-{arch}.{dll/so}
       +--- Qt 6.9.x build --------> qtPilot-probe-qt6.9-{os}-{arch}.{dll/so}
       |
       v
  Distribution Channels:
       +--- GitHub Releases (prebuilt binaries, 10 artifacts)
       +--- vcpkg port (source build against user's Qt)
       +--- PyPI (Python MCP server + optional probe download)
```

### Component Inventory: New vs Modified

| Component | Status | File(s) | Purpose |
|-----------|--------|---------|---------|
| Root CMakeLists.txt | **MODIFY** | `CMakeLists.txt` | Add Qt version encoding in output names |
| CMakePresets.json | **MODIFY** | `CMakePresets.json` | Add per-Qt-version presets |
| qtPilotConfig.cmake.in | **MODIFY** | `cmake/qtPilotConfig.cmake.in` | Fix Qt5-only hardcode, support both Qt versions |
| CI workflow | **MODIFY** | `.github/workflows/ci.yml` | Matrix build, artifact upload |
| Release workflow | **NEW** | `.github/workflows/release.yml` | Tag-triggered release with all artifacts |
| Custom Qt build workflow | **NEW** | `.github/workflows/build-qt.yml` | Build patched Qt 5.15.1 from source, cache it |
| vcpkg overlay port | **NEW** | `ports/qtpilot/portfile.cmake`, `ports/qtpilot/vcpkg.json` | Source-build port |
| Python pyproject.toml | **MODIFY** | `python/pyproject.toml` | Add metadata for PyPI, optional extras |
| Python probe downloader | **NEW** | `python/src/qtpilot/probe.py` | Download prebuilt probe from GitHub Releases |
| Probe CMakeLists.txt | **MODIFY** | `src/probe/CMakeLists.txt` | Encode Qt version in output name |

---

### 1. CMake Multi-Qt Architecture

#### Current State

The root `CMakeLists.txt` already has a working dual Qt 5/6 discovery pattern (lines 43-88). It tries Qt6 first, falls back to Qt5. The probe CMakeLists.txt branches on `QT_VERSION_MAJOR` for link targets. Output name is `qtPilot-probe` regardless of Qt version.

#### Problem

A single build produces `qtPilot-probe.dll` whether built against Qt 5.15 or Qt 6.8. Users cannot distinguish them. Installing both to the same prefix would overwrite.

#### Recommended Change: Qt-Version-Encoded Output Names

Encode the Qt major.minor version in the library output name. This is the approach used by KDE Frameworks (KF5 vs KF6) and GammaRay.

```cmake
# In src/probe/CMakeLists.txt, replace the OUTPUT_NAME line:

# Encode Qt version in library name for side-by-side installation
set(QTPILOT_QT_SUFFIX "qt${QT_VERSION_MAJOR}.${Qt${QT_VERSION_MAJOR}_VERSION_MINOR}")
# Produces: qt5.15, qt6.2, qt6.8, qt6.9

set_target_properties(qtPilot_probe PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    OUTPUT_NAME "qtPilot-probe-${QTPILOT_QT_SUFFIX}"
    EXPORT_NAME probe
)
```

This produces:
- `qtPilot-probe-qt5.15.dll` / `libqtPilot-probe-qt5.15.so`
- `qtPilot-probe-qt6.8.dll` / `libqtPilot-probe-qt6.8.so`

Similarly for the launcher:
```cmake
set_target_properties(qtPilot_launcher PROPERTIES
    OUTPUT_NAME "qtpilot-launch-${QTPILOT_QT_SUFFIX}"
)
```

#### CMake Preset Strategy

Add per-Qt-version presets that explicitly set `CMAKE_PREFIX_PATH`. The current presets use a single `vcpkg-base` with no Qt path -- Qt is found via environment variable at configure time. This works but is implicit. For CI clarity, add explicit presets:

```jsonc
// New presets to add to CMakePresets.json
{
    "name": "ci-linux-qt515",
    "displayName": "CI Linux Qt 5.15",
    "inherits": "vcpkg-base",
    "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Release",
        "CMAKE_PREFIX_PATH": "$env{Qt5_DIR}",
        "QTPILOT_BUILD_TESTS": "ON"
    }
},
{
    "name": "ci-linux-qt68",
    "displayName": "CI Linux Qt 6.8",
    "inherits": "vcpkg-base",
    "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Release",
        "CMAKE_PREFIX_PATH": "$env{Qt6_DIR}",
        "QTPILOT_BUILD_TESTS": "ON"
    }
}
```

Each preset produces a separate build directory (`build/ci-linux-qt515/`, `build/ci-linux-qt68/`), so artifacts do not collide.

#### qtPilotConfig.cmake.in Fix (Critical Bug)

The current `cmake/qtPilotConfig.cmake.in` hardcodes Qt5:

```cmake
find_dependency(Qt5 5.15 COMPONENTS Core Widgets WebSockets)  # BUG: hardcoded Qt5
find_dependency(nlohmann_json)  # BUG: not optional at install time
find_dependency(spdlog)         # BUG: not optional at install time
```

This must be fixed to be Qt-version-aware and handle optional deps:

```cmake
@PACKAGE_INIT@

include(CMakeFindDependencyMacro)

# Qt version detected at build time
set(QTPILOT_QT_VERSION_MAJOR @QT_VERSION_MAJOR@)

if(QTPILOT_QT_VERSION_MAJOR EQUAL 6)
    find_dependency(Qt6 @QT_MIN_VERSION_6@ COMPONENTS Core Widgets WebSockets)
else()
    find_dependency(Qt5 @QT_MIN_VERSION_5@ COMPONENTS Core Widgets WebSockets)
endif()

if(@QTPILOT_HAS_NLOHMANN_JSON@)
    find_dependency(nlohmann_json)
endif()

if(@QTPILOT_HAS_SPDLOG@)
    find_dependency(spdlog)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/qtPilotTargets.cmake")
check_required_components(qtPilot)
```

**Confidence: HIGH** -- This is standard CMake `configure_package_config_file` practice documented in the [CMake packaging guide](https://cmake.org/cmake/help/latest/guide/importing-exporting/index.html).

---

### 2. GitHub Actions CI/CD Architecture

#### Build Matrix Design

The CI needs to build: 5 Qt versions x 2 platforms = 10 probe artifacts. Additionally, the patched Qt 5.15.1 requires a custom-built Qt (not available from `install-qt-action`).

```yaml
# Matrix structure for the standard builds
strategy:
  fail-fast: false
  matrix:
    include:
      # Qt 5.15.2 - available from install-qt-action
      - qt-version: '5.15.2'
        qt-major: 5
        os: ubuntu-22.04
        artifact-suffix: qt5.15.2-linux-x64
      - qt-version: '5.15.2'
        qt-major: 5
        os: windows-2022
        artifact-suffix: qt5.15.2-windows-x64

      # Qt 6.2.x LTS
      - qt-version: '6.2.4'
        qt-major: 6
        os: ubuntu-22.04
        artifact-suffix: qt6.2-linux-x64
      - qt-version: '6.2.4'
        qt-major: 6
        os: windows-2022
        artifact-suffix: qt6.2-windows-x64

      # Qt 6.8.x LTS
      - qt-version: '6.8.3'
        qt-major: 6
        os: ubuntu-22.04
        artifact-suffix: qt6.8-linux-x64
      - qt-version: '6.8.3'
        qt-major: 6
        os: windows-2022
        artifact-suffix: qt6.8-windows-x64

      # Qt 6.9.x latest
      - qt-version: '6.9.1'
        qt-major: 6
        os: ubuntu-22.04
        artifact-suffix: qt6.9-linux-x64
      - qt-version: '6.9.1'
        qt-major: 6
        os: windows-2022
        artifact-suffix: qt6.9-windows-x64
```

**Note on `include` vs cross-product:** Using explicit `include` entries instead of `os: [ubuntu, windows]` x `qt-version: [5.15, 6.2, ...]` because:
1. Some Qt versions may need different `modules` values (e.g., Qt 5 uses `qtwebsockets`, Qt 6 may need `qt5compat`)
2. The patched Qt 5.15.1 job is completely different (no `install-qt-action`)
3. Explicit is more maintainable than implicit for 10 cells

#### Workflow Structure: Three Workflows

**Workflow 1: `ci.yml` (on push/PR)**
- Lint job (existing, unchanged)
- Matrix build job (8 standard Qt versions x platforms)
- Patched Qt 5.15.1 build job (separate, depends on cached Qt)
- Python test job (existing, unchanged)
- Artifact upload per matrix cell

**Workflow 2: `release.yml` (on tag push `v*`)**
- Triggers on `v*` tags
- Runs the full matrix build (same as CI but Release mode)
- Collects all artifacts into a GitHub Release
- Publishes Python package to PyPI

**Workflow 3: `build-qt.yml` (manual/scheduled)**
- Builds patched Qt 5.15.1 from source
- Caches the result as a GitHub Actions cache or artifact
- Only runs when cache is stale or manually triggered

#### Caching Strategy

| What | Cache Key | Expected Size | TTL |
|------|-----------|---------------|-----|
| vcpkg packages | `vcpkg-{os}-{hash(vcpkg.json)}` | ~50-100MB | Until vcpkg.json changes |
| Qt (install-qt-action) | Built-in, keyed by version+modules | ~500MB-1GB per version | Managed by action |
| Patched Qt 5.15.1 build | `qt-5.15.1-patched-{os}-{hash(patches)}` | ~1-2GB | Until patches change |
| CMake build (ccache) | `ccache-{os}-{qt-version}-{hash(src)}` | ~100-200MB | Rolling, prefix match |

**Important:** GitHub Actions cache is limited to 10GB per repository. With 2 platforms x 4 Qt versions from `install-qt-action` at ~500MB each = 4GB. Adding patched Qt at ~1.5GB x 2 platforms = 3GB. This is tight. Recommendation: Use `install-qt-action`'s built-in caching (which is separate from the GitHub Actions cache) and only use explicit caching for the patched Qt and vcpkg.

#### Patched Qt 5.15.1: CI Strategy

This is the hardest part. `install-qt-action` cannot install a custom-patched Qt build. Options:

**Option A: Build Qt from source in CI (RECOMMENDED)**

```yaml
build-patched-qt:
  runs-on: ${{ matrix.os }}
  strategy:
    matrix:
      os: [ubuntu-22.04, windows-2022]
  steps:
    - uses: actions/checkout@v4

    - name: Cache patched Qt
      id: qt-cache
      uses: actions/cache@v4
      with:
        path: ~/qt-5.15.1-patched
        key: qt-5.15.1-patched-${{ matrix.os }}-${{ hashFiles('patches/**') }}

    - name: Build Qt from source
      if: steps.qt-cache.outputs.cache-hit != 'true'
      run: |
        git clone --branch v5.15.1 --depth 1 https://code.qt.io/qt/qt5.git
        cd qt5
        perl init-repository --module-subset=qtbase,qtwebsockets
        # Apply patches
        cd qtbase
        git apply $GITHUB_WORKSPACE/patches/*.patch
        cd ..
        # Configure and build
        ./configure -prefix ~/qt-5.15.1-patched -opensource -confirm-license \
          -nomake tests -nomake examples -release
        make -j$(nproc)
        make install
      # Windows equivalent uses configure.bat and nmake/jom

    - name: Upload Qt build
      uses: actions/upload-artifact@v4
      with:
        name: qt-5.15.1-patched-${{ matrix.os }}
        path: ~/qt-5.15.1-patched
```

**Build time:** ~30-60 minutes for qtbase+qtwebsockets on a GitHub Actions runner. The cache makes subsequent runs instant.

**Option B: Pre-built Qt as release artifact (alternative)**

Build the patched Qt once locally, upload as a release artifact to a separate repo or as a GitHub Release asset on the qtPilot repo itself. CI downloads it instead of building.

Pros: Faster CI, no Qt build in pipeline.
Cons: Manual process, harder to keep in sync with patches.

**Recommendation:** Start with Option A (build from source with aggressive caching). If CI time becomes a problem, switch to Option B later.

**Confidence: MEDIUM** -- The Vector35/qt-build project validates that building Qt from source in CI is practical, but specific build times for GitHub Actions runners with Qt 5.15.1 subset builds are not verified.

---

### 3. vcpkg Port Architecture

#### Port Type: Source Build (Overlay Port)

vcpkg's philosophy is build-from-source. The official curated registry does not accept ports that download prebuilt binaries. qtPilot should provide an overlay port that builds the probe from source against whatever Qt the user has installed.

#### Directory Structure

```
ports/
  qtpilot/
    portfile.cmake      # Build instructions
    vcpkg.json          # Port manifest
    usage               # Post-install usage instructions
```

**Note:** This goes in the qtPilot repo root under `ports/`, not in the vcpkg tree. Users add it as `--overlay-ports=./ports`.

#### portfile.cmake

```cmake
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO ssss2art/qtPilot
    REF "v${VERSION}"
    SHA512 0  # Updated on each release
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DQTPILOT_BUILD_TESTS=OFF
        -DQTPILOT_BUILD_TEST_APP=OFF
)

vcpkg_cmake_install()

vcpkg_cmake_config_fixup(
    PACKAGE_NAME qtPilot
    CONFIG_PATH lib/cmake/qtPilot
)

file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
)

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
```

#### vcpkg.json (Port Manifest)

```json
{
  "name": "qtpilot",
  "version": "1.1.0",
  "description": "Qt application introspection and automation library with MCP integration",
  "homepage": "https://github.com/ssss2art/qtPilot",
  "license": "MIT",
  "supports": "windows | linux",
  "dependencies": [
    "vcpkg-cmake",
    "vcpkg-cmake-config"
  ],
  "features": {
    "extras": {
      "description": "Additional dependencies (nlohmann-json, spdlog)",
      "dependencies": ["nlohmann-json", "spdlog"]
    }
  }
}
```

**Key design decision:** The port does NOT declare `qtbase` or `qtwebsockets` as vcpkg dependencies. Why? Because:
1. Users likely already have Qt installed system-wide or via Qt installer (not vcpkg)
2. vcpkg's Qt ports are notoriously slow to build (~1-2 hours)
3. The probe must match the EXACT Qt version the target app uses

Instead, the port relies on `CMAKE_PREFIX_PATH` to find Qt at configure time. The `usage` file explains this:

```
qtpilot provides CMake targets:
    find_package(qtPilot CONFIG REQUIRED)
    target_link_libraries(main PRIVATE qtPilot::probe)

Note: qtPilot requires Qt (5.15+ or 6.x) with Core, Widgets, Network, and
WebSockets modules. Set CMAKE_PREFIX_PATH to your Qt installation.
```

**Confidence: HIGH** -- This follows the standard vcpkg overlay port pattern documented in [Microsoft's vcpkg overlay ports guide](https://learn.microsoft.com/en-us/vcpkg/concepts/overlay-ports).

#### Binary Download Variant (Separate Overlay)

For users who want prebuilt binaries without building from source, provide a second overlay port:

```
ports/
  qtpilot/          # Source build (primary)
  qtpilot-prebuilt/ # Binary download (convenience)
```

The `qtpilot-prebuilt` portfile downloads from GitHub Releases instead of building:

```cmake
# Determine Qt version and platform
if(VCPKG_TARGET_IS_WINDOWS)
    set(PLATFORM "windows")
    set(EXT "zip")
else()
    set(PLATFORM "linux")
    set(EXT "tar.gz")
endif()

# User must set QTPILOT_QT_VERSION (e.g., "qt5.15", "qt6.8")
if(NOT DEFINED QTPILOT_QT_VERSION)
    message(FATAL_ERROR "Set QTPILOT_QT_VERSION to match your Qt (e.g., qt5.15, qt6.8)")
endif()

vcpkg_download_distfile(ARCHIVE
    URLS "https://github.com/ssss2art/qtPilot/releases/download/v${VERSION}/qtpilot-${VERSION}-${QTPILOT_QT_VERSION}-${PLATFORM}-x64.${EXT}"
    FILENAME "qtpilot-${VERSION}-${QTPILOT_QT_VERSION}-${PLATFORM}-x64.${EXT}"
    SHA512 0
)

vcpkg_extract_source_archive(SOURCE_PATH ARCHIVE "${ARCHIVE}")

# Install pre-built files
file(INSTALL "${SOURCE_PATH}/lib/" DESTINATION "${CURRENT_PACKAGES_DIR}/lib")
file(INSTALL "${SOURCE_PATH}/include/" DESTINATION "${CURRENT_PACKAGES_DIR}/include")
# ... etc
```

**Confidence: MEDIUM** -- This pattern is used by some overlay ports (e.g., CUDA) but is not standard vcpkg practice. It requires careful SHA512 management per release per Qt version.

---

### 4. Python Package Architecture (PyPI)

#### Current State

The Python package at `python/` uses hatchling with a simple `pyproject.toml`:
- Name: `qtpilot`
- Entry point: `qtpilot = "qtpilot.cli:main"`
- Dependencies: `fastmcp>=2.0,<3`, `websockets>=14.0`

#### Recommended PyPI Distribution Strategy

The Python package is **pure Python** (no native code). It connects to the C++ probe over WebSocket. The probe binary is NOT included in the wheel. Instead:

1. `pip install qtpilot` installs the pure-Python MCP server
2. Users obtain the probe DLL/SO separately (GitHub Releases, vcpkg, or manual build)
3. Optionally, `qtpilot` provides a CLI command to download the probe: `qtpilot probe download --qt-version 5.15`

This avoids the hatchling platform-specific wheel issue (hatchling produces `py3-none-any` wheels even with binary artifacts, as documented in [pypa/hatch#1955](https://github.com/pypa/hatch/issues/1955)).

#### Enhanced pyproject.toml

```toml
[project]
name = "qtpilot"
version = "1.1.0"
description = "MCP server for controlling Qt applications via qtPilot probe"
requires-python = ">=3.10"
license = {text = "MIT"}
authors = [
    {name = "qtPilot Contributors"}
]
readme = "README.md"
keywords = ["qt", "mcp", "automation", "introspection", "claude"]
classifiers = [
    "Development Status :: 4 - Beta",
    "Intended Audience :: Developers",
    "License :: OSI Approved :: MIT License",
    "Programming Language :: Python :: 3",
    "Programming Language :: Python :: 3.10",
    "Programming Language :: Python :: 3.11",
    "Programming Language :: Python :: 3.12",
    "Topic :: Software Development :: Testing",
    "Topic :: Software Development :: Libraries",
]
dependencies = [
    "fastmcp>=2.0,<3",
    "websockets>=14.0",
    "httpx>=0.25",  # For probe download
]

[project.optional-dependencies]
dev = [
    "pytest>=7.0",
    "pytest-asyncio>=0.21",
]

[project.scripts]
qtpilot = "qtpilot.cli:main"

[project.urls]
Homepage = "https://github.com/ssss2art/qtPilot"
Documentation = "https://github.com/ssss2art/qtPilot/blob/main/python/README.md"
Issues = "https://github.com/ssss2art/qtPilot/issues"
Changelog = "https://github.com/ssss2art/qtPilot/releases"

[build-system]
requires = ["hatchling"]
build-backend = "hatchling.build"

[tool.hatch.build.targets.sdist]
include = ["src/qtpilot"]

[tool.hatch.build.targets.wheel]
packages = ["src/qtpilot"]
```

#### Probe Download Helper

New file: `python/src/qtpilot/probe.py`

```python
"""Download prebuilt probe binaries from GitHub Releases."""

import platform
import httpx
from pathlib import Path

GITHUB_RELEASE_URL = "https://github.com/ssss2art/qtPilot/releases/download"

def get_probe_filename(qt_version: str) -> str:
    """Get the expected probe filename for this platform."""
    os_name = "windows" if platform.system() == "Windows" else "linux"
    ext = "dll" if os_name == "windows" else "so"
    return f"qtPilot-probe-{qt_version}.{ext}"

def download_probe(qt_version: str, version: str = "latest", dest: Path | None = None) -> Path:
    """Download probe binary from GitHub Releases."""
    # Implementation: resolve 'latest', download, verify, place in dest
    ...
```

This is integrated into the CLI:

```
qtpilot probe download --qt-version qt5.15
qtpilot probe download --qt-version qt6.8 --dest /usr/local/lib
qtpilot probe list  # Show available versions from GitHub Releases API
```

**Confidence: HIGH** for the pure-Python wheel approach. MEDIUM for the probe download helper (needs GitHub API integration).

---

### 5. Release Artifact Organization

#### Naming Convention

```
qtpilot-{version}-{qt-suffix}-{os}-{arch}.{ext}
```

| Component | Values | Example |
|-----------|--------|---------|
| version | semver | `1.1.0` |
| qt-suffix | `qt5.15`, `qt5.15.1p`, `qt6.2`, `qt6.8`, `qt6.9` | `qt5.15` |
| os | `linux`, `windows` | `linux` |
| arch | `x64` | `x64` |
| ext | `tar.gz` (Linux), `zip` (Windows) | `tar.gz` |

The `p` suffix on `qt5.15.1p` denotes the patched build, distinguishing it from stock Qt 5.15.1.

#### Full Release Artifact List (v1.1.0 example)

```
qtpilot-1.1.0-qt5.15-linux-x64.tar.gz
qtpilot-1.1.0-qt5.15-linux-x64.tar.gz.sha256
qtpilot-1.1.0-qt5.15-windows-x64.zip
qtpilot-1.1.0-qt5.15-windows-x64.zip.sha256
qtpilot-1.1.0-qt5.15.1p-linux-x64.tar.gz
qtpilot-1.1.0-qt5.15.1p-linux-x64.tar.gz.sha256
qtpilot-1.1.0-qt5.15.1p-windows-x64.zip
qtpilot-1.1.0-qt5.15.1p-windows-x64.zip.sha256
qtpilot-1.1.0-qt6.2-linux-x64.tar.gz
qtpilot-1.1.0-qt6.2-linux-x64.tar.gz.sha256
qtpilot-1.1.0-qt6.2-windows-x64.zip
qtpilot-1.1.0-qt6.2-windows-x64.zip.sha256
qtpilot-1.1.0-qt6.8-linux-x64.tar.gz
qtpilot-1.1.0-qt6.8-linux-x64.tar.gz.sha256
qtpilot-1.1.0-qt6.8-windows-x64.zip
qtpilot-1.1.0-qt6.8-windows-x64.zip.sha256
qtpilot-1.1.0-qt6.9-linux-x64.tar.gz
qtpilot-1.1.0-qt6.9-linux-x64.tar.gz.sha256
qtpilot-1.1.0-qt6.9-windows-x64.zip
qtpilot-1.1.0-qt6.9-windows-x64.zip.sha256
```

That is 20 files (10 archives + 10 checksums).

#### Archive Internal Structure

Each archive contains:

```
qtpilot-1.1.0-qt5.15-linux-x64/
  lib/
    libqtPilot-probe-qt5.15.so
    libqtPilot-probe-qt5.15.so.1
    libqtPilot-probe-qt5.15.so.1.1.0
  bin/
    qtpilot-launch-qt5.15
  include/
    qtpilot/
      core/probe.h
      core/injector.h
      ...
  lib/cmake/qtPilot/
    qtPilotConfig.cmake
    qtPilotConfigVersion.cmake
    qtPilotTargets.cmake
    qtPilotTargets-release.cmake
  LICENSE
  README.md
```

Windows variant uses `.dll`, `.lib`, `.exe` extensions.

#### GitHub Actions Artifact to Release Flow

```
Matrix Build Jobs (parallel)
  |
  +-- Job 1: qt5.15 + linux --> upload-artifact: "qtpilot-qt5.15-linux-x64"
  +-- Job 2: qt5.15 + windows --> upload-artifact: "qtpilot-qt5.15-windows-x64"
  +-- Job 3: qt5.15.1p + linux --> upload-artifact: "qtpilot-qt5.15.1p-linux-x64"
  ...
  +-- Job 10: qt6.9 + windows --> upload-artifact: "qtpilot-qt6.9-windows-x64"
  |
  v
Release Job (depends on all build jobs)
  |
  +-- download-artifact (all 10)
  +-- package each into archive with naming convention
  +-- generate checksums
  +-- softprops/action-gh-release@v2 with all 20 files
  +-- Publish Python to PyPI via trusted publishing
```

**Confidence: HIGH** -- Standard GitHub Actions release pattern, documented in [actions/upload-artifact](https://github.com/actions/upload-artifact) and [softprops/action-gh-release](https://github.com/softprops/action-gh-release).

---

### 6. Integration Points with Existing Architecture

#### What Changes in Existing Files

| File | Change | Risk |
|------|--------|------|
| `CMakeLists.txt` | Add `QTPILOT_QT_SUFFIX` variable, pass to subdirectories | LOW - additive |
| `src/probe/CMakeLists.txt` | Change `OUTPUT_NAME` to include Qt suffix | LOW - rename only |
| `src/launcher/CMakeLists.txt` | Change `OUTPUT_NAME` to include Qt suffix | LOW - rename only |
| `cmake/qtPilotConfig.cmake.in` | Fix Qt5 hardcode, handle optional deps | MEDIUM - consumers affected |
| `CMakePresets.json` | Add per-Qt-version presets | LOW - additive |
| `.github/workflows/ci.yml` | Matrix expansion, artifact naming | MEDIUM - full rewrite of build jobs |
| `python/pyproject.toml` | Add PyPI metadata, bump version | LOW - additive |
| `vcpkg.json` | No change needed (root manifest is for dev, not distribution) | NONE |

#### What Does NOT Change

- All source code in `src/probe/` and `src/launcher/` (zero C++ changes needed)
- Python source code in `python/src/qtpilot/` (existing code unchanged)
- Test code in `tests/`
- The runtime architecture (probe injection, WebSocket, JSON-RPC)

---

### 7. Suggested Build Order for Phases

```
Phase 1: CMake Multi-Qt Foundation
  1.1 Add QTPILOT_QT_SUFFIX to root CMakeLists.txt
  1.2 Update OUTPUT_NAME in probe and launcher CMakeLists.txt
  1.3 Fix qtPilotConfig.cmake.in (Qt version + optional deps)
  1.4 Add per-Qt-version presets to CMakePresets.json
  1.5 Verify: build locally against Qt 5.15 and Qt 6.x, confirm different output names
  Depends on: nothing (pure CMake changes)

Phase 2: CI/CD Matrix Build
  2.1 Restructure ci.yml with matrix strategy
  2.2 Add install-qt-action for each standard Qt version
  2.3 Add artifact upload with version-encoded names
  2.4 Verify: all 8 standard matrix cells build green
  Depends on: Phase 1 (output names must be correct before CI builds)

Phase 3: Patched Qt 5.15.1 CI
  3.1 Create patches/ directory with Qt source patches
  3.2 Create build-qt.yml workflow for building patched Qt
  3.3 Add patched Qt job to ci.yml (download cached build)
  3.4 Verify: patched Qt builds produce qtPilot-probe-qt5.15.1p
  Depends on: Phase 2 (CI infrastructure must exist)

Phase 4: GitHub Releases
  4.1 Create release.yml triggered on v* tags
  4.2 Implement archive packaging (tar.gz/zip with correct structure)
  4.3 Implement checksum generation
  4.4 Create release with all 20 artifacts
  4.5 Verify: tag push produces complete release
  Depends on: Phase 2 + 3 (all matrix builds must work)

Phase 5: vcpkg Port
  5.1 Create ports/qtpilot/vcpkg.json
  5.2 Create ports/qtpilot/portfile.cmake
  5.3 Create ports/qtpilot/usage
  5.4 Test: vcpkg install qtpilot --overlay-ports=./ports
  5.5 (Optional) Create ports/qtpilot-prebuilt/ for binary download variant
  Depends on: Phase 4 (source build port needs tagged releases for vcpkg_from_github)

Phase 6: PyPI Publication
  6.1 Enhance python/pyproject.toml with PyPI metadata
  6.2 Add probe download helper (python/src/qtpilot/probe.py)
  6.3 Add CLI commands for probe management
  6.4 Configure trusted publishing in release.yml
  6.5 Verify: pip install qtpilot works, qtpilot probe download works
  Depends on: Phase 4 (probe download needs GitHub Releases to exist)
```

**Ordering rationale:**
- CMake changes first because everything else depends on correct output naming
- CI before releases because releases depend on CI builds
- Patched Qt is the riskiest (building Qt from source in CI) so it gets its own phase
- vcpkg and PyPI are parallel-capable but both need releases to exist first

---

## Part B: Runtime Architecture (v1.0 -- Preserved)

### Recommended Architecture

qtPilot follows the established architecture patterns used by GammaRay, Qt-Inspector, and Qat for Qt introspection tools. The architecture separates into four primary components with clear boundaries:

```
                                    +---------------------------+
                                    |     Claude / LLM          |
                                    |    (MCP Client)           |
                                    +-----------+---------------+
                                                | MCP Protocol
                                                | (stdio/HTTP)
+-----------------------------------------------+------------------------------+
|  HOST MACHINE                                 |                              |
|                                               v                              |
|  +-----------------------------------------------------------------------+   |
|  |  PYTHON MCP SERVER                                                    |   |
|  |  +----------------+    +----------------+    +----------------------+ |   |
|  |  | MCP Protocol   |    | Tool           |    | WebSocket Client     | |   |
|  |  | Handler        |--->| Dispatcher     |--->| (JSON-RPC)           | |   |
|  |  +----------------+    +----------------+    +----------+-----------+ |   |
|  +---------------------------------------------------------|-------------+   |
|                                                            | WebSocket       |
|                                                            | JSON-RPC 2.0   |
|  +---------------------------------------------------------v-------------+   |
|  |  TARGET Qt APPLICATION PROCESS                                        |   |
|  |  +------------------------------------------------------------------+ |   |
|  |  |  C++ PROBE (libqtpilot.so / qtpilot.dll)                             | |   |
|  |  |                                                                   | |   |
|  |  |  +--------------------------------------------------------------+ | |   |
|  |  |  |  TRANSPORT LAYER                                             | | |   |
|  |  |  |  +-------------------+    +--------------------------------+ | | |   |
|  |  |  |  | WebSocket Server  |    | JSON-RPC Handler               | | | |   |
|  |  |  |  | (QWebSocketSvr)   |--->| (Request/Response routing)     | | | |   |
|  |  |  |  +-------------------+    +--------------------------------+ | | |   |
|  |  |  +--------------------------------------------------------------+ | |   |
|  |  |                                  |                                | |   |
|  |  |  +-------------------------------v------------------------------+ | |   |
|  |  |  |  INTROSPECTION LAYER                                        | | |   |
|  |  |  |  +----------------+  +----------------+  +------------------+| | |   |
|  |  |  |  | Object         |  | Meta           |  | Widget/A11y      || | |   |
|  |  |  |  | Registry       |  | Inspector      |  | Tree Builder     || | |   |
|  |  |  |  +----------------+  +----------------+  +------------------+| | |   |
|  |  |  +--------------------------------------------------------------+ | |   |
|  |  |                                  |                                | |   |
|  |  |  +-------------------------------v------------------------------+ | |   |
|  |  |  |  HOOKS LAYER (Qt Internal APIs)                              | | |   |
|  |  |  |  +------------------------+  +------------------------------+| | |   |
|  |  |  |  | qtHookData Callbacks   |  | Signal Spy Callbacks         || | |   |
|  |  |  |  | (AddQObject/Remove)    |  | (qt_register_signal_spy_cb)  || | |   |
|  |  |  |  +------------------------+  +------------------------------+| | |   |
|  |  |  +--------------------------------------------------------------+ | |   |
|  |  |                                  |                                | |   |
|  |  |  +-------------------------------v------------------------------+ | |   |
|  |  |  |  Qt Application Objects (QWidgets, QML Items, etc.)          | | |   |
|  |  |  +--------------------------------------------------------------+ | |   |
|  |  +------------------------------------------------------------------+ |   |
|  +-----------------------------------------------------------------------+   |
+------------------------------------------------------------------------------+
```

### Component Boundaries

| Component | Responsibility | Communicates With | Process |
|-----------|---------------|-------------------|---------|
| **Python MCP Server** | MCP protocol handling, tool exposure to Claude | Claude (stdio/HTTP), C++ Probe (WebSocket) | Separate process |
| **WebSocket Client (Python)** | Async communication with probe, request/response correlation | JSON-RPC Handler in probe | Part of MCP Server |
| **WebSocket Server (C++)** | Network transport, connection management | MCP Server, JSON-RPC Handler | In-target process |
| **JSON-RPC Handler (C++)** | Request parsing, method dispatch, response formatting | WebSocket Server, Mode Handlers | In-target process |
| **Mode Handlers (C++)** | API-specific logic (Native/ComputerUse/Chrome) | JSON-RPC Handler, Introspection Layer | In-target process |
| **Object Registry (C++)** | QObject lifecycle tracking, ID assignment | Hooks Layer, Meta Inspector | In-target process |
| **Meta Inspector (C++)** | QMetaObject introspection, property/method access | Object Registry, Qt Objects | In-target process |
| **Widget Tree Builder (C++)** | Hierarchy traversal, accessibility tree generation | Object Registry, Qt Widgets | In-target process |
| **Hooks Layer (C++)** | Qt internal API integration (qtHookData, signal spy) | Qt Core internals | In-target process |

### Data Flow

**Request Flow (Inbound):**
```
1. Claude sends MCP tool call
2. Python MCP Server receives via stdio/HTTP
3. Tool Dispatcher selects appropriate handler
4. WebSocket Client formats JSON-RPC request
5. WebSocket Server receives in Qt event loop
6. JSON-RPC Handler parses and dispatches
7. Mode Handler processes request
8. Introspection Layer queries Qt objects
9. Response travels back up the chain
```

**Event Flow (Outbound - Push Notifications):**
```
1. Qt signal fires or object created/destroyed
2. Hooks Layer callback triggers
3. Object Registry updates internal state
4. JSON-RPC Handler formats notification
5. WebSocket Server pushes to all clients
6. Python MCP Server receives notification
7. (Optionally forwarded to Claude via MCP)
```

---

## Sources

### HIGH Confidence
- [Qt 5 and Qt 6 CMake Compatibility](https://doc.qt.io/qt-6/cmake-qt5-and-qt6-compatibility.html) -- Official Qt docs on dual-version CMake support
- [vcpkg Overlay Ports](https://learn.microsoft.com/en-us/vcpkg/concepts/overlay-ports) -- Official Microsoft docs
- [actions/upload-artifact v4](https://github.com/actions/upload-artifact) -- Matrix artifact naming requirements
- [jurplel/install-qt-action](https://github.com/jurplel/install-qt-action) -- Qt installation in GitHub Actions
- [CMake Package Config Files](https://cmake.org/cmake/help/latest/guide/importing-exporting/index.html) -- CMake packaging guide

### MEDIUM Confidence
- [KDAB: GitHub Actions for Qt](https://www.kdab.com/github-actions-for-cpp-and-qt/) -- Matrix strategy patterns for Qt
- [Vector35/qt-build](https://github.com/Vector35/qt-build) -- Custom Qt builds from source with patches
- [Multi-platform release workflow](https://www.lucavall.in/blog/how-to-create-a-release-with-multiple-artifacts-from-a-github-actions-workflow-using-the-matrix-strategy) -- Artifact-to-release pattern
- [hatchling platform wheel issue](https://github.com/pypa/hatch/issues/1955) -- Confirms pure-Python-only approach is correct

### LOW Confidence
- Patched Qt 5.15.1 build times in GitHub Actions CI (~30-60 min estimate, not measured)
- GitHub Actions 10GB cache limit interaction with multi-Qt caching (may need tuning)
