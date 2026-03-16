# Phase 10: Patched Qt 5.15.1 CI - Research

**Researched:** 2026-02-02
**Domain:** CI build-from-source Qt 5.15.1, GitHub Actions caching, composite actions
**Confidence:** MEDIUM (verified against official Qt docs and GitHub docs; build timing is estimated)

## Summary

This phase requires building Qt 5.15.1 from source in GitHub Actions CI, applying custom patches to the extracted tarball, caching the built Qt aggressively, and then building the qtPilot probe against it. The research covers: exact download URLs, minimal configure flags for the probe's dependencies, patch application on non-git source trees, GitHub Actions caching for 1-3 GB artifacts, composite action design, and critical platform differences between Linux and Windows builds.

The probe links against Qt::Core, Qt::Network, Qt::WebSockets, Qt::Widgets, Qt::Test, and Qt::CorePrivate (for qhooks_p.h). A minimal Qt 5.15.1 build targeting only these modules (plus qtdeclarative for optional QML) can skip ~25 of the ~35 top-level modules, reducing build time from ~2 hours to an estimated 20-40 minutes on GitHub Actions runners.

**Primary recommendation:** Build only qtbase + qtwebsockets + qtdeclarative (optional) + qttools (for qdoc/lupdate, optional), skip everything else. Use `patch -p1` for applying git-format patches to the extracted tarball. Cache the installed Qt prefix directory with a key based on `hashFiles('.ci/patches/5.15.1/**')` + runner OS + a configure-flags hash constant.

## Standard Stack

### Core

| Tool | Version | Purpose | Why Standard |
|------|---------|---------|--------------|
| Qt source tarball | 5.15.1 | Source to build from | Official archive from download.qt.io |
| actions/cache | v4 | Cache built Qt between runs | Standard GH Actions caching, now supports >10 GB |
| patch (POSIX) | system | Apply .patch files to tarball source | Works on non-git directories; available on all runners |
| jom | latest stable | Parallel make on Windows | Qt project tool; clone of nmake with `-j` support |
| make | system | Build on Linux | Standard; use `-j$(nproc)` for parallelism |
| ilammy/msvc-dev-cmd | v1 | Set up MSVC dev environment | Required for configure.bat + nmake/jom on Windows |
| Perl | 5.12+ | Required by Qt configure | Qt build dependency; pre-installed on GH runners |
| Python | 2.7+ or 3.x | Required by Qt configure | Qt build dependency; pre-installed on GH runners |

### Supporting

| Tool | Version | Purpose | When to Use |
|------|---------|---------|-------------|
| actions/upload-artifact | v4 | Upload probe binaries | After successful probe build |
| hashFiles() | built-in | Generate cache keys from patch files | Cache key computation |

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| POSIX `patch` | `git apply` | `git apply` requires initializing a git repo in the extracted tarball; `patch -p1` is simpler |
| jom | nmake with `/MP` in CL env | jom is faster; nmake `/MP` only parallelizes within a single translation unit |
| Separate workflow | Matrix include entry | Separate workflow provides isolated triggers, timeouts, and failure isolation |

**Qt Source Download:**
```bash
# Linux (tar.xz, 554 MB)
wget https://download.qt.io/archive/qt/5.15/5.15.1/single/qt-everywhere-src-5.15.1.tar.xz

# Windows (zip, 910 MB)
curl -L -o qt-everywhere-src-5.15.1.zip https://download.qt.io/archive/qt/5.15/5.15.1/single/qt-everywhere-src-5.15.1.zip
```

## Architecture Patterns

### Recommended Project Structure

```
.github/
  actions/
    build-qt/
      action.yml           # Reusable composite action
  workflows/
    ci.yml                 # Existing matrix CI (Phase 9, unchanged)
    ci-patched-qt.yml      # NEW: patched Qt 5.15.1 workflow
.ci/
  patches/
    5.15.1/
      001-fix-something.patch
      002-another-fix.patch
      README.md            # Describes each patch's purpose
```

### Pattern 1: Composite Action for Building Qt from Source

**What:** A reusable composite action at `.github/actions/build-qt/` that downloads, patches, configures, builds, and installs Qt. It checks for a cache hit first and skips the build if the cache is warm.

**When to use:** Both the CI workflow (Phase 10) and release workflow (Phase 11) need to build patched Qt.

**Interface design:**

```yaml
# .github/actions/build-qt/action.yml
name: 'Build Patched Qt from Source'
description: 'Downloads, patches, configures, builds, and caches Qt from source'
inputs:
  qt-version:
    description: 'Qt version to build (e.g., 5.15.1)'
    required: true
  patches-dir:
    description: 'Directory containing .patch files (e.g., .ci/patches/5.15.1)'
    required: true
  install-prefix:
    description: 'Qt installation prefix path'
    required: true
    default: '${{ github.workspace }}/qt-install'
  configure-args:
    description: 'Additional configure arguments (beyond the defaults)'
    required: false
    default: ''

outputs:
  qt-dir:
    description: 'Path to the installed Qt directory (for CMAKE_PREFIX_PATH)'
    value: ${{ steps.setup.outputs.qt-dir }}
  cache-hit:
    description: 'Whether the Qt cache was hit'
    value: ${{ steps.cache.outputs.cache-hit }}
```

**Steps within composite action:**
1. Compute cache key from `hashFiles(patches-dir + '/**')` + runner.os + configure args hash
2. Restore cache using `actions/cache@v4`
3. If cache miss:
   a. Download Qt source tarball
   b. Extract tarball
   c. Apply patches with `patch -p1`
   d. Run configure
   e. Build with make/jom
   f. Install to prefix
4. Set output `qt-dir` to the install prefix

### Pattern 2: Platform-Specific Configure Commands

**What:** Qt 5.15.1 uses completely different configure invocations on Linux vs Windows.

**Linux configure:**
```bash
cd qt-everywhere-src-5.15.1
./configure \
  -prefix /path/to/install \
  -opensource -confirm-license \
  -shared \
  -nomake examples \
  -nomake tests \
  -skip qt3d \
  -skip qtactiveqt \
  -skip qtandroidextras \
  -skip qtcanvas3d \
  -skip qtcharts \
  -skip qtconnectivity \
  -skip qtdatavis3d \
  -skip qtdoc \
  -skip qtgamepad \
  -skip qtgraphicaleffects \
  -skip qtimageformats \
  -skip qtlocation \
  -skip qtlottie \
  -skip qtmacextras \
  -skip qtmultimedia \
  -skip qtnetworkauth \
  -skip qtpurchasing \
  -skip qtquick3d \
  -skip qtquickcontrols \
  -skip qtquickcontrols2 \
  -skip qtquicktimeline \
  -skip qtremoteobjects \
  -skip qtscript \
  -skip qtscxml \
  -skip qtsensors \
  -skip qtserialbus \
  -skip qtserialport \
  -skip qtspeech \
  -skip qtsvg \
  -skip qtvirtualkeyboard \
  -skip qtwayland \
  -skip qtwebchannel \
  -skip qtwebengine \
  -skip qtwebglplugin \
  -skip qtwebview \
  -skip qtx11extras \
  -skip qtxmlpatterns \
  -no-dbus \
  -no-icu \
  -qt-pcre \
  -qt-doubleconversion \
  -qt-harfbuzz \
  -xcb \
  -release
```

**Windows configure (in MSVC dev prompt):**
```cmd
configure.bat ^
  -prefix C:\path\to\install ^
  -opensource -confirm-license ^
  -shared ^
  -nomake examples ^
  -nomake tests ^
  -skip qt3d ^
  -skip qtactiveqt ^
  -skip qtandroidextras ^
  -skip qtcanvas3d ^
  -skip qtcharts ^
  -skip qtconnectivity ^
  -skip qtdatavis3d ^
  -skip qtdoc ^
  -skip qtgamepad ^
  -skip qtgraphicaleffects ^
  -skip qtimageformats ^
  -skip qtlocation ^
  -skip qtlottie ^
  -skip qtmacextras ^
  -skip qtmultimedia ^
  -skip qtnetworkauth ^
  -skip qtpurchasing ^
  -skip qtquick3d ^
  -skip qtquickcontrols ^
  -skip qtquickcontrols2 ^
  -skip qtquicktimeline ^
  -skip qtremoteobjects ^
  -skip qtscript ^
  -skip qtscxml ^
  -skip qtsensors ^
  -skip qtserialbus ^
  -skip qtserialport ^
  -skip qtspeech ^
  -skip qtsvg ^
  -skip qtvirtualkeyboard ^
  -skip qtwayland ^
  -skip qtwebchannel ^
  -skip qtwebengine ^
  -skip qtwebglplugin ^
  -skip qtwebview ^
  -skip qtx11extras ^
  -skip qtxmlpatterns ^
  -no-dbus ^
  -no-icu ^
  -qt-pcre ^
  -qt-doubleconversion ^
  -qt-harfbuzz ^
  -opengl desktop ^
  -platform win32-msvc ^
  -mp ^
  -release
```

**Modules kept (NOT skipped):**
- `qtbase` (Core, Network, Widgets, Test, CorePrivate) - REQUIRED, cannot be skipped
- `qtwebsockets` - REQUIRED by probe
- `qtdeclarative` - OPTIONAL, needed for Qt::Qml/Qt::Quick support
- `qttools` - OPTIONAL, provides useful dev tools; can be skipped for minimal build

### Pattern 3: Patch Application Workflow

**What:** Apply `.patch` files in sorted order to the extracted Qt source tree.

**Linux:**
```bash
# Extract
tar -xf qt-everywhere-src-5.15.1.tar.xz
cd qt-everywhere-src-5.15.1

# Apply all patches in sorted order
for patch_file in $(ls -1 ../patches/*.patch | sort); do
  echo "Applying patch: $patch_file"
  patch -p1 --verbose < "$patch_file"
  if [ $? -ne 0 ]; then
    echo "ERROR: Patch failed: $patch_file"
    exit 1
  fi
done
```

**Windows (PowerShell):**
```powershell
# Windows needs Git's patch or a standalone patch.exe
# Git for Windows includes patch in usr/bin
$patches = Get-ChildItem -Path ..\patches\*.patch | Sort-Object Name
foreach ($p in $patches) {
    Write-Host "Applying patch: $($p.Name)"
    git apply --verbose $p.FullName
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Patch failed: $($p.Name)"
        exit 1
    }
}
```

**Key detail for Windows:** The POSIX `patch` command is not natively available. Options:
1. Use `git apply` (available since Git for Windows is pre-installed on GH runners) -- works on non-git directories with `--unsafe-paths` or by initializing a temporary git repo
2. Use `git init && git apply` in the extracted source dir
3. Install patch.exe via chocolatey or MSYS2

**Recommendation:** Use `git apply --directory=<src-dir>` on both platforms for consistency. Initialize a temp git repo in the source dir: `git init && git add -A && git commit -m "base" && git apply ../patches/*.patch`. This is more reliable cross-platform.

**Revised cross-platform approach:**
```bash
# Works identically on Linux and Windows (Git is available on both)
cd qt-everywhere-src-5.15.1
git init
git add -A
git commit -m "Qt 5.15.1 base"
for patch_file in $(ls -1 ${PATCHES_DIR}/*.patch | sort); do
  git apply --verbose "$patch_file" || { echo "Patch failed: $patch_file"; exit 1; }
done
```

### Anti-Patterns to Avoid

- **Building all modules:** Skipping nothing results in 2+ hour builds. Always use `-skip` aggressively.
- **Using `-static` for probe:** The probe MUST be a shared library (it's injected into target processes). Always use `-shared`.
- **Caching the entire build tree:** Cache only the installed prefix, not the build directory. The build tree is 5-15 GB; the install prefix is 200-500 MB.
- **Running on PRs:** Cold-cache builds take 30-60 min. Limit to push-to-main to conserve CI minutes.
- **Using `windows-latest` directly:** Pin to `windows-2022` for now since Qt 5.15.1 is only officially supported with MSVC 2019 (v142). The `windows-latest` label now points to Windows Server 2025 with VS 2022/2026, which may have MSVC compatibility issues.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Parallel make on Windows | Custom parallelism scripts | jom | Qt-project tool, drop-in nmake replacement with `-j` |
| MSVC environment setup | Manual vcvarsall.bat parsing | `ilammy/msvc-dev-cmd@v1` | Handles all VS versions, toolset pinning, arch selection |
| Cache key computation | Manual SHA256 of files | `hashFiles()` built-in | GitHub-native, handles globs, multiple patterns |
| Qt source download+extract | Custom curl/tar scripts | Inline steps (curl + tar) | Simple enough to inline; no action needed |
| Patch application | Custom diff parsing | `git apply` or `patch -p1` | Battle-tested tools, handle all edge cases |

**Key insight:** The complexity in this phase is in the configure flags and cache strategy, not in the tooling. Use standard tools and focus effort on getting the configure flags and cache key design right.

## Common Pitfalls

### Pitfall 1: MSVC Version Incompatibility

**What goes wrong:** Qt 5.15.1 configure fails or produces broken builds with MSVC versions newer than v142 (VS 2019).
**Why it happens:** Qt 5.15.1 was released in Sept 2020 and only officially supports MSVC 2015/2017/2019. The `msvc-version.conf` file in qtbase may not recognize newer MSVC versions, causing `QMAKE_MSC_VER` to not be set.
**How to avoid:** Pin the MSVC toolset to v142 using `ilammy/msvc-dev-cmd@v1` with `toolset: '14.29'` (or `vsversion: '2022'` with `toolset: '14.2'`). Use `windows-2022` runner (not `windows-latest`) to ensure VS 2022 is available with v142 build tools installed as an individual component.
**Warning signs:** Configure output shows "msvc-version.conf loaded but QMAKE_MSC_VER isn't set" error.

### Pitfall 2: Missing XCB Dependencies on Linux

**What goes wrong:** Qt configure completes but the xcb platform plugin is not built, resulting in a Qt installation that cannot create GUI windows.
**Why it happens:** Qt's configure silently skips building the xcb platform plugin when xcb dev packages are missing. There is no error during configure.
**How to avoid:** Install the full set of xcb dev packages before configure. Check configure output for "XCB: yes" confirmation.
**Warning signs:** `config.summary` shows `xcb: no` or the probe fails at runtime with "Could not find the Qt platform plugin xcb".

**Required Linux packages (Ubuntu 22.04):**
```bash
sudo apt-get install -y \
  build-essential \
  perl \
  python3 \
  libgl1-mesa-dev \
  libglu1-mesa-dev \
  libfontconfig1-dev \
  libfreetype6-dev \
  libx11-dev \
  libx11-xcb-dev \
  libxext-dev \
  libxfixes-dev \
  libxi-dev \
  libxrender-dev \
  libxcb1-dev \
  libxcb-glx0-dev \
  libxcb-keysyms1-dev \
  libxcb-image0-dev \
  libxcb-shm0-dev \
  libxcb-icccm4-dev \
  libxcb-sync-dev \
  libxcb-xfixes0-dev \
  libxcb-shape0-dev \
  libxcb-randr0-dev \
  libxcb-render-util0-dev \
  libxcb-xinerama0-dev \
  libxcb-xkb-dev \
  libxcb-cursor0 \
  libxkbcommon-dev \
  libxkbcommon-x11-dev \
  libxtst-dev
```

### Pitfall 3: Cache Key Staleness / Collisions

**What goes wrong:** Cache is restored but contains a stale build that doesn't match current patches or configure flags.
**Why it happens:** Cache key doesn't include all inputs that affect the build output.
**How to avoid:** Include ALL of: (1) hash of all patch files, (2) runner OS, (3) a hash/version of the configure flags string, (4) Qt version string.
**Warning signs:** Probe builds succeed but behavior doesn't match expected patches.

**Recommended cache key:**
```yaml
key: qt-${{ inputs.qt-version }}-${{ runner.os }}-${{ hashFiles('.ci/patches/5.15.1/**') }}-${{ env.CONFIGURE_FLAGS_HASH }}
restore-keys: |
  # NO restore-keys — never use a partial match for a patched Qt build
  # A partial cache hit would restore unpatched or differently-patched Qt
```

### Pitfall 4: Tarball Extraction Path Differences

**What goes wrong:** Patches don't apply because the extracted directory name doesn't match patch paths.
**Why it happens:** tar.xz extracts to `qt-everywhere-src-5.15.1/` but patches may reference different paths.
**How to avoid:** Always `cd` into the extracted source directory and use `patch -p1` (or `git apply`) which strips the leading directory component (`a/`, `b/` prefixes from git-generated patches).
**Warning signs:** "can't find file to patch" errors during patch application.

### Pitfall 5: Private Headers Not Available

**What goes wrong:** Probe build fails because `qhooks_p.h` (from Qt::CorePrivate) is not found.
**Why it happens:** Private headers are installed automatically when building Qt from source with `make install`. This is NOT an issue with source builds -- it only happens with pre-built binary distributions that omit private headers.
**How to avoid:** Verify after `make install` that `include/QtCore/5.15.1/QtCore/private/qhooks_p.h` exists in the install prefix.
**Warning signs:** CMake error "Could not find Qt5CorePrivate" during probe configuration.

### Pitfall 6: Cache Size Exceeds Limits

**What goes wrong:** Cache save fails or evicts other important caches.
**Why it happens:** Caching the entire Qt build tree (5-15 GB) instead of just the install prefix.
**How to avoid:** Cache ONLY the `make install` output directory (the prefix). A minimal Qt 5.15.1 install with the modules listed above should be ~200-500 MB. Even with shared libraries, it should stay under 1 GB.
**Warning signs:** Cache save step takes >5 minutes or reports size warnings.

## Code Examples

### Complete Composite Action

```yaml
# .github/actions/build-qt/action.yml
name: 'Build Patched Qt'
description: 'Build Qt from source with patches, with aggressive caching'

inputs:
  qt-version:
    description: 'Qt version (e.g., 5.15.1)'
    required: true
  patches-dir:
    description: 'Path to patches directory'
    required: true
  install-prefix:
    description: 'Installation prefix'
    required: false
    default: '${{ github.workspace }}/qt-patched'

outputs:
  qt-dir:
    description: 'Qt installation directory'
    value: ${{ steps.paths.outputs.qt-dir }}
  cache-hit:
    description: 'Whether cache was hit'
    value: ${{ steps.qt-cache.outputs.cache-hit }}

runs:
  using: 'composite'
  steps:
    - name: Set paths
      id: paths
      shell: bash
      run: |
        echo "qt-dir=${{ inputs.install-prefix }}" >> $GITHUB_OUTPUT

    - name: Restore Qt cache
      id: qt-cache
      uses: actions/cache@v4
      with:
        path: ${{ inputs.install-prefix }}
        key: qt-${{ inputs.qt-version }}-${{ runner.os }}-patches-${{ hashFiles(format('{0}/**', inputs.patches-dir)) }}

    # All subsequent steps only run on cache miss
    - name: Download Qt source
      if: steps.qt-cache.outputs.cache-hit != 'true'
      shell: bash
      run: |
        QT_VER="${{ inputs.qt-version }}"
        QT_MAJOR_MINOR="${QT_VER%.*}"
        URL="https://download.qt.io/archive/qt/${QT_MAJOR_MINOR}/${QT_VER}/single/qt-everywhere-src-${QT_VER}.tar.xz"
        echo "Downloading: $URL"
        curl -L -o qt-src.tar.xz "$URL"
        tar -xf qt-src.tar.xz
        rm qt-src.tar.xz

    - name: Apply patches
      if: steps.qt-cache.outputs.cache-hit != 'true'
      shell: bash
      run: |
        cd qt-everywhere-src-${{ inputs.qt-version }}
        git init
        git add -A
        git commit -m "base" --quiet
        PATCH_COUNT=0
        for p in $(ls -1 ${{ github.workspace }}/${{ inputs.patches-dir }}/*.patch 2>/dev/null | sort); do
          echo "::group::Applying $(basename $p)"
          git apply --verbose "$p"
          PATCH_COUNT=$((PATCH_COUNT + 1))
          echo "::endgroup::"
        done
        echo "Applied $PATCH_COUNT patch(es)"

    # Configure + build steps are platform-specific (shown in workflow examples below)
```

### Workflow File Structure

```yaml
# .github/workflows/ci-patched-qt.yml
name: 'CI: Patched Qt 5.15.1'

on:
  push:
    branches: [main]
    paths:
      - 'src/**'
      - 'CMakeLists.txt'
      - 'cmake/**'
      - '.ci/patches/**'
      - '.github/actions/build-qt/**'
      - '.github/workflows/ci-patched-qt.yml'
  workflow_dispatch:

jobs:
  build:
    name: "Patched Qt 5.15.1 / ${{ matrix.platform }}"
    runs-on: ${{ matrix.os }}
    timeout-minutes: 120
    strategy:
      fail-fast: false
      matrix:
        include:
          - os: ubuntu-22.04
            platform: linux-gcc
            preset: ci-linux
          - os: windows-2022
            platform: windows-msvc
            preset: ci-windows
    steps:
      # ... (see detailed examples in Architecture Patterns)
```

### Cache Key Design

```yaml
# Precise cache key: changes to ANY of these invalidate the cache
key: patched-qt-${{ inputs.qt-version }}-${{ runner.os }}-${{ hashFiles('.ci/patches/5.15.1/**') }}
# DO NOT use restore-keys -- a partial match would restore wrong Qt
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| `actions/cache@v3` | `actions/cache@v4` | 2024 | Required; v1/v2/v3 deprecated |
| 10 GB repo cache limit | >10 GB cache limit | Nov 2025 | More headroom for large Qt builds |
| `windows-latest` = Windows 2022 | `windows-latest` = Windows 2025 | Sept 2025 | Must pin `windows-2022` for Qt 5.15.1 MSVC v142 compatibility |
| `::set-output` syntax | `$GITHUB_OUTPUT` file | 2022 | Must use new syntax in composite actions |
| `-qt-xcb` configure flag | removed in Qt 5.15 | Qt 5.15 | Must install system xcb packages; use `-bundled-xcb-xinput` if needed |

**Deprecated/outdated:**
- `actions/cache@v3`: Deprecated since early 2025; use v4
- `::set-output` command: Deprecated; use `echo "name=value" >> $GITHUB_OUTPUT`
- `windows-latest` for VS 2022: Now points to Windows 2025; use `windows-2022` explicitly

## Open Questions

1. **Exact build time on GH Actions runners**
   - What we know: A full Qt 5.15 build takes ~2 hours on a workstation; a minimal subset takes ~30 min. GH Actions runners have 2-4 vCPUs.
   - What's unclear: Exact time for our specific minimal module set on a GH Actions runner. Windows MSVC builds are typically slower than Linux GCC builds.
   - Recommendation: The 120-minute timeout provides adequate headroom. Measure actual build times in the first CI run and adjust if needed. Expect ~20-40 min Linux, ~30-60 min Windows.

2. **Qt 5.15.1 with MSVC v143 (VS 2022 default toolset)**
   - What we know: Qt 5.15.1 officially supports up to MSVC 2019 (v142). VS 2022's default v143 toolset may cause `QMAKE_MSC_VER` issues.
   - What's unclear: Whether the `windows-2022` runner still includes v142 build tools by default, or if they must be explicitly installed.
   - Recommendation: Use `ilammy/msvc-dev-cmd@v1` with `toolset: '14.29'` to pin v142. If v142 is not available on the runner, add a step to install it via Visual Studio Build Tools. Alternatively, one of the user's patches may fix the `msvc-version.conf` for newer MSVC versions.

3. **Windows patch application tool**
   - What we know: POSIX `patch` is not natively available on Windows runners. Git for Windows is pre-installed.
   - What's unclear: Whether `git apply` works reliably on all patch formats in a freshly-initialized git repo on an extracted tarball.
   - Recommendation: Use the `git init && git add -A && git commit && git apply` approach. Test with actual patches early. Fallback: install MSYS2 `patch` via chocolatey.

4. **Jom availability on Windows runners**
   - What we know: Jom is not pre-installed on GH Actions Windows runners.
   - What's unclear: Whether jom provides enough speedup over `nmake /MP` to justify the download step.
   - Recommendation: Download jom from `https://download.qt.io/official_releases/jom/jom.zip` in the workflow. The download is small (~500 KB) and the parallel build speedup is significant (2-4x on multi-core runners). Alternatively, use `cmake --build` with Ninja generator instead of qmake+nmake, but this would require Qt 5.15.1 to be built with cmake (which is experimental in Qt 5).

5. **Tarball download on Windows**
   - What we know: The `.tar.xz` format (554 MB) is smaller than `.zip` (910 MB). Windows runners have `tar` available (Windows 10+ ships with tar).
   - What's unclear: Whether the Windows built-in tar handles `.tar.xz` natively or needs 7zip.
   - Recommendation: Use `.tar.xz` on Linux. On Windows, use `.zip` to avoid xz decompression issues, OR install 7zip (pre-installed on GH runners) to extract `.tar.xz`. Test in the first run.

## Sources

### Primary (HIGH confidence)
- [Qt Configure Options (Qt 5.15)](https://doc.qt.io/qt-5/configure-options.html) - configure flags reference
- [Qt for Windows - Building from Source (Qt 5.15)](https://doc.qt.io/qt-5/windows-building.html) - Windows build steps
- [Qt for Linux/X11 - Building from Source (Qt 5.15)](https://doc.qt.io/qt-5/linux-building.html) - Linux build steps
- [Qt 5.15 All Modules](https://doc.qt.io/qt-5/qtmodules.html) - module list
- [Qt download archive](https://download.qt.io/archive/qt/5.15/5.15.1/single/) - source tarball verified available
- [GitHub Actions cache docs](https://docs.github.com/en/actions/using-workflows/caching-dependencies-to-speed-up-workflows) - caching reference
- [GitHub Actions composite action docs](https://docs.github.com/actions/creating-actions/creating-a-composite-action) - composite action structure
- [actions/cache@v4 repository](https://github.com/actions/cache) - cache action reference
- [GitHub Actions runner images](https://github.com/actions/runner-images) - runner image specs

### Secondary (MEDIUM confidence)
- [GitHub Actions cache >10GB announcement (Nov 2025)](https://github.blog/changelog/2025-11-20-github-actions-cache-size-can-now-exceed-10-gb-per-repository/) - expanded cache limits
- [windows-latest migration to Windows 2025](https://github.com/actions/runner-images/issues/12677) - runner migration details
- [ilammy/msvc-dev-cmd](https://github.com/ilammy/msvc-dev-cmd) - MSVC toolset pinning
- [Qt Forum: Build Qt 5.15.2 with VS 2022](https://forum.qt.io/topic/136064/build-qt-5-15-2-from-sources-with-visual-studio-2022) - MSVC compatibility details
- [qt-minimalistic-builds](https://github.com/martinrotter/qt-minimalistic-builds) - reference configure flags
- [Jom - Qt Wiki](https://wiki.qt.io/Jom) - parallel make tool
- [Qt 5.15 configure options gist](https://gist.github.com/h3ssan/d83f5560abc1c9d9200bb3e23b1dc160) - community-documented options

### Tertiary (LOW confidence)
- [Qt Forum: Build time estimates](https://forum.qt.io/topic/119026/how-much-time-will-it-take-to-build-qt-5-15-x-source-package) - build duration anecdotes
- [Linux kernel patch documentation](https://docs.kernel.org/process/applying-patches.html) - patch best practices (applicable to any tarball)

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH - Official Qt docs, verified download URLs, confirmed GH Actions features
- Architecture: MEDIUM - Composite action pattern is well-documented; specific configure flags need validation with actual build
- Pitfalls: MEDIUM - MSVC version issues confirmed by multiple sources; xcb dependency list from official Qt docs; cache key design follows GH Actions best practices
- Build timing: LOW - Extrapolated from community reports on different hardware; actual GH Actions runner timing needs measurement

**Research date:** 2026-02-02
**Valid until:** 2026-03-04 (30 days - Qt 5.15.1 is a frozen release; GH Actions infrastructure may change)
