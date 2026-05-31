---
name: notarize
description: Build, sign, notarize, and staple qtPilot macOS binaries for distribution. Creates a release-ready .tar.gz archive.
argument-hint: [qt-version]
allowed-tools: Bash(*), Read, Glob, Grep, Edit, AskUserQuestion
---

# macOS Build, Sign, Notarize & Package

Build qtPilot for macOS, sign with Developer ID, notarize with Apple, staple the ticket, and produce a release-ready archive.

All commands run from the project root directory (where this repo is checked out).

## Arguments

- `$ARGUMENTS` — Qt version to build against (e.g., "6.10"). Defaults to whatever Qt is configured in the existing build.

## Prerequisites

Before first use, the user must have:

1. **Developer ID Application certificate** installed in keychain
   - Create at developer.apple.com > Certificates > Developer ID Application
   - Download and double-click to install in Keychain Access

2. **Notarytool keychain profile** named `qtpilot-notary`
   - One-time setup: `xcrun notarytool store-credentials qtpilot-notary`
   - Enter: Apple ID, Team ID, and app-specific password from appleid.apple.com

## Steps

Execute each step sequentially. Stop and report clearly if any step fails.
Use the project root as the working directory for all commands.

### Step 1: Verify prerequisites

```bash
# Check for Developer ID Application cert
IDENTITY=$(security find-identity -v -p codesigning | grep "Developer ID Application" | head -1)
if [ -z "$IDENTITY" ]; then
  echo "ERROR: No 'Developer ID Application' certificate found."
  echo "Create one at developer.apple.com > Certificates > Developer ID Application"
  echo "Then download and double-click to install."
  exit 1
fi
SIGNING_ID=$(echo "$IDENTITY" | sed 's/.*"\(.*\)"/\1/')
echo "Signing identity: $SIGNING_ID"

# Check for notarytool keychain profile
xcrun notarytool history --keychain-profile qtpilot-notary 2>/dev/null
if [ $? -ne 0 ]; then
  echo "ERROR: No keychain profile 'qtpilot-notary' found."
  echo "Run: xcrun notarytool store-credentials qtpilot-notary"
  exit 1
fi
echo "Notarytool profile: qtpilot-notary OK"
```

If prerequisites are missing, tell the user exactly what to do and stop.

### Step 2: Detect Qt and build

First, detect the Qt installation. Check for an existing build cache, then common install locations, then ask the user.

```bash
# Try to find Qt prefix path
if [ -f build/macos-release/CMakeCache.txt ]; then
  QT_PREFIX=$(grep "CMAKE_PREFIX_PATH" build/macos-release/CMakeCache.txt | head -1 | cut -d= -f2)
elif [ -f build/CMakeCache.txt ]; then
  QT_PREFIX=$(grep "CMAKE_PREFIX_PATH" build/CMakeCache.txt | head -1 | cut -d= -f2)
fi

# If a Qt version argument was provided, look for it
if [ -n "$ARGUMENTS" ]; then
  for candidate in "/Applications/Qt/$ARGUMENTS/macos" "$HOME/Qt/$ARGUMENTS/macos"; do
    [ -d "$candidate" ] && QT_PREFIX="$candidate" && break
  done
fi

echo "Qt prefix: $QT_PREFIX"
```

If no Qt prefix is found, use AskUserQuestion to ask the user for the path.

```bash
# Configure with macOS preset
cmake --preset macos-release -DCMAKE_PREFIX_PATH="$QT_PREFIX"

# Build
cmake --build --preset macos-release

# Run tests
ctest --preset macos-release -LE admin
```

If the build or tests fail, stop and report the error.

### Step 3: Stage binaries

```bash
STAGING="dist/macos-release"
rm -rf "$STAGING"
mkdir -p "$STAGING"

# Install to staging
cmake --install build/macos-release --prefix "$STAGING"

# Identify the binaries
LAUNCHER="$STAGING/bin/qtPilot-launcher"
PROBE=$(ls "$STAGING"/lib/libqtPilot-probe-*.dylib 2>/dev/null | head -1)

echo "Launcher: $LAUNCHER"
echo "Probe: $PROBE"

# Verify they exist
[ -f "$LAUNCHER" ] || { echo "ERROR: Launcher not found"; exit 1; }
[ -f "$PROBE" ] || { echo "ERROR: Probe dylib not found"; exit 1; }
```

### Step 4: Sign with Developer ID

Sign the dylib first, then the executable (sign leaf nodes before parents).

```bash
# SIGNING_ID comes from step 1

# Sign the probe dylib with hardened runtime
codesign --sign "$SIGNING_ID" \
  --options runtime \
  --timestamp \
  --force \
  "$PROBE"

# Sign the launcher with hardened runtime
codesign --sign "$SIGNING_ID" \
  --options runtime \
  --timestamp \
  --force \
  "$LAUNCHER"

# Verify signatures
codesign --verify --verbose=2 "$PROBE"
codesign --verify --verbose=2 "$LAUNCHER"

# Check Gatekeeper assessment
spctl --assess --type execute --verbose=2 "$LAUNCHER" 2>&1 || true
```

Report the verification results. `--options runtime` enables hardened runtime (required for notarization). `--timestamp` embeds a secure timestamp.

### Step 5: Create zip for notarization

Apple notarytool requires a .zip (not .tar.gz) for submission.

```bash
NOTARIZE_ZIP="dist/qtpilot-notarize.zip"
rm -f "$NOTARIZE_ZIP"

# Create zip containing just the binaries
PROBE_BASENAME=$(basename "$PROBE")
(cd dist/macos-release && zip -j "../../$NOTARIZE_ZIP" "bin/qtPilot-launcher" "lib/$PROBE_BASENAME")
echo "Created: $NOTARIZE_ZIP"
```

### Step 6: Submit for notarization

```bash
xcrun notarytool submit "$NOTARIZE_ZIP" \
  --keychain-profile qtpilot-notary \
  --wait
```

This blocks until Apple finishes processing (usually 2-15 minutes, can be longer for first submissions).
If notarization fails, fetch the log:

```bash
# Get the submission ID from the output, then:
xcrun notarytool log <submission-id> --keychain-profile qtpilot-notary
```

Report the full notarization result to the user.

### Step 7: Staple

Staple the notarization ticket to each binary so offline verification works.

```bash
# Staple each binary
xcrun stapler staple "$LAUNCHER"
xcrun stapler staple "$PROBE"

# Verify stapling
xcrun stapler validate "$LAUNCHER"
xcrun stapler validate "$PROBE"
```

Note: `stapler staple` only works on .app bundles, .dmg, and .pkg — it may fail on bare executables and dylibs. If it does, that's OK: the notarization ticket is still in Apple's service and macOS will find it online. Report the result either way.

### Step 8: Package release archive

```bash
# Determine Qt version tag from build
QT_VERSION=$(grep "Qt6_DIR" build/macos-release/CMakeCache.txt | grep -o '[0-9]\+\.[0-9]\+\.[0-9]\+' | head -1)
QT_TAG="qt$(echo $QT_VERSION | cut -d. -f1,2)"
ARCH=$(uname -m)  # arm64 or x86_64

RELEASE_DIR="dist/release"
ARCHIVE_NAME="qtpilot-${QT_TAG}-macos-${ARCH}.tar.gz"
rm -rf "$RELEASE_DIR"
mkdir -p "$RELEASE_DIR"

# Copy with simplified names
cp "$LAUNCHER" "$RELEASE_DIR/qtPilot-launcher"
cp "$PROBE" "$RELEASE_DIR/qtPilot-probe.dylib"

# Create archive
tar -czf "dist/$ARCHIVE_NAME" -C "$RELEASE_DIR" .

# Generate checksum
(cd dist && shasum -a 256 "$ARCHIVE_NAME" > "$ARCHIVE_NAME.sha256" && cat "$ARCHIVE_NAME.sha256")

echo ""
echo "Release archive: dist/$ARCHIVE_NAME"
echo "Checksum: dist/$ARCHIVE_NAME.sha256"
```

### Step 9: Verify end-to-end

```bash
# Extract to temp dir and verify
VERIFY_DIR=$(mktemp -d)
tar -xzf "dist/$ARCHIVE_NAME" -C "$VERIFY_DIR"

# Check signatures
codesign --verify --verbose=2 "$VERIFY_DIR/qtPilot-launcher"
codesign --verify --verbose=2 "$VERIFY_DIR/qtPilot-probe.dylib"

# Check Gatekeeper
spctl --assess --type execute --verbose=2 "$VERIFY_DIR/qtPilot-launcher" 2>&1

# Check quarantine behavior (simulate download)
xattr -w com.apple.quarantine "0081;$(date +%s);Safari;|com.apple.Safari" "$VERIFY_DIR/qtPilot-launcher"
"$VERIFY_DIR/qtPilot-launcher" --help 2>&1 || echo "(exit code: $?)"

rm -rf "$VERIFY_DIR"
```

Report the full verification results. The key test is whether the binary runs even with the quarantine xattr applied — this simulates what a real user downloading from GitHub would experience.

## Output

When complete, report:
- Build status + test results
- Signing identity used
- Notarization status (approved/rejected + submission ID)
- Stapling results
- Final archive path and SHA256
- Gatekeeper verification result
