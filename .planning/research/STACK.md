# Technology Stack: Distribution, CI/CD & Packaging

**Project:** qtPilot - Distribution Milestone
**Researched:** 2026-02-01
**Overall Confidence:** HIGH (verified against current GitHub Action repos, official docs, PyPI)

---

## Executive Summary

This document covers the stack additions needed to distribute qtPilot as a multi-Qt-version library with CI/CD, vcpkg packaging, PyPI publishing, and GitHub Releases automation. The existing build system (CMake 3.16+, vcpkg manifest, CMakePresets.json) provides a solid foundation but needs several upgrades and additions.

Key constraints driving decisions:
1. **Qt ABI incompatibility across minors** -- probe DLL/SO must be compiled per Qt version (5.15, 6.2, 6.8, 6.9)
2. **Qt 5.15 open-source availability is degrading** -- only 5.15.2 reliably available via aqtinstall; newer patches require commercial
3. **Python package is Qt-version-independent** -- separate publish pipeline
4. **Existing CI uses outdated action versions** -- install-qt-action@v3, run-vcpkg@v11 with stale commit ID

---

## 1. GitHub Actions CI/CD

### Core Actions

| Action | Recommended Version | Purpose | Confidence |
|--------|---------------------|---------|------------|
| **jurplel/install-qt-action** | **@v4** | Install Qt versions for CI builds | HIGH |
| **lukka/run-vcpkg** | **@v11** (v11.5) | Setup vcpkg with binary caching | HIGH |
| **actions/checkout** | **@v4** | Repository checkout | HIGH |
| **actions/upload-artifact** | **@v4** | Upload build artifacts | HIGH |
| **actions/download-artifact** | **@v4** | Download artifacts in release job | HIGH |
| **actions/setup-python** | **@v5** | Python environment for PyPI publish | HIGH |
| **softprops/action-gh-release** | **@v2** | Create GitHub releases with assets | HIGH |
| **pypa/gh-action-pypi-publish** | **@release/v1** | Trusted publisher PyPI upload | HIGH |
| **jidicula/clang-format-action** | **@v4.11.0** | Code formatting check (existing) | HIGH |
| **github/codeql-action** | **@v3** | Security scanning (existing) | HIGH |

### install-qt-action@v4 Details

**Upgrade from v3 is required.** v4 uses aqtinstall 3.2.x (default), which fixed issues with Qt 6.7+ installation. The current CI uses v3 which is outdated.

**Qt version availability via aqtinstall (open-source):**

| Qt Version | Available | Modules Needed | Notes |
|------------|-----------|----------------|-------|
| 5.15.2 | YES | `qtwebsockets` | Last open-source 5.15 patch reliably available |
| 5.15.x-patched | NO | N/A | Requires commercial license (`use-official: true`) |
| 6.2.x | YES | `qtwebsockets` | LTS, should work with aqtinstall |
| 6.8.x | YES | `qtwebsockets` | Current LTS, default version in v4 is 6.8.3 |
| 6.9.x | YES | `qtwebsockets` | Latest feature release |

**Critical: CorePrivate headers.** The probe links `Qt::CorePrivate` for `qhooks_p.h`. The install-qt-action installs the full SDK including private headers, so this should work. However, this is a risk area -- if aqtinstall ever strips private headers, builds will fail. No `modules` parameter is needed for CorePrivate; it ships with qtbase.

**Runner OS requirements:**

| Qt Version | Linux Runner | Windows Runner |
|------------|--------------|----------------|
| 5.15.2 | `ubuntu-22.04` | `windows-2022` |
| 6.2.x | `ubuntu-22.04` | `windows-2022` |
| 6.8.x | `ubuntu-24.04` (or `ubuntu-latest`) | `windows-2022` |
| 6.9.x | `ubuntu-24.04` (or `ubuntu-latest`) | `windows-2022` |

Note: `ubuntu-latest` now points to Ubuntu 24.04 as of January 2025. Qt 5.15 may have issues on Ubuntu 24.04 due to older OpenSSL/glibc expectations -- use `ubuntu-22.04` explicitly for Qt 5.15 builds.

**Matrix strategy pattern:**

```yaml
strategy:
  fail-fast: false
  matrix:
    include:
      - qt-version: '5.15.2'
        os: ubuntu-22.04
        preset: ci-linux
        artifact-suffix: linux-qt5.15
      - qt-version: '5.15.2'
        os: windows-2022
        preset: ci-windows
        artifact-suffix: windows-qt5.15
      - qt-version: '6.2.*'
        os: ubuntu-22.04
        preset: ci-linux
        artifact-suffix: linux-qt6.2
      - qt-version: '6.2.*'
        os: windows-2022
        preset: ci-windows
        artifact-suffix: windows-qt6.2
      - qt-version: '6.8.*'
        os: ubuntu-24.04
        preset: ci-linux
        artifact-suffix: linux-qt6.8
      - qt-version: '6.8.*'
        os: windows-2022
        preset: ci-windows
        artifact-suffix: windows-qt6.8
      - qt-version: '6.9.*'
        os: ubuntu-24.04
        preset: ci-linux
        artifact-suffix: linux-qt6.9
      - qt-version: '6.9.*'
        os: windows-2022
        preset: ci-windows
        artifact-suffix: windows-qt6.9
```

**Why `include` instead of cross-product matrix:** Different Qt versions need different Ubuntu runners. An `include`-based matrix gives explicit control over which OS pairs with which Qt version.

### vcpkg Binary Caching in CI

**Important change (2025):** The `x-gha` binary caching backend was removed from vcpkg. The current recommended approaches are:

1. **GitHub Packages NuGet feed** (recommended by Microsoft) -- uses `GITHUB_TOKEN`, stores built packages in GitHub Packages
2. **lukka/run-vcpkg@v11** built-in caching -- caches the vcpkg executable and build trees via GitHub Actions cache

**Recommendation: Use lukka/run-vcpkg@v11 with its built-in caching.** Since qtPilot has minimal vcpkg deps (nlohmann-json, spdlog are optional), the caching complexity of NuGet feeds is not worth it. The built-in action cache is sufficient.

**Update the vcpkg commit ID.** The current CI uses `vcpkgGitCommitId: '2024.01.12'` which is over 2 years old. Use a recent baseline from the vcpkg repository.

```yaml
- name: Setup vcpkg
  uses: lukka/run-vcpkg@v11
  with:
    vcpkgGitCommitId: 'a0e1fc3'  # Update to recent commit
```

### CI Environment Variables

```yaml
env:
  # Set by install-qt-action automatically:
  # Qt5_DIR or Qt6_DIR (depending on version)
  # CMAKE_PREFIX_PATH (includes Qt)

  # For CMake to find Qt:
  CMAKE_PREFIX_PATH: ${{ env.Qt5_DIR || env.Qt6_DIR }}
```

**Note:** install-qt-action@v4 sets environment variables differently depending on Qt major version. The workflow must handle both `Qt5_DIR` and `Qt6_DIR`.

---

## 2. vcpkg Port Creation

### Port Structure

A vcpkg overlay port for qtPilot should live in a `ports/` directory in the repo (or a separate registry repo).

```
ports/
  qtpilot/
    portfile.cmake
    vcpkg.json
    usage              # Shown to user after install
```

### Source Port (vcpkg.json)

```json
{
  "name": "qtpilot",
  "version": "0.1.0",
  "port-version": 0,
  "description": "Qt application introspection and automation library with MCP integration",
  "homepage": "https://github.com/ssss2art/qtPilot",
  "license": "MIT",
  "dependencies": [
    "vcpkg-cmake",
    "vcpkg-cmake-config"
  ],
  "features": {
    "qml": {
      "description": "QML/Quick introspection support",
      "dependencies": []
    }
  }
}
```

**Why no Qt dependency in vcpkg.json:** Qt must come from outside vcpkg (user's installation). The probe must link against the same Qt version as the target application. Building Qt from vcpkg would defeat the purpose.

### Source Port (portfile.cmake)

```cmake
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO ssss2art/qtPilot
    REF "v${VERSION}"
    SHA512 <hash>
    HEAD_REF main
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DQTPILOT_BUILD_TESTS=OFF
        -DQTPILOT_BUILD_TEST_APP=OFF
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME qtPilot CONFIG_PATH lib/cmake/qtPilot)
vcpkg_copy_pdbs()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
```

### Binary Overlay Port

For pre-built binaries (per Qt version), a binary overlay port downloads the correct artifact:

```cmake
# portfile.cmake for binary distribution
set(QT_MAJOR_VERSION "" CACHE STRING "Qt major version to use (5 or 6)")

if(VCPKG_TARGET_IS_WINDOWS)
    vcpkg_download_distfile(ARCHIVE
        URLS "https://github.com/ssss2art/qtPilot/releases/download/v${VERSION}/qtpilot-${VERSION}-windows-qt${QT_MAJOR_VERSION}.zip"
        FILENAME "qtpilot-${VERSION}-windows-qt${QT_MAJOR_VERSION}.zip"
        SHA512 <hash>
    )
elseif(VCPKG_TARGET_IS_LINUX)
    vcpkg_download_distfile(ARCHIVE
        URLS "https://github.com/ssss2art/qtPilot/releases/download/v${VERSION}/qtpilot-${VERSION}-linux-qt${QT_MAJOR_VERSION}.tar.gz"
        FILENAME "qtpilot-${VERSION}-linux-qt${QT_MAJOR_VERSION}.tar.gz"
        SHA512 <hash>
    )
endif()

vcpkg_extract_source_archive(SOURCE_PATH ARCHIVE "${ARCHIVE}")
file(INSTALL "${SOURCE_PATH}/include/" DESTINATION "${CURRENT_PACKAGES_DIR}/include")
file(INSTALL "${SOURCE_PATH}/lib/" DESTINATION "${CURRENT_PACKAGES_DIR}/lib")
# ... etc
```

### Using Overlay Ports

Users configure via `vcpkg-configuration.json`:

```json
{
  "overlay-ports": ["./ports"]
}
```

Or via command line: `--overlay-ports=./ports`

Or via environment variable: `VCPKG_OVERLAY_PORTS=./ports`

---

## 3. Python PyPI Publishing

### Build Toolchain

| Tool | Version | Purpose | Confidence |
|------|---------|---------|------------|
| **hatchling** | 1.28.0 | Build backend (already in pyproject.toml) | HIGH |
| **hatch** | (latest) | Build frontend (optional, `python -m build` also works) | HIGH |
| **pypa/gh-action-pypi-publish** | @release/v1 (v1.12.3+) | Trusted publisher upload | HIGH |

**Why keep hatchling (not switch to setuptools or poetry):**
- Already configured in `python/pyproject.toml` -- no migration needed
- Hatchling 1.28.0 is current and well-maintained (released Nov 2025)
- Supports PEP 517/518/621 natively
- Lighter than poetry (no lock file needed for a library)
- Better than setuptools for modern Python packaging

### Trusted Publishers (no API tokens needed)

PyPI Trusted Publishers with OIDC is the modern standard. No `PYPI_TOKEN` secret required.

**Setup steps:**
1. On PyPI, go to project settings > Publishing > Add GitHub Actions as trusted publisher
2. Configure: owner=`ssss2art`, repo=`qtPilot`, workflow=`release.yml`, environment=`pypi`
3. In workflow, set `permissions: id-token: write`

### pyproject.toml Additions

The existing `python/pyproject.toml` needs metadata additions for PyPI:

```toml
[project]
name = "qtpilot"
version = "0.1.0"
description = "MCP server for controlling Qt applications via qtPilot probe"
requires-python = ">=3.11"
license = "MIT"
authors = [
    {name = "qtPilot Contributors"}
]
readme = "README.md"
classifiers = [
    "Development Status :: 3 - Alpha",
    "License :: OSI Approved :: MIT License",
    "Programming Language :: Python :: 3",
    "Programming Language :: Python :: 3.11",
    "Programming Language :: Python :: 3.12",
    "Programming Language :: Python :: 3.13",
    "Topic :: Software Development :: Testing",
    "Topic :: Software Development :: Quality Assurance",
]
keywords = ["mcp", "qt", "automation", "introspection"]
dependencies = [
    "fastmcp>=2.0,<3",
    "websockets>=14.0",
]

[project.urls]
Homepage = "https://github.com/ssss2art/qtPilot"
Repository = "https://github.com/ssss2art/qtPilot"
Issues = "https://github.com/ssss2art/qtPilot/issues"

[project.scripts]
qtpilot = "qtpilot.cli:main"

[build-system]
requires = ["hatchling"]
build-backend = "hatchling.build"

[tool.hatch.build.targets.sdist]
include = ["src/qtpilot/**"]

[tool.hatch.build.targets.wheel]
packages = ["src/qtpilot"]
```

### Publish Workflow Snippet

```yaml
publish-pypi:
  name: Publish to PyPI
  runs-on: ubuntu-latest
  needs: [build-matrix]  # Wait for all C++ builds to pass
  if: github.ref_type == 'tag'
  environment:
    name: pypi
    url: https://pypi.org/p/qtpilot
  permissions:
    id-token: write  # Required for trusted publishing
  steps:
    - uses: actions/checkout@v4
    - uses: actions/setup-python@v5
      with:
        python-version: '3.11'
    - name: Build package
      run: |
        pip install build
        python -m build python/
    - name: Publish to PyPI
      uses: pypa/gh-action-pypi-publish@release/v1
      with:
        packages-dir: python/dist/
```

---

## 4. GitHub Releases Automation

### Release Action

| Tool | Version | Purpose | Confidence |
|------|---------|---------|------------|
| **softprops/action-gh-release** | **@v2** | Create release with artifacts | HIGH |

**Why softprops/action-gh-release over `gh release create`:**
- Declarative (YAML config vs imperative shell commands)
- Built-in glob pattern support for assets
- Handles release creation and asset upload atomically
- Idempotent (updates existing release if tag already has one)

### Artifact Naming Convention

Each CI matrix job uploads artifacts with a consistent naming scheme:

```
qtpilot-{version}-{platform}-qt{qt_version}.{ext}
```

Examples:
- `qtpilot-0.1.0-windows-qt5.15.zip`
- `qtpilot-0.1.0-windows-qt6.8.zip`
- `qtpilot-0.1.0-linux-qt5.15.tar.gz`
- `qtpilot-0.1.0-linux-qt6.8.tar.gz`

### Release Workflow Pattern

```yaml
release:
  name: Create Release
  runs-on: ubuntu-latest
  needs: [build-matrix, publish-pypi]
  if: github.ref_type == 'tag'
  permissions:
    contents: write
  steps:
    - uses: actions/download-artifact@v4
      with:
        path: artifacts/
        merge-multiple: true

    - name: Create Release
      uses: softprops/action-gh-release@v2
      with:
        files: artifacts/**/*
        generate_release_notes: true
        draft: false
        prerelease: ${{ contains(github.ref_name, '-rc') || contains(github.ref_name, '-beta') }}
```

### Artifact Collection Strategy

Each matrix build job uploads its artifacts separately:

```yaml
- name: Package artifacts
  run: |
    mkdir -p dist
    # Copy probe DLL/SO, launcher, headers
    # Create archive with consistent naming

- uses: actions/upload-artifact@v4
  with:
    name: qtpilot-${{ matrix.artifact-suffix }}
    path: dist/
    retention-days: 5
```

The release job uses `actions/download-artifact@v4` with `merge-multiple: true` to collect all matrix artifacts into a single directory.

---

## 5. CMake Changes for Multi-Qt-Version Compatibility

### Current State (Issues to Fix)

The existing CMakeLists.txt has several issues for distribution:

1. **qtPilotConfig.cmake.in hardcodes Qt5:** Line `find_dependency(Qt5 5.15 ...)` will fail for Qt6 consumers
2. **cmake_minimum_required says 3.16 but presets say 3.16 too:** Should be 3.16 minimum (fine for Qt5/6)
3. **No versionless target support:** Project uses explicit `Qt5::` / `Qt6::` branches everywhere
4. **No Qt version in output filenames:** `qtPilot-probe.dll` is identical regardless of Qt version built against

### Qt5/Qt6 Compatibility Patterns

**Official Qt recommendation (from doc.qt.io):** Use `QT_VERSION_MAJOR` variable approach for supporting Qt < 5.15 versionless targets. Since qtPilot already does this, the pattern is correct. However, it should be streamlined.

**Current approach (acceptable):**
```cmake
find_package(Qt6 QUIET COMPONENTS Core ...)
if(NOT Qt6_FOUND)
    find_package(Qt5 5.15 REQUIRED COMPONENTS Core ...)
endif()
# Then use Qt${QT_VERSION_MAJOR}::Core everywhere
```

**Alternative (cleaner, uses versionless targets):**
```cmake
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Network WebSockets Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core CorePrivate Network WebSockets Widgets)
# Can now use Qt${QT_VERSION_MAJOR}::Core or Qt::Core (if Qt 5.15+)
```

**Recommendation: Keep the current explicit approach** because:
- It already works
- `CorePrivate` handling differs between Qt5 and Qt6
- Versionless targets have a pitfall: "Projects must not export targets that expose the versionless targets" (official Qt docs)
- Since qtPilot exports CMake targets (`qtPilotTargets.cmake`), it MUST use versioned targets in exports

### Required CMake Fixes

#### Fix 1: qtPilotConfig.cmake.in must be Qt-version-aware

The current config hardcodes Qt5. It must detect which Qt version the consumer has:

```cmake
@PACKAGE_INIT@

include(CMakeFindDependencyMacro)

# Find the same Qt version that qtPilot was built against
set(_QTPILOT_QT_MAJOR_VERSION @QT_VERSION_MAJOR@)

if(_QTPILOT_QT_MAJOR_VERSION EQUAL 6)
    find_dependency(Qt6 @QT_MIN_VERSION_6@ COMPONENTS Core Widgets WebSockets)
else()
    find_dependency(Qt5 @QT_MIN_VERSION_5@ COMPONENTS Core Widgets WebSockets)
endif()

# Optional dependencies (only if qtPilot was built with them)
if(@QTPILOT_HAS_NLOHMANN_JSON@)
    find_dependency(nlohmann_json)
endif()
if(@QTPILOT_HAS_SPDLOG@)
    find_dependency(spdlog)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/qtPilotTargets.cmake")

check_required_components(qtPilot)
```

#### Fix 2: Qt version in output filename

For distributing binaries built against different Qt versions, the output filename should include the Qt version:

```cmake
# Encode Qt version in library name for binary distribution
option(QTPILOT_VERSIONED_OUTPUT "Include Qt version in output filename" OFF)

if(QTPILOT_VERSIONED_OUTPUT)
    set_target_properties(qtPilot_probe PROPERTIES
        OUTPUT_NAME "qtPilot-probe-qt${QT_VERSION_MAJOR}.${QT_VERSION_MINOR}"
    )
endif()
```

This produces: `qtPilot-probe-qt5.15.dll`, `qtPilot-probe-qt6.8.dll`, etc.

For CI builds: `-DQTPILOT_VERSIONED_OUTPUT=ON`
For local dev: leave OFF (default) for simplicity.

#### Fix 3: Install component separation

Allow installing just the probe, just headers, or just CMake config:

```cmake
install(TARGETS qtPilot_probe
    EXPORT qtPilotTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Runtime
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Development
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime
)

install(DIRECTORY src/probe/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/qtpilot
    COMPONENT Development
    FILES_MATCHING PATTERN "*.h"
)
```

#### Fix 4: Qt 5 vs 6 API differences to watch

| API | Qt 5 | Qt 6 | Impact |
|-----|------|------|--------|
| `QVariant::type()` | Returns `QVariant::Type` | Deprecated, use `typeId()` or `metaType()` | Probe introspection code |
| `QList` | Backed by `QList` (pointer-based) | Backed by `QVector` (contiguous) | Mostly transparent |
| `QStringRef` | Available | Removed, use `QStringView` | If used in JSON parsing |
| `QRegExp` | Available | Removed, use `QRegularExpression` | If used anywhere |
| `QTextCodec` | Available | Moved to Qt5Compat | Should not be needed |
| `UNICODE` define | Not set by CMake | Set by default in Qt6 | Already handled (NOMINMAX etc.) |

**Recommendation:** Use `#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)` guards for divergent APIs. The existing codebase likely already handles most of these if it builds with both Qt5 and Qt6.

---

## 6. Complete Workflow Architecture

### Workflow Files Needed

```
.github/
  workflows/
    ci.yml              # Matrix build on push/PR (existing, needs upgrade)
    release.yml         # Tag-triggered: build + package + release + PyPI publish
```

### ci.yml Responsibilities
- Lint (clang-format)
- Build matrix: 4 Qt versions x 2 platforms = 8 jobs
- Run tests per matrix cell
- Upload artifacts (for PR review, not release)
- Python tests (separate job, no Qt needed)
- CodeQL analysis

### release.yml Responsibilities
- Triggered by tag push (`v*`)
- Same build matrix as CI (reuse via workflow call or duplication)
- Package artifacts with consistent naming
- Create GitHub Release with all artifacts
- Publish Python package to PyPI
- Optionally publish to TestPyPI first

### Workflow Trigger Pattern

```yaml
# ci.yml
on:
  push:
    branches: [main, develop]
  pull_request:
    branches: [main]

# release.yml
on:
  push:
    tags: ['v*']
```

---

## 7. Alternatives Considered

| Category | Recommended | Alternative | Why Not |
|----------|-------------|-------------|---------|
| Qt installer | jurplel/install-qt-action@v4 | Manual aqtinstall pip install | Action handles caching, env vars, path setup automatically |
| vcpkg caching | lukka/run-vcpkg@v11 built-in | GitHub Packages NuGet feed | Overkill for 2-3 optional deps |
| Python build | hatchling | setuptools, poetry | Already configured, modern, lightweight |
| PyPI publish | Trusted Publishers (OIDC) | API token secret | More secure, no secret management, PyPI recommended |
| Release creation | softprops/action-gh-release@v2 | `gh release create` | Declarative, idempotent, glob support |
| CMake Qt compat | QT_VERSION_MAJOR variable | Versionless targets (Qt::Core) | Cannot export versionless targets; must use versioned in installed config |

---

## 8. Version Summary

| Component | Current in Repo | Recommended | Action Needed |
|-----------|-----------------|-------------|---------------|
| install-qt-action | v3 | **v4** | Upgrade |
| run-vcpkg | v11 (old commit) | **v11** (recent commit) | Update commit ID |
| upload-artifact | v4 | v4 | OK |
| softprops/action-gh-release | (not used) | **v2** | Add |
| pypa/gh-action-pypi-publish | (not used) | **@release/v1** | Add |
| actions/setup-python | v5 | v5 | OK |
| hatchling | 1.x | **1.28.0** | OK (already configured) |
| Ubuntu runner | ubuntu-22.04 | **ubuntu-22.04** (Qt5) / **ubuntu-24.04** (Qt6.8+) | Split by Qt version |
| Windows runner | windows-2022 | **windows-2022** | OK |
| CMakePresets version | 6 | 6 | OK |
| CMake minimum | 3.16 | 3.16 | OK |

---

## Sources

### Verified (HIGH Confidence)
- [jurplel/install-qt-action](https://github.com/jurplel/install-qt-action) -- v4, default Qt 6.8.3, aqtinstall 3.2.x
- [install-qt-action README](https://github.com/jurplel/install-qt-action/blob/master/README.md) -- Configuration options, caching, modules
- [Qt 5.15.2 availability issue #283](https://github.com/jurplel/install-qt-action/issues/283) -- Windows x86 issues, open-source availability
- [softprops/action-gh-release](https://github.com/softprops/action-gh-release) -- v2 latest, file glob support
- [pypa/gh-action-pypi-publish](https://github.com/pypa/gh-action-pypi-publish) -- release/v1, v1.12.3, trusted publishers
- [hatchling on PyPI](https://pypi.org/project/hatchling/) -- v1.28.0 (Nov 2025)
- [lukka/run-vcpkg](https://github.com/lukka/run-vcpkg) -- v11.5, binary caching
- [vcpkg overlay ports docs](https://learn.microsoft.com/en-us/vcpkg/concepts/overlay-ports) -- Port structure and resolution
- [vcpkg binary caching with GitHub Packages](https://learn.microsoft.com/en-us/vcpkg/consume/binary-caching-github-packages) -- NuGet feed approach
- [Qt CMake Qt5/Qt6 compatibility](https://doc.qt.io/qt-6/cmake-qt5-and-qt6-compatibility.html) -- Official versionless targets guidance
- [Qt versionless CMake targets blog](https://www.qt.io/blog/versionless-cmake-targets-qt-5.15) -- Qt 5.15 versionless target introduction
- [GitHub Actions ubuntu-latest migration](https://github.com/actions/runner-images/issues/10636) -- ubuntu-latest = 24.04 since Jan 2025
- [PyPI Trusted Publishers](https://packaging.python.org/en/latest/guides/publishing-package-distribution-releases-using-github-actions-ci-cd-workflows/) -- Official Python packaging guide
- [vcpkg x-gha removal issue](https://github.com/lukka/run-vcpkg/issues/251) -- x-gha cache backend removed

### Cross-Referenced (MEDIUM Confidence)
- [KDAB GitHub Actions for Qt](https://www.kdab.com/github-actions-for-cpp-and-qt/) -- Matrix build patterns
- [Qt Forum CI best practices](https://forum.qt.io/topic/161356/github-actions-ci-best-known-methods-to-support-qt-applications) -- Community patterns
- [PyOpenSci Python packaging guide](https://www.pyopensci.org/python-package-guide/tutorials/publish-pypi.html) -- Trusted publisher workflow
