# Domain Pitfalls

**Domain:** Distribution & packaging for Qt injection library
**Project:** qtPilot
**Researched:** 2026-02-01
**Confidence:** HIGH (verified against multiple authoritative sources)
**Scope:** Pitfalls specific to ADDING distribution (vcpkg, PyPI, GitHub Releases, CI) to an existing C++/Qt injection library

---

## Critical Pitfalls

Mistakes that cause broken releases, unusable packages, or require architectural rework.

---

### Pitfall 1: vcpkg Port Builds Its Own Qt Instead of Using the Target's Qt

**What goes wrong:** When creating a vcpkg port for qtPilot, vcpkg resolves the `qtbase` dependency from its own registry and builds/downloads a vcpkg-managed Qt. Consumers who have Qt installed via the official installer or a custom build end up with TWO Qt installations -- the vcpkg one linked into qtPilot and their own. The probe DLL then links against the wrong Qt, causing immediate ABI mismatch crashes when injected into the target app.

**Why it happens:** vcpkg's design intentionally prefers its own packages. Declaring `qtbase` as a dependency in `vcpkg.json` triggers vcpkg to provide Qt, even if the consumer already has Qt installed. This is doubly problematic for qtPilot because the probe MUST match the target app's exact Qt version -- there is no "one Qt fits all."

**Warning signs:**
- vcpkg build takes 30+ minutes (it is building Qt from source)
- `ldd` or Dependency Walker shows the probe linking to Qt DLLs in the vcpkg installed tree, not the target app's Qt
- Probe crashes immediately on injection with symbol resolution failures
- Consumer's `find_package(Qt6)` finds a different Qt than the one qtPilot was built against

**Prevention:**
1. **Do NOT declare qtbase as a vcpkg dependency.** Instead, treat Qt as an external/system dependency. Use `find_package(Qt6 REQUIRED)` in CMakeLists.txt and document that Qt must be pre-installed
2. **Use an overlay port approach** where the portfile expects Qt via `CMAKE_PREFIX_PATH` rather than vcpkg dependency resolution
3. **Consider a "header-only" vcpkg port** that ships CMake config and headers, with the actual probe DLL built separately per Qt version
4. **Document clearly:** "This port requires Qt to be installed externally. Set `CMAKE_PREFIX_PATH` to your Qt installation."
5. **Alternative:** Provide multiple vcpkg ports or features per Qt major version (e.g., `qtpilot[qt5]`, `qtpilot[qt6]`)

**Confidence:** HIGH -- verified via [vcpkg discussions on external Qt](https://github.com/microsoft/vcpkg/discussions/46814) and [vcpkg issue #27574](https://github.com/microsoft/vcpkg/issues/27574)

**Phase impact:** Must be resolved in vcpkg port design phase. Getting this wrong means the port is fundamentally broken.

---

### Pitfall 2: Probe DLL ABI Matrix Explosion

**What goes wrong:** qtPilot must ship probe DLLs that exactly match the target app's Qt version + compiler + build config. With 5 Qt versions (5.15, 5.15.1-patched, 6.2, 6.8, 6.9) x 2 platforms (Windows MSVC, Linux GCC) x 2 configs (Debug/Release on Windows), that is 15-20 distinct binaries. Teams underestimate this matrix and ship incomplete sets, or worse, ship a single "universal" binary that crashes on version mismatch.

**Why it happens:** Unlike normal libraries where "close enough" ABI compatibility works, injection probes must match the host process exactly. Qt does NOT guarantee ABI stability across minor versions for private headers, and qtPilot uses `CorePrivate`. Even patch-level differences in Qt can break private API users. The custom-patched Qt 5.15.1 adds another dimension.

**Warning signs:**
- CI matrix has fewer jobs than Qt version x platform combinations
- Artifact names don't encode Qt version + compiler + config
- Release assets are missing expected platform/version combinations
- Users report "works with Qt 6.8 but crashes with 6.9"

**Prevention:**
1. **Explicitly enumerate the full build matrix** in CI configuration. Every Qt version x platform x config combination must be a separate CI job
2. **Encode the full ABI identity in artifact names:** `qtPilot-probe-qt6.8.0-msvc2022-x64-release.dll`
3. **Add runtime ABI verification:** The probe should check `qVersion()` at load time and refuse to initialize if there is a mismatch, printing a clear error instead of crashing
4. **Automate completeness checks:** A CI step should verify that all expected artifacts were produced before creating a release
5. **For the custom-patched Qt 5.15.1:** Document the exact patch and provide build scripts so users can reproduce it

**Confidence:** HIGH -- this is the core challenge for injection tools, validated by [GammaRay's probe ABI system](https://github.com/KDAB/GammaRay/wiki/Roadmap) and [Qt Forum ABI discussions](https://forum.qt.io/topic/156047/qt-6-7-0-statically-built-with-llvm-mingw1706_64-has-different-abi-from-compiler)

**Phase impact:** Must be designed into CI from the start. Retrofitting ABI-aware naming is painful.

---

### Pitfall 3: GitHub Actions upload-artifact v4 Overwrites in Matrix Builds

**What goes wrong:** In a matrix build (multiple Qt versions x platforms), all jobs try to upload artifacts with the same name. With `upload-artifact@v4`, this is no longer silently merged -- it throws a `409 Conflict` error and the build fails. Even with `overwrite: true`, parallel jobs can race and produce conflicts.

**Why it happens:** `upload-artifact@v4` made artifacts immutable. Unlike v3 which silently merged (and sometimes corrupted) same-named artifacts from parallel jobs, v4 rejects duplicate names entirely. This is a breaking change that bites projects migrating their CI.

**Warning signs:**
- CI fails with "Failed to CreateArtifact: (409) Conflict: an artifact with this name already exists"
- Only artifacts from the first-finishing matrix job survive
- Release has artifacts from only one platform/Qt version

**Prevention:**
1. **Always include matrix variables in artifact names:**
   ```yaml
   - uses: actions/upload-artifact@v4
     with:
       name: probe-${{ matrix.qt_version }}-${{ matrix.os }}-${{ matrix.build_type }}
       path: build/lib/*
   ```
2. **Use `download-artifact@v4` with `merge-multiple: true`** in the release job to collect all matrix outputs:
   ```yaml
   - uses: actions/download-artifact@v4
     with:
       pattern: probe-*
       merge-multiple: true
       path: all-artifacts/
   ```
3. **Never use `overwrite: true` in parallel matrix jobs** -- it races
4. **Set `fail-fast: false`** on the matrix so one failure does not cancel other builds

**Confidence:** HIGH -- [upload-artifact v4 breaking change documented](https://github.com/actions/upload-artifact) and [issue #506 on overwrite race](https://github.com/actions/upload-artifact/issues/506)

**Phase impact:** CI setup phase. Easy to fix if caught early, wastes hours of debugging if not.

---

### Pitfall 4: Custom-Patched Qt 5.15.1 Cannot Be Reproduced in CI

**What goes wrong:** The custom-patched Qt 5.15.1 build works locally but cannot be reproduced in GitHub Actions CI. The patch may depend on specific source archives, build flags, or host toolchain versions that are not documented. CI either fails to build it or produces a binary-incompatible result.

**Why it happens:** Custom Qt builds are complex (30+ minute build times, hundreds of configure options). Without pinned source hashes, exact configure flags, and compiler versions, the output is not reproducible. GitHub Actions runners update their toolchains regularly, silently changing compiler minor versions.

**Warning signs:**
- CI build of patched Qt succeeds but probe crashes when loaded into locally-built patched-Qt app
- Build takes 45+ minutes, eating CI minutes budget
- Different CI runs produce different probe binaries (non-reproducible)
- "It works on my machine" but not in CI

**Prevention:**
1. **Pre-build the patched Qt and cache it as a CI artifact or in a binary cache.** Do NOT build Qt from source on every CI run
2. **Document the exact recipe:** Source tarball SHA256, patch file, configure command, compiler version
3. **Use vcpkg binary caching** with `--x-abi-tools-use-exact-versions` to pin tool versions
4. **Pin the GitHub Actions runner image** (e.g., `ubuntu-22.04` not `ubuntu-latest`) to avoid toolchain drift
5. **Store the pre-built patched Qt in GitHub Releases** as a bootstrapping artifact, or use a Docker container with it pre-installed
6. **Consider GitHub Actions cache** for the built Qt, keyed on patch hash + compiler version:
   ```yaml
   - uses: actions/cache@v4
     with:
       path: /opt/qt-5.15.1-patched
       key: qt-5.15.1-patched-${{ hashFiles('patches/qt5151.patch') }}-gcc-${{ steps.gcc-ver.outputs.version }}
   ```
7. **Budget CI minutes:** Building Qt from source costs 30-60 minutes. At 5+ Qt versions, that is 2.5-5 hours per CI run without caching

**Confidence:** HIGH -- [vcpkg binary caching troubleshooting](https://learn.microsoft.com/en-us/vcpkg/users/binarycaching-troubleshooting), [Qt Forum CI discussion](https://forum.qt.io/topic/161356/github-actions-ci-best-known-methods-to-support-qt-applications)

**Phase impact:** Must be solved before CI can work. This is the single biggest CI infrastructure challenge.

---

## Moderate Pitfalls

Mistakes that cause delays, user confusion, or require rework.

---

### Pitfall 5: PyPI Package Ships Without Pre-built Probe Binaries

**What goes wrong:** The Python `qtpilot` package is published as a pure-Python wheel (correctly, since it has no C extensions), but users `pip install qtpilot` and expect the probe DLL/SO to be included. It is not. Users get runtime errors when trying to connect to a Qt application because the probe binary is not on their system.

**Why it happens:** The Python package is a pure-Python MCP server that communicates with the injected probe via WebSocket. The probe DLL is a separate C++ artifact. There is a fundamental packaging mismatch: the Python package depends on a platform-specific C++ binary that cannot be distributed through PyPI's wheel system (wrong ABI model -- the probe must match the TARGET app's Qt, not the Python environment's platform).

**Warning signs:**
- Users file issues: "qtpilot installed but cannot find probe"
- Documentation says "install from PyPI" without mentioning the probe
- `qtpilot connect` fails with "probe not found" or similar

**Prevention:**
1. **Make the PyPI package clearly document that probe binaries are separate.** Add a prominent note in the package description and `--help` output
2. **Add a `qtpilot doctor` or `qtpilot check` command** that verifies probe availability and reports what is missing
3. **Support a `QTPILOT_PROBE_PATH` environment variable** so users can point to their probe build
4. **Consider bundling pre-built probes as optional pip extras** using platform-specific wheels:
   - `pip install qtpilot[qt68-win64]` downloads a platform wheel containing the Qt 6.8 Windows probe
   - This requires separate PyPI packages per Qt version (complex but user-friendly)
5. **Alternative: Ship probes via GitHub Releases** and have the Python CLI download the right one:
   ```
   qtpilot install-probe --qt-version 6.8 --platform windows
   ```
6. **At minimum:** Include clear installation instructions linking to GitHub Releases for probe binaries

**Confidence:** MEDIUM -- based on analysis of the existing `python/pyproject.toml` (pure Python, no C extensions) and analogous projects like GammaRay

**Phase impact:** Python packaging phase. User experience issue -- not a crash, but causes abandonment.

---

### Pitfall 6: Version Tag and Package Version Drift

**What goes wrong:** The Git tag says `v1.1.0`, the CMakeLists.txt says `0.1.0`, the Python `pyproject.toml` says `0.1.0`, and the vcpkg port says `1.0.0`. Users cannot tell which versions are compatible with each other. Release automation picks up the wrong version.

**Why it happens:** Multiple version sources exist: `CMakeLists.txt` (`project(VERSION ...)`), `vcpkg.json`, `python/pyproject.toml`, and Git tags. Without single-source-of-truth automation, they drift. This is already visible: CMakeLists.txt and pyproject.toml both say `0.1.0` but vcpkg.json also says `0.1.0` -- all fine now, but manual coordination will fail as the project grows.

**Warning signs:**
- Git tag does not match any package version
- Users report installing "version X" from PyPI but CMake reports "version Y"
- Release notes reference the wrong version
- vcpkg port version lags behind Git tags

**Prevention:**
1. **Single source of truth:** Use `CMakeLists.txt` `project(VERSION X.Y.Z)` as the canonical version. Derive all others from it
2. **Automate version extraction in CI:**
   ```bash
   VERSION=$(grep 'project(qtPilot' CMakeLists.txt | grep -oP 'VERSION \K[0-9.]+')
   ```
3. **Use Git tags as release triggers:** Tag creation triggers CI which reads version from CMakeLists.txt and validates it matches the tag
4. **Fail CI if versions disagree:** Add a check step that compares versions across all files
5. **For Python:** Use `hatch-vcs` or similar to derive version from Git tags automatically instead of hardcoding in pyproject.toml
6. **For vcpkg:** The port version in `vcpkg.json` should match the `port-version` field in the registry, and the `version` should match the Git tag

**Confidence:** HIGH -- this is a universal packaging pitfall, visible in the current codebase where three files independently declare `0.1.0`

**Phase impact:** Release automation phase. Must be solved before the first public release.

---

### Pitfall 7: CMake Install/Export Targets Miss Qt Version Dependency

**What goes wrong:** The `qtPilotConfig.cmake` generated by `install(EXPORT ...)` does not encode which Qt version it was built against. A consumer does `find_package(qtPilot)` and links successfully at CMake time, but the resulting binary crashes because it linked against Qt 6.9 while the qtPilot probe was built against Qt 6.8.

**Why it happens:** The current CMakeLists.txt uses `SameMajorVersion` compatibility, which would allow a Qt6.8-built qtPilot to be found by a Qt6.9 project. For normal libraries this is fine, but for an injection probe it is wrong -- the probe MUST match exactly.

**Warning signs:**
- Consumer builds succeed but probe crashes on injection
- `find_package(qtPilot)` succeeds even when Qt versions differ
- No error at configure time, crash at runtime

**Prevention:**
1. **Encode the Qt version in the CMake package name or config:** e.g., `qtPilot-Qt6.8` instead of just `qtPilot`
2. **Add a version check in `qtPilotConfig.cmake.in`:**
   ```cmake
   # In qtPilotConfig.cmake.in
   set(QTPILOT_QT_VERSION "@QT_VERSION@")
   find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core)
   if(NOT "${Qt${QT_VERSION_MAJOR}_VERSION}" VERSION_EQUAL "${QTPILOT_QT_VERSION}")
     message(FATAL_ERROR "qtPilot was built against Qt ${QTPILOT_QT_VERSION} but found Qt ${Qt${QT_VERSION_MAJOR}_VERSION}. ABI mismatch.")
   endif()
   ```
3. **Use `ExactVersion` instead of `SameMajorVersion`** for the package compatibility check, or better yet, encode Qt version in the package version scheme (e.g., `0.1.0+qt6.8.0`)
4. **Install to Qt-version-specific paths:** `lib/cmake/qtPilot-Qt6.8/` so different Qt builds coexist

**Confidence:** HIGH -- verified against the existing `CMakeLists.txt` lines 249-253 which use `SameMajorVersion`

**Phase impact:** vcpkg port / CMake packaging phase. Must be fixed before any distribution.

---

### Pitfall 8: aqtinstall/install-qt-action Hangs or Fails in CI

**What goes wrong:** The `install-qt-action` (which wraps aqtinstall) hangs during download, fails due to Qt mirror server issues, or installs the wrong architecture. With 5 Qt versions in the matrix, failures multiply -- a single flaky download can block the entire release pipeline.

**Why it happens:** aqtinstall downloads Qt from Qt's official servers, which are not 100% reliable. Known issues include hangs after download completion on Windows/Linux, Python version incompatibilities on self-hosted runners, and architecture string mismatches (e.g., `win64_msvc2019_64` vs `win64_msvc2022_64`). The action also has version-specific quirks with older Qt versions.

**Warning signs:**
- CI hangs for 10+ minutes with no output during Qt installation
- Intermittent failures: "Connection reset" or timeout errors
- Wrong Qt version installed (architecture string mismatch)
- `setup-python` failures on self-hosted runners

**Prevention:**
1. **Always enable caching** in install-qt-action:
   ```yaml
   - uses: jurplel/install-qt-action@v4
     with:
       version: '6.8.0'
       cache: true
       cache-key-prefix: qt-${{ matrix.qt_version }}
   ```
2. **Use the `archives` parameter** to install only needed modules (Core, Network, WebSockets), reducing download size and failure surface
3. **Pin aqtinstall version** to avoid regressions: `aqtversion: ==3.2.1`
4. **Add retry logic** around the install step:
   ```yaml
   - uses: nick-fields/retry@v2
     with:
       max_attempts: 3
       timeout_minutes: 15
       command: aqtinstall ...
   ```
5. **For the custom-patched Qt 5.15.1:** aqtinstall CANNOT install this. It must come from a cache or pre-built artifact
6. **Pin runner images:** Use `windows-2022` and `ubuntu-22.04`, not `-latest`
7. **Consider Docker containers** with pre-installed Qt for Linux builds to eliminate aqtinstall entirely

**Confidence:** HIGH -- [aqtinstall hang issue #91](https://github.com/miurahr/aqtinstall/issues/91), [install-qt-action failures](https://github.com/jurplel/install-qt-action/issues/70), [KDAB Qt CI guide](https://www.kdab.com/github-actions-for-cpp-and-qt/)

**Phase impact:** CI setup phase. A flaky CI blocks all releases.

---

### Pitfall 9: GitHub Release Changelog Does Not Map to Qt-Version-Specific Changes

**What goes wrong:** The auto-generated changelog lists all commits/PRs since the last release, but does not indicate which changes affect which Qt versions. A user on Qt 5.15 cannot tell if a release fixes their issue or only affects Qt 6.x. Worse, the release notes do not clarify which probe binaries in the release assets correspond to which Qt versions.

**Why it happens:** GitHub's auto-generated release notes are commit-based, not artifact-based. They have no concept of "this binary is for Qt 6.8 on Windows." When release assets are named `qtPilot-probe-qt6.8.0-msvc2022-x64-release.dll`, the connection between changelog entries and artifacts is lost.

**Warning signs:**
- Users download the wrong probe binary for their Qt version
- Support issues: "I downloaded the release but it crashes" (wrong Qt version binary)
- Changelog is a wall of text with no Qt-version context

**Prevention:**
1. **Structure release notes by Qt version:**
   ```markdown
   ## Probe Binaries
   | File | Qt Version | Platform | Config |
   |------|-----------|----------|--------|
   | qtPilot-probe-qt5.15-... | Qt 5.15.x | Linux GCC | Release |
   | qtPilot-probe-qt6.8-... | Qt 6.8.x | Windows MSVC 2022 | Release |
   ```
2. **Use GitHub's `.github/release.yml`** to categorize PRs by label (e.g., `qt5`, `qt6`, `ci`, `python`)
3. **Automate the asset table** in CI: after all matrix builds complete, generate a markdown table of all artifacts and inject it into release notes
4. **Name assets clearly:** Include Qt version, platform, compiler, and build type in every filename
5. **Add a "Which binary do I need?" section** to release notes with a decision tree

**Confidence:** MEDIUM -- based on analysis of typical Qt project releases and GitHub's [auto-generated release notes docs](https://docs.github.com/en/repositories/releasing-projects-on-github/automatically-generated-release-notes)

**Phase impact:** Release automation phase.

---

### Pitfall 10: vcpkg Binary Caching Invalidated by Runner Toolchain Updates

**What goes wrong:** vcpkg binary caching works perfectly for a week, then suddenly all packages rebuild from source. CI times jump from 5 minutes to 45 minutes. The cache hit rate drops to 0%.

**Why it happens:** vcpkg computes an ABI hash that includes the compiler version. GitHub Actions runners auto-update Visual Studio and GCC. Even a minor version bump (e.g., MSVC 19.41 to 19.42) changes the ABI hash, invalidating the entire binary cache.

**Warning signs:**
- CI suddenly takes much longer without any project changes
- vcpkg reports "rebuilding" packages that were previously cached
- Cache keys in GitHub Actions cache change unexpectedly
- This happens after GitHub runner image updates (typically monthly)

**Prevention:**
1. **Use `--x-abi-tools-use-exact-versions`** in vcpkg invocations to pin tool versions
2. **Pin runner images:** `windows-2022` not `windows-latest`, or use specific image versions
3. **Disable automatic VS updates** in CI or use a specific VS component version
4. **Use vcpkg binary caching with a remote store** (GitHub Actions cache, Azure Blob, or NuGet feed) so rebuilds are shared across runs
5. **Monitor cache hit rates** and alert on sudden drops
6. **For Qt (if building via vcpkg):** A cache miss on Qt alone costs 30+ minutes

**Confidence:** HIGH -- [vcpkg binary caching troubleshooting](https://learn.microsoft.com/en-us/vcpkg/users/binarycaching-troubleshooting)

**Phase impact:** CI optimization phase. Not a blocker but a major cost/time issue.

---

## Minor Pitfalls

Mistakes that cause annoyance but are quickly fixable.

---

### Pitfall 11: PyPI Package Name Collision

**What goes wrong:** The name `qtpilot` may already be taken on PyPI, or a similar name exists causing user confusion.

**Prevention:**
1. **Check PyPI name availability early:** `pip install qtpilot` -- if it installs something, the name is taken
2. **Register the name by publishing a placeholder** (version 0.0.1) early
3. **Consider `qt-mcp` or `qtpilot-server`** as alternatives if `qtpilot` is taken

**Confidence:** LOW -- not verified whether `qtpilot` is available on PyPI

**Phase impact:** Python packaging phase. Trivial to fix but embarrassing if discovered late.

---

### Pitfall 12: Qt Version Detection Breaks with Versionless Targets

**What goes wrong:** CMakeLists.txt uses versionless targets (`Qt::Core`) for build simplicity, but the CMake config exported to consumers hardcodes `Qt6::Core` or `Qt5::Core`. Consumers using a different Qt major version get confusing "target not found" errors.

**Why it happens:** Versionless targets are aliases resolved at configure time. The exported targets file records the resolved (versioned) name. If the consumer has a different Qt version, the dependency chain breaks.

**Warning signs:**
- Consumer gets "Qt6::Core not found" when they have Qt5
- Export file contains hardcoded version-specific target names
- Works when consumer has same Qt major version, fails otherwise

**Prevention:**
1. **This is actually correct behavior for qtPilot** -- the probe MUST match the Qt version. The export SHOULD fail if Qt versions differ
2. **Make the error message clear:** Add a check in `qtPilotConfig.cmake.in` that prints "qtPilot requires Qt6. You have Qt5. Please install the Qt5 build of qtPilot."
3. **Ship separate CMake packages per Qt major version** (e.g., `find_package(qtPilot-Qt6)`)

**Confidence:** HIGH -- verified against [Qt 5 and Qt 6 compatibility docs](https://doc.qt.io/qt-6/cmake-qt5-and-qt6-compatibility.html)

**Phase impact:** CMake packaging phase. Quick fix once identified.

---

### Pitfall 13: `actions/cache@v3` Sunset Breaks CI

**What goes wrong:** CI workflows using `actions/cache@v3` stop working after GitHub sunsets the legacy cache service.

**Why it happens:** GitHub deprecated the legacy cache service on February 1, 2025. Workflows using `actions/cache` below v4 or runners below version 2.231.0 fail silently or explicitly.

**Prevention:**
1. **Use `actions/cache@v4`** in all workflows
2. **Ensure self-hosted runners (if any) are version 2.231.0+**
3. **Use `hashFiles()` for cache keys** to avoid stale caches

**Confidence:** HIGH -- [GitHub cache documentation](https://github.com/actions/cache)

**Phase impact:** CI setup phase. One-line fix but blocks CI if missed.

---

### Pitfall 14: Git Tag Format Inconsistency Breaks Release Automation

**What goes wrong:** Some tags are `v1.0.0`, others are `1.0.0`, others are `release/1.0.0`. Release automation (semantic-release, custom scripts) fails to detect the latest version or creates duplicate releases.

**Prevention:**
1. **Pick one format and enforce it:** `v{MAJOR}.{MINOR}.{PATCH}` (e.g., `v1.0.0`)
2. **Use annotated tags** (`git tag -a v1.0.0 -m "Release 1.0.0"`), not lightweight tags
3. **Add a CI check** that validates tag format before creating a release
4. **Configure release automation** to only trigger on tags matching the pattern: `on: push: tags: ['v*.*.*']`

**Confidence:** HIGH -- universal Git/CI best practice

**Phase impact:** Release automation phase. Trivial to establish, painful to fix retroactively.

---

### Pitfall 15: Python Package Declares Wrong Dependency Bounds

**What goes wrong:** `pyproject.toml` declares `fastmcp>=2.0,<3` but a new fastmcp 2.x release introduces a breaking change in a minor version. Users who `pip install qtpilot` get a broken installation.

**Why it happens:** Semantic versioning is aspirational, not guaranteed, for third-party packages. `fastmcp` is relatively new (the current bound `>=2.0,<3` is broad). The `websockets>=14.0` bound is similarly open-ended.

**Prevention:**
1. **Test against both minimum and latest dependencies in CI:**
   ```yaml
   - name: Test minimum deps
     run: pip install "fastmcp==2.0" "websockets==14.0" && pytest
   - name: Test latest deps
     run: pip install "fastmcp>=2.0" "websockets>=14.0" && pytest
   ```
2. **Use a lockfile** (`pip-compile` or `uv lock`) for reproducible CI
3. **Pin upper bounds more tightly after testing:** e.g., `fastmcp>=2.0,<2.5`
4. **Run weekly CI against latest deps** to catch breakage early

**Confidence:** MEDIUM -- based on current `pyproject.toml` analysis and general Python packaging experience

**Phase impact:** Python packaging phase. Ongoing maintenance concern.

---

## Phase-Specific Warning Summary

| Phase | Topic | Likely Pitfall | Severity | Mitigation |
|-------|-------|---------------|----------|------------|
| vcpkg Port | Qt dependency | Port builds its own Qt (#1) | CRITICAL | External Qt via CMAKE_PREFIX_PATH |
| vcpkg Port | CMake exports | Missing Qt version check (#7) | CRITICAL | Version check in Config.cmake |
| vcpkg Port | Binary cache | Cache invalidation (#10) | MODERATE | Pin toolchain, use --x-abi-tools |
| CI Setup | Matrix builds | Artifact name collision (#3) | CRITICAL | Matrix vars in artifact names |
| CI Setup | Qt installation | aqtinstall hangs (#8) | MODERATE | Caching, retries, Docker fallback |
| CI Setup | Custom Qt | Patched Qt reproduction (#4) | CRITICAL | Pre-built cache, pinned recipe |
| CI Setup | Runner updates | Cache invalidation (#10, #13) | MODERATE | Pin runners, cache@v4 |
| Release Automation | Version sync | Multi-file version drift (#6) | MODERATE | Single source of truth + CI check |
| Release Automation | Tagging | Tag format inconsistency (#14) | MINOR | Enforce v{semver} pattern |
| Release Automation | Changelog | No Qt-version mapping (#9) | MODERATE | Structured release notes |
| Release Automation | ABI matrix | Incomplete binary set (#2) | CRITICAL | Explicit matrix, completeness check |
| Python / PyPI | Probe binaries | Probe not included (#5) | MODERATE | Doctor command, clear docs |
| Python / PyPI | Naming | Name collision (#11) | MINOR | Check and reserve early |
| Python / PyPI | Dependencies | Bounds too broad (#15) | MINOR | Min/max testing in CI |
| CMake Packaging | Targets | Versionless target confusion (#12) | MINOR | Per-Qt-version package names |

---

## Sources

### Authoritative (HIGH confidence)
- [vcpkg: Using external Qt](https://github.com/microsoft/vcpkg/discussions/46814)
- [vcpkg: Pre-installed Qt issue](https://github.com/microsoft/vcpkg/issues/27574)
- [vcpkg Binary Caching Troubleshooting](https://learn.microsoft.com/en-us/vcpkg/users/binarycaching-troubleshooting)
- [vcpkg Maintainer Guide](https://learn.microsoft.com/en-us/vcpkg/contributing/maintainer-guide)
- [GitHub upload-artifact v4](https://github.com/actions/upload-artifact)
- [upload-artifact overwrite race bug](https://github.com/actions/upload-artifact/issues/506)
- [GitHub actions/cache v4](https://github.com/actions/cache)
- [Qt 5 and Qt 6 CMake Compatibility](https://doc.qt.io/qt-6/cmake-qt5-and-qt6-compatibility.html)
- [Qt CMake Policies](https://doc.qt.io/qt-6/qt-cmake-policies.html)
- [Python Packaging: Binary Distribution Format](https://packaging.python.org/en/latest/specifications/binary-distribution-format/)
- [GitHub Auto-generated Release Notes](https://docs.github.com/en/repositories/releasing-projects-on-github/automatically-generated-release-notes)

### Community/Analysis (MEDIUM confidence)
- [GammaRay Probe ABI Roadmap](https://github.com/KDAB/GammaRay/wiki/Roadmap)
- [KDAB: GitHub Actions for C++ and Qt](https://www.kdab.com/github-actions-for-cpp-and-qt/)
- [Qt Forum: CI Best Practices](https://forum.qt.io/topic/161356/github-actions-ci-best-known-methods-to-support-qt-applications)
- [install-qt-action](https://github.com/jurplel/install-qt-action)
- [aqtinstall hang issue](https://github.com/miurahr/aqtinstall/issues/91)
- [Docker-based Qt6 CI](https://dev.to/accvcc/simplifying-qt6-ci-on-github-actions-with-a-docker-based-cmake-build-environment-33bp)
- [Qt Forum: ABI mismatch](https://forum.qt.io/topic/156047/qt-6-7-0-statically-built-with-llvm-mingw1706_64-has-different-abi-from-compiler)
- [KDAB: Un-deprecate your Qt project](https://www.kdab.com/un-deprecate-qt-project/)
