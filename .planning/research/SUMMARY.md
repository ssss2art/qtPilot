# Project Research Summary

**Project:** qtPilot v1.1 - Distribution & Compatibility Milestone
**Domain:** Multi-platform Qt injection library distribution, CI/CD, and packaging
**Researched:** 2026-02-01
**Confidence:** HIGH

## Executive Summary

qtPilot v1.1 focuses on distributing an existing Qt introspection/injection library across multiple Qt versions (5.15.2, 5.15.1-patched, 6.2, 6.8, 6.9) and platforms (Windows MSVC, Linux GCC). The core constraint is ABI compatibility: the probe DLL/SO must be compiled against the exact Qt version used by the target application. This creates a distribution matrix of 10 binary artifacts (5 Qt versions x 2 platforms), each requiring dedicated CI builds.

The recommended approach leverages GitHub Actions matrix builds with install-qt-action@v4, vcpkg overlay ports for source builds, PyPI for the pure-Python MCP server, and GitHub Releases for prebuilt binaries. The critical challenges are: (1) building and caching the custom-patched Qt 5.15.1 in CI (30-60 min build time), (2) ensuring vcpkg ports don't pull their own Qt instead of using the user's installation, and (3) preventing artifact name collisions in parallel matrix builds.

Key risks include ABI matrix explosion (missing binaries), vcpkg dependency mis-configuration (building its own Qt), and CI flakiness from aqtinstall hangs. Mitigation requires explicit matrix enumeration, Qt-version-encoded output names, aggressive caching, and runtime ABI verification in the probe.

## Key Findings

### Recommended Stack

The existing CMake 3.16 + Qt 5/6 + vcpkg manifest foundation is solid. Distribution adds GitHub Actions workflows (ci.yml upgrade, new release.yml), vcpkg overlay ports, and PyPI trusted publishing.

**Core technologies:**
- **jurplel/install-qt-action@v4** (upgrade from v3) — installs Qt 5.15.2, 6.2, 6.8, 6.9 in CI; v4 uses aqtinstall 3.2.x which fixes Qt 6.7+ issues
- **lukka/run-vcpkg@v11** — already in use but needs vcpkg commit ID update; built-in caching sufficient (no NuGet complexity needed)
- **softprops/action-gh-release@v2** — declarative release creation with glob support for 20 artifacts (10 binaries + 10 checksums)
- **pypa/gh-action-pypi-publish@release/v1** — trusted publisher OIDC (no API tokens), publishes pure-Python wheel
- **hatchling 1.28.0** — already configured in pyproject.toml, modern PEP 517/518/621 support

**Critical: Qt version encoding in output filenames.** CMake must produce `qtPilot-probe-qt5.15.dll`, `qtPilot-probe-qt6.8.dll`, etc. This is NOT currently implemented (all builds produce identical names). Requires adding `QTPILOT_QT_SUFFIX` to CMakeLists.txt.

**Critical: qtPilotConfig.cmake.in hardcodes Qt5.** The installed CMake package config uses `find_dependency(Qt5 5.15 ...)` even when built against Qt6. Must be fixed to use `@QT_VERSION_MAJOR@` variable.

### Expected Features

Distribution features are infrastructure-focused, not user-facing. The probe functionality itself is unchanged from v1.0.

**Must have (table stakes):**
- Prebuilt probe binaries for all 5 Qt versions x 2 platforms — users expect drop-in DLLs/SOs
- vcpkg port for source builds — C++ users expect vcpkg integration
- PyPI package for Python MCP server — Python users expect `pip install qtpilot`
- GitHub Releases with tagged versions — standard distribution channel
- CI/CD automation (matrix builds, artifact upload, checksum generation)

**Should have (competitive):**
- Patched Qt 5.15.1 support — unique to this project, required for certain users
- Python CLI probe downloader (`qtpilot probe download --qt-version 6.8`) — convenience over manual GitHub releases navigation
- CMake install components (separate Runtime/Development) — allows installing just probe or just headers
- SHA256 checksums for all release artifacts — security best practice

**Defer (v2+):**
- Homebrew formula (macOS packaging)
- Conan package (alternative to vcpkg)
- Docker container images with preloaded probes
- Multi-architecture support (ARM64, x86)

### Architecture Approach

Distribution architecture separates source control (single CMakeLists.txt), build matrix (10 parallel CI jobs), packaging (3 channels: GitHub Releases, vcpkg, PyPI), and artifact aggregation (release job collects all matrix outputs). The key pattern is Qt-version-aware CMake output naming feeding into matrix-based artifact upload.

**Major components:**

1. **CMake Multi-Qt Build System** — Detects Qt5/Qt6, encodes version in output name (`QTPILOT_QT_SUFFIX = qt5.15`), exports version-aware CMake config; MUST fix qtPilotConfig.cmake.in Qt5 hardcode
2. **GitHub Actions Matrix (10 cells)** — 5 Qt versions x 2 platforms; Qt 5.15.2/6.2/6.8/6.9 use install-qt-action; patched Qt 5.15.1 uses pre-built cache; uploads uniquely-named artifacts
3. **vcpkg Overlay Port (Source Build)** — Lives in `ports/qtpilot/`, does NOT declare qtbase as vcpkg dependency (treats Qt as external), expects `CMAKE_PREFIX_PATH` to user's Qt
4. **Python PyPI Package (Pure-Python)** — Wheel contains MCP server only; probe binaries distributed separately via GitHub Releases; optional download helper in `qtpilot.probe` module
5. **GitHub Release Aggregation** — Collects all 10 matrix artifacts, packages as tar.gz/zip with consistent structure, generates checksums, creates release with softprops/action-gh-release@v2

### Critical Pitfalls

1. **vcpkg Port Builds Its Own Qt Instead of Using Target's Qt (CRITICAL)** — If vcpkg.json declares `qtbase` as dependency, vcpkg builds/downloads its own Qt (~30 min), probe links wrong Qt, crashes on injection. Prevention: NO qtbase dependency; treat Qt as external; document `CMAKE_PREFIX_PATH` requirement.

2. **Probe DLL ABI Matrix Explosion (CRITICAL)** — 5 Qt versions x 2 platforms x 2 configs (Debug/Release on Windows) = 15-20 binaries. Incomplete sets or missing Qt-version encoding in filenames causes "works on 6.8, crashes on 6.9" issues. Prevention: Explicit CI matrix enumeration; Qt version in OUTPUT_NAME; runtime ABI check in probe (qVersion() verification).

3. **GitHub Actions upload-artifact@v4 Overwrites in Matrix Builds (CRITICAL)** — Parallel jobs uploading same artifact name causes 409 Conflict. v4 made artifacts immutable. Prevention: Include matrix vars in artifact names (`probe-${{ matrix.qt_version }}-${{ matrix.os }}`); use download-artifact with `merge-multiple: true` in release job.

4. **Custom-Patched Qt 5.15.1 Cannot Be Reproduced in CI (CRITICAL)** — Building Qt from source takes 30-60 min; toolchain version drift (GitHub runner updates) breaks reproducibility. Prevention: Pre-build once, cache aggressively (actions/cache@v4 keyed on patch hash + compiler version); optionally store pre-built Qt as GitHub Release artifact.

5. **PyPI Package Ships Without Pre-built Probe Binaries (MODERATE)** — Pure-Python wheel doesn't include probe DLL (correct for packaging, wrong for UX). Users `pip install qtpilot` and get runtime "probe not found" errors. Prevention: Clear docs; `qtpilot doctor` command to verify probe; optional download helper (`qtpilot probe download --qt-version 6.8`).

## Implications for Roadmap

Based on research, suggested phase structure (6 phases, strict dependency order):

### Phase 1: CMake Multi-Qt Foundation
**Rationale:** All distribution channels depend on correct Qt-version-encoded output names. Must fix CMake before CI builds anything.
**Delivers:**
- `QTPILOT_QT_SUFFIX` variable in root CMakeLists.txt
- Updated OUTPUT_NAME in probe and launcher CMakeLists.txt (e.g., `qtPilot-probe-qt5.15.dll`)
- Fixed qtPilotConfig.cmake.in (Qt5 hardcode removed, uses `@QT_VERSION_MAJOR@`)
- Per-Qt-version CMake presets (ci-linux-qt515, ci-linux-qt68, etc.)
**Addresses:** Pitfall #2 (ABI matrix), Pitfall #7 (CMake config missing Qt version)
**Risk:** LOW — pure CMake changes, local testing verifies different output names

### Phase 2: CI/CD Matrix Build
**Rationale:** Must build all 10 artifacts before releases can exist. Patched Qt is deferred to Phase 3 (high risk, separate effort).
**Delivers:**
- Restructured .github/workflows/ci.yml with matrix strategy (8 cells: 4 standard Qt versions x 2 platforms)
- install-qt-action@v4 per matrix cell
- Artifact upload with matrix-encoded names (prevents Pitfall #3)
- vcpkg caching with updated commit ID
**Addresses:** Pitfall #3 (artifact collision), core distribution requirement
**Uses:** Stack decisions (install-qt-action@v4, upload-artifact@v4, run-vcpkg@v11)
**Risk:** MEDIUM — aqtinstall can hang (Pitfall #8), mitigate with caching + retries

### Phase 3: Patched Qt 5.15.1 CI
**Rationale:** Custom-patched Qt is the highest-risk component (long build time, reproducibility). Isolate from standard matrix.
**Delivers:**
- .github/workflows/build-qt.yml (builds Qt 5.15.1 from source with patches)
- Cache strategy (actions/cache@v4 keyed on patch hash + compiler version)
- Added to ci.yml matrix as 2 additional jobs (Linux + Windows)
**Addresses:** Pitfall #4 (patched Qt reproduction)
**Risk:** HIGH — 30-60 min build time, cache hit rate critical; alternative is pre-built Qt as release artifact

### Phase 4: GitHub Releases
**Rationale:** Releases aggregate CI artifacts. Depends on Phase 2 + 3 producing all 10 binaries.
**Delivers:**
- .github/workflows/release.yml triggered on `v*` tags
- Archive packaging (tar.gz/zip with consistent internal structure)
- Checksum generation (SHA256 per artifact)
- softprops/action-gh-release@v2 with all 20 files (10 archives + 10 checksums)
**Addresses:** Core distribution requirement, Pitfall #9 (changelog mapping)
**Risk:** LOW — standard GitHub Actions pattern

### Phase 5: vcpkg Port
**Rationale:** Requires tagged releases for vcpkg_from_github. Depends on Phase 4.
**Delivers:**
- ports/qtpilot/vcpkg.json (NO qtbase dependency)
- ports/qtpilot/portfile.cmake (source build against external Qt)
- ports/qtpilot/usage (documents CMAKE_PREFIX_PATH requirement)
- Optional: ports/qtpilot-prebuilt/ (binary download variant)
**Addresses:** Pitfall #1 (vcpkg builds its own Qt)
**Risk:** MEDIUM — Pitfall #1 is subtle; testing with real user installs required

### Phase 6: PyPI Publication
**Rationale:** Python package can be parallel to vcpkg but benefits from GitHub Releases existing for probe download helper.
**Delivers:**
- Enhanced python/pyproject.toml (PyPI metadata, classifiers, URLs)
- python/src/qtpilot/probe.py (download helper for GitHub Releases)
- CLI commands: `qtpilot probe download`, `qtpilot probe list`, `qtpilot doctor`
- Trusted publisher config in release.yml (OIDC, no API token)
**Addresses:** Pitfall #5 (missing probe binaries), Pitfall #11 (name collision check)
**Risk:** LOW — pure Python packaging, well-documented

### Phase Ordering Rationale

1. **CMake first** — Output name encoding is foundational; CI and all packaging depend on it
2. **CI before releases** — Can't release what hasn't been built
3. **Standard Qt before patched Qt** — De-risk CI with 8 standard cells before tackling 30-60 min Qt build
4. **Releases before vcpkg/PyPI** — Both packaging channels reference GitHub Releases (vcpkg_from_github, probe download)
5. **vcpkg and PyPI are parallel-capable** but PyPI's probe download helper benefits from releases existing

### Research Flags

**Phases likely needing deeper research during planning:**
- **Phase 3 (Patched Qt CI):** Custom Qt build is project-specific; research covered general patterns but exact patch + configure flags need validation. Consider `/gsd:research-phase` if patches are undocumented.
- **Phase 5 (vcpkg Port):** Pitfall #1 is well-researched but specific portfile.cmake implementation may need testing. Standard patterns apply.

**Phases with standard patterns (skip research-phase):**
- **Phase 1 (CMake):** Well-documented CMake patterns, verified against existing CMakeLists.txt
- **Phase 2 (CI Matrix):** Standard GitHub Actions matrix, install-qt-action docs are complete
- **Phase 4 (Releases):** Standard softprops/action-gh-release usage
- **Phase 6 (PyPI):** Standard hatchling + trusted publisher, pyproject.toml already configured

## Confidence Assessment

| Area | Confidence | Notes |
|------|------------|-------|
| Stack | HIGH | All actions/tools verified against latest docs; install-qt-action@v4 default Qt is 6.8.3 (confirmed), vcpkg@v11 is current |
| Features | HIGH | Distribution features are standard (releases, CI, packaging); no novel features |
| Architecture | HIGH | Multi-Qt CMake pattern verified in Qt docs; matrix build verified in KDAB guides; vcpkg overlay ports in Microsoft docs |
| Pitfalls | HIGH | Top 4 critical pitfalls verified with authoritative sources (vcpkg discussions, upload-artifact v4 issues, Qt ABI forums); moderate/minor pitfalls cross-referenced |

**Overall confidence:** HIGH

### Gaps to Address

- **Patched Qt 5.15.1 build recipe specifics:** Research assumes patches exist and are documented. If patches are missing or incomplete, Phase 3 requires creating/documenting them before CI can build. Fallback: Ship patched Qt as pre-built artifact instead of building in CI.

- **PyPI package name availability:** "qtpilot" availability on PyPI not verified. Check early in Phase 6 (trivial fix: use "qt-mcp" or "qtpilot-server" if taken).

- **Exact CI build times for patched Qt:** Estimate is 30-60 min based on Vector35/qt-build; actual time may vary. Budget conservatively; consider 2-hour timeout on build-qt.yml jobs.

- **vcpkg port testing with real consumer projects:** Pitfall #1 prevention (no qtbase dependency) must be validated with actual user workflows. Test: Consumer with existing Qt installation runs `vcpkg install qtpilot --overlay-ports=./ports` and confirms it doesn't build Qt.

## Sources

### Primary (HIGH confidence)
- [jurplel/install-qt-action GitHub](https://github.com/jurplel/install-qt-action) — v4 configuration, aqtinstall 3.2.x, Qt 6.8.3 default
- [Qt 5 and Qt 6 CMake Compatibility](https://doc.qt.io/qt-6/cmake-qt5-and-qt6-compatibility.html) — Official Qt dual-version patterns
- [vcpkg Overlay Ports](https://learn.microsoft.com/en-us/vcpkg/concepts/overlay-ports) — Port structure, resolution order
- [vcpkg: External Qt discussions](https://github.com/microsoft/vcpkg/discussions/46814) — Pitfall #1 source
- [actions/upload-artifact v4](https://github.com/actions/upload-artifact) — Artifact immutability, name collision (Pitfall #3)
- [softprops/action-gh-release](https://github.com/softprops/action-gh-release) — v2 glob support, release creation
- [pypa/gh-action-pypi-publish](https://github.com/pypa/gh-action-pypi-publish) — Trusted publishers, OIDC workflow
- [CMake Package Config Files Guide](https://cmake.org/cmake/help/latest/guide/importing-exporting/index.html) — qtPilotConfig.cmake.in fix pattern

### Secondary (MEDIUM confidence)
- [KDAB: GitHub Actions for Qt](https://www.kdab.com/github-actions-for-cpp-and-qt/) — Matrix strategy patterns
- [Vector35/qt-build](https://github.com/Vector35/qt-build) — Custom Qt from source in CI (30-60 min estimate source)
- [GammaRay Probe ABI System](https://github.com/KDAB/GammaRay/wiki/Roadmap) — ABI versioning patterns
- [Qt Forum: CI Best Practices](https://forum.qt.io/topic/161356/github-actions-ci-best-known-methods-to-support-qt-applications) — Community consensus on caching, runners
- [hatchling PyPI page](https://pypi.org/project/hatchling/) — v1.28.0 current, modern Python packaging

### Tertiary (LOW confidence)
- Patched Qt 5.15.1 build times (30-60 min estimate not measured on GitHub Actions runners)
- GitHub Actions 10GB cache limit interaction with multi-Qt caching (may need tuning in practice)

---
*Research completed: 2026-02-01*
*Ready for roadmap: yes*
