# Building qtPilot from Source

This guide covers building the qtPilot probe and launcher from source code.

## Prerequisites

### Required

- **CMake 3.16+**
- **Qt 5.15.1+ or Qt 6.5+** with development headers (including private headers).
  On macOS, Qt 6.5+ only — Qt 5.15 is not a supported configuration there, see
  [MACOS.md](MACOS.md)
- **C++17 compiler:**
  - GCC 8+ (Linux)
  - Clang 7+ (Linux/macOS)
  - MSVC 2019+ (Windows)

### Required Qt Modules

- Qt Core (including CorePrivate for private headers)
- Qt Network
- Qt WebSockets
- Qt Widgets
- Qt Test (for building tests)

### Optional

- **Qt Qml/Quick** - Enables QML introspection

## Clone and Build

### Basic Build

```bash
git clone https://github.com/ssss2art/qtPilot.git
cd qtPilot

# Configure (specify Qt path if not in system PATH)
cmake -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.0/gcc_64

# Build
cmake --build build

# Run tests
ctest --test-dir build --output-on-failure
```

### Windows with Visual Studio

```powershell
git clone https://github.com/ssss2art/qtPilot.git
cd qtPilot

# Configure for Visual Studio 2022
cmake -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:\Qt\6.8.0\msvc2022_64"

# Build
cmake --build build --config Release

# Run tests
ctest --test-dir build --output-on-failure -C Release
```

## Using CMake Presets

The project includes predefined presets for common configurations.

### Available Presets

| Preset | Platform | Build Type | Description |
|--------|----------|------------|-------------|
| `debug` | Linux | Debug | Development build with tests |
| `release` | Linux | Release | Optimized Linux build |
| `windows-debug` | Windows | Debug | Development build with tests |
| `windows-release` | Windows | Release | Optimized Windows build |
| `windows-x86-debug` | Windows x86 | Debug | 32-bit development build with tests |
| `windows-x86-release` | Windows x86 | Release | Optimized 32-bit build |
| `macos-debug` | macOS | Debug | Development build with tests |
| `macos-release` | macOS | Release | Optimized macOS build |
| `qt5-release` | Linux | Release | Qt 5.15.x targeted build |
| `qt5-windows-release` | Windows | Release | Qt 5.15.x targeted build |

Presets are platform-conditional -- only the presets for your current OS will appear.

### Using Presets

```bash
# List available presets
cmake --list-presets

# Configure with a preset
cmake --preset release

# Build with the preset
cmake --build --preset release

# Run tests with the preset
ctest --preset release
```

### Custom Qt Path with Presets

Pass `-DCMAKE_PREFIX_PATH` after the preset to point at a custom Qt installation:

```bash
cmake --preset release -DCMAKE_PREFIX_PATH=/opt/Qt/6.7.2/gcc_64
```

For a persistent local override, create a `CMakeUserPresets.json` (not checked in) that inherits from an existing preset:

```json
{
  "version": 6,
  "configurePresets": [
    {
      "name": "my-qt",
      "inherits": "windows-release",
      "cacheVariables": {
        "CMAKE_PREFIX_PATH": "C:/Qt/6.7.2/msvc2022_64"
      }
    }
  ]
}
```

Then use `cmake --preset my-qt`.

## Build Options

Configure these options with `-D<OPTION>=<VALUE>`:

| Option | Default | Description |
|--------|---------|-------------|
| `QTPILOT_BUILD_TESTS` | `ON` (forced `OFF` on mobile) | Build unit tests |
| `QTPILOT_BUILD_TEST_APP` | `ON` (forced `OFF` on mobile) | Build the test Qt application |
| `QTPILOT_PROBE_STATIC` | `OFF` (`ON` on mobile) | Build the probe as a static library to link into an app, instead of a shared library to inject. Not supported on Windows — the probe's Detours dependency is not installed, so the archive cannot be linked by a consumer |
| `QTPILOT_BUILD_BENCHMARKS` | `OFF` | Build complexity micro-benchmarks. Fetches google/benchmark at configure time; see [benchmarks/README.md](../benchmarks/README.md) |
| `QTPILOT_QT_DIR` | - | Explicit path to Qt installation (prepended to `CMAKE_PREFIX_PATH`) |

"Mobile" means an Android or iOS toolchain, detected from CMake's `ANDROID` /
`IOS` variables. Those builds also skip the launcher entirely — it injects into
an already-running process, which neither mobile platform permits.

### Examples

```bash
# Build without tests (faster)
cmake -B build -DQTPILOT_BUILD_TESTS=OFF -DQTPILOT_BUILD_TEST_APP=OFF

# Specify Qt installation directly
cmake -B build -DQTPILOT_QT_DIR=/opt/Qt/6.8.0/gcc_64
```

## Building for Android or iOS

Mobile builds produce a *static* probe you link into your own development build,
because neither platform can inject a library into an existing process. Configure
with the mobile Qt kit's `qt-cmake` wrapper:

```bash
# Android
/path/to/Qt/6.11.1/android_arm64_v8a/bin/qt-cmake -B build-android -S . -DCMAKE_BUILD_TYPE=Release

# iOS (device)
/path/to/Qt/6.11.1/ios/bin/qt-cmake -B build-ios -S . -G Xcode
cmake --build build-ios --config Debug -- -sdk iphoneos
```

The installed `qtPilot::Probe` target carries the whole-archive link option a
static probe needs -- without it the linker discards the object file that registers
the probe's startup hook and the probe silently never starts. Consumers that link
the archive by raw path instead have to force-load it themselves. Full instructions,
including how to reach the probe from a device, are in [MOBILE.md](MOBILE.md).

## Sanitizer builds

```bash
cmake --preset asan-ubsan && cmake --build --preset asan-ubsan && ctest --preset asan-ubsan
cmake --preset tsan       && cmake --build --preset tsan       && ctest --preset tsan
```

Linux and macOS only (MSVC has ASan but no UBSan); `QTPILOT_SANITIZE` takes any
`-fsanitize=` list directly. Default builds are unaffected — the option is empty and
the configure summary reports it.

ASan + UBSan is clean. TSan reports one real race, left unsuppressed on purpose, plus
two artifacts of TSan being unable to see `QRecursiveMutex`, which are suppressed.
Neither runs in CI yet.

For the findings, the evidence behind them, macOS leak checking (a runtime tool, so
deliberately **not** a preset), and the sequencing for adding CI, see
[SANITIZERS.md](SANITIZERS.md).

## Benchmarks## Benchmarks

The probe ships complexity benchmarks for its hot paths — object-ID generation, tree
serialization and ID resolution. They exist to answer "did this change make a hot
path more complex than it was?" mechanically: each case reports an empirically fitted
Big-O rather than a timing.

```bash
cmake -B build -DQTPILOT_BUILD_BENCHMARKS=ON
cmake --build build --target qtPilot_bench_object_id
./build/bin/qtPilot_bench_object_id
```

Off by default, because the target fetches [google/benchmark](https://github.com/google/benchmark)
at configure time — a normal build never touches the network for it, and no CI leg
builds it. Read the `_BigO` and `_RMS` rows; an RMS above ~10% means the fit is not
trustworthy on that machine.

The recorded baseline, what each case covers, and what a future optimization should
make the numbers do are in [benchmarks/README.md](../benchmarks/README.md).

CI runs them too, in a non-blocking `benchmarks` job that writes the fitted Big-O
into the run's step summary. It never fails the workflow — the point is a reviewable
record of the exponent, not a timing gate.

## Build Artifacts

After building, find the artifacts in these locations:

### Probe Library

| Platform | Location |
|----------|----------|
| Windows | `build/lib/Release/qtPilot-probe-qt6.8.dll` |
| Linux | `build/lib/libqtPilot-probe-qt6.8.so` |
| macOS | `build/lib/libqtPilot-probe-qt6.8.dylib` |
| Android / iOS | `build/lib/libqtPilot-probe-qt6.11.a` (static; Xcode adds a per-config subdirectory) |

The probe binary name includes the Qt major.minor version it was built against (e.g. `qt6.8`, `qt5.15`).
Debug builds append `d` (`libqtPilot-probe-qt6.11d.a`).

### Launcher Executable

Not built for Android or iOS.

| Platform | Location |
|----------|----------|
| Windows | `build/bin/Release/qtPilot-launcher.exe` |
| Linux | `build/bin/qtPilot-launcher` |
| macOS | `build/bin/qtPilot-launcher` |

### Test App (if enabled)

| Platform | Location |
|----------|----------|
| Windows | `build/bin/Release/qtPilot-test-app.exe` |
| Linux | `build/bin/qtPilot-test-app` |
| macOS | `build/bin/qtPilot-test-app` |

## Running Tests

```bash
# Run all tests
ctest --test-dir build --output-on-failure

# Run specific test
ctest --test-dir build -R "test_object_registry" --output-on-failure

# Verbose output
ctest --test-dir build -V
```

On Windows with multi-config generators:
```powershell
ctest --test-dir build --output-on-failure -C Release
```

### Running Admin Tests

The `test_admin_injection` test verifies probe injection into elevated processes. It auto-skips when not running as administrator.

```bash
# Run only admin tests (from elevated terminal)
ctest --test-dir build -C Release -L admin --output-on-failure

# Exclude admin tests
ctest --test-dir build -C Release -LE admin --output-on-failure
```

## Installation

```bash
# Install to default prefix (/usr/local on Linux)
cmake --install build

# Install to custom prefix
cmake --install build --prefix /path/to/install

# Install specific configuration (Windows)
cmake --install build --config Release --prefix C:\qtPilot
```

Installation layout:
```
<prefix>/
├── bin/
│   └── qtPilot-launcher(.exe)
├── lib/
│   ├── qtPilot-probe-qt6.8.dll  (Windows)
│   └── libqtPilot-probe-qt6.8.so (Linux)
├── include/qtpilot/
│   └── (header files)
└── share/cmake/qtPilot/
    └── (CMake package files)
```

## Qt Version Notes

### Qt 5.15

Qt 5.15 requires private headers for probe functionality. Ensure your Qt installation includes them:

```bash
# Check for private headers
ls /path/to/Qt/5.15.2/gcc_64/include/QtCore/5.15.2/QtCore/private/
# Should contain qhooks_p.h
```

### Qt 6.x

Qt 6.5+ is fully supported. Qt 6.x changed some internal APIs, but qtPilot handles this transparently via compatibility helpers in `src/compat/`.

### Building for Multiple Qt Versions

To create probes for different Qt versions, configure and build separately:

```bash
# Build for Qt 6.8
cmake -B build-qt6 -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.0/gcc_64
cmake --build build-qt6

# Build for Qt 5.15
cmake -B build-qt5 -DCMAKE_PREFIX_PATH=/path/to/Qt/5.15.2/gcc_64
cmake --build build-qt5
```

### Building for Qt 5.15.1

Qt 5.15 is supported on **Windows and Linux**. It is not supported on macOS: that
combination is not covered by CI and cannot practically be, since open-source
Qt 5 ended at 5.15.2 and its macOS build is x86_64-only. See
[MACOS.md](MACOS.md#qt-version-support).

Use the dedicated Qt 5 presets to build against Qt 5.15.1 locally:

**Windows:**
```powershell
cmake --preset qt5-windows-release -DQTPILOT_QT_DIR="C:\Qt\5.15.1\msvc2019_64"
cmake --build --preset qt5-windows-release
ctest --preset qt5-windows-release
```

**Linux:**
```bash
cmake --preset qt5-release -DQTPILOT_QT_DIR=/opt/Qt/5.15.1/gcc_64
cmake --build --preset qt5-release
ctest --preset qt5-release
```

Qt 5.15.1 is available from the [Qt Archive](https://download.qt.io/archive/qt/5.15/5.15.1/) or via [aqtinstall](https://github.com/miurahr/aqtinstall):
```bash
pip install aqtinstall
aqt install-qt linux desktop 5.15.1
```

## Troubleshooting Build Issues

### "Qt6::CorePrivate not found"

Your Qt installation is missing private development headers. Solutions:

1. **apt (Ubuntu/Debian):** `sudo apt install qt6-base-private-dev`
2. **aqt (online installer):** Ensure you installed with `--modules all`
3. **Manual:** Check that `<qt-install>/include/QtCore/<version>/QtCore/private/` exists

### "Could not find Qt"

Set the Qt path explicitly:

```bash
cmake -B build -DQTPILOT_QT_DIR=/path/to/Qt/6.8.0/gcc_64
# or
cmake -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.0/gcc_64
```

### Compiler Version Errors

qtPilot requires C++17. Check your compiler version:

```bash
g++ --version   # Need 8+
clang++ --version  # Need 7+
```

On Windows, Visual Studio 2019 or later is required.

## IDE Setup

### Visual Studio Code

1. Install CMake Tools extension
2. Open the qtPilot folder
3. Select a configure preset or set `CMAKE_PREFIX_PATH` in settings.json
4. Build and debug from the CMake panel

### CLion

1. Open the CMakeLists.txt as a project
2. Set `CMAKE_PREFIX_PATH` in CMake options: `-DCMAKE_PREFIX_PATH=/path/to/Qt`
3. Select build configuration and build

### Visual Studio

1. Open folder or CMakeLists.txt
2. Configure Qt path in CMakeSettings.json
3. Build from the Build menu

## Next Steps

- [Getting Started](GETTING-STARTED.md) - Use the probe with your application
- [Troubleshooting](TROUBLESHOOTING.md) - Common runtime issues
