#!/usr/bin/env bash
# Launch a Qt application with the qtPilot probe injected.
#
# Usage:
#   ./scripts/launch-with-probe.sh [options] <executable> [app-args...]
#
# Options:
#   --port <port>         WebSocket port (default: 9222)
#   --probe <path>        Path to probe dylib/so (auto-detected from build/)
#   --extra-lib <path>    Additional library search path (repeatable)
#   --build-dir <path>    Build directory (default: ./build)
#   --help                Show this help
#
# Examples:
#   # Launch a .app bundle
#   ./scripts/launch-with-probe.sh /path/to/MyApp.app
#
#   # Launch with extra library paths
#   ./scripts/launch-with-probe.sh --extra-lib /path/to/libs MyApp.app
#
#   # Custom port and probe
#   ./scripts/launch-with-probe.sh --port 9333 --probe /path/to/probe.dylib MyApp

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build"
PORT=9222
PROBE=""
EXTRA_LIBS=()
TARGET=""
TARGET_ARGS=()

usage() {
    sed -n '2,/^$/s/^# \?//p' "$0"
    exit "${1:-0}"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --port)     PORT="$2"; shift 2 ;;
        --probe)    PROBE="$2"; shift 2 ;;
        --extra-lib) EXTRA_LIBS+=("$2"); shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --help|-h)  usage 0 ;;
        -*)         echo "Unknown option: $1" >&2; usage 1 ;;
        *)
            TARGET="$1"; shift
            TARGET_ARGS=("$@")
            break
            ;;
    esac
done

if [[ -z "$TARGET" ]]; then
    echo "Error: No target executable specified" >&2
    usage 1
fi

# Resolve .app bundles to their inner executable
if [[ "$TARGET" == *.app ]] || [[ "$TARGET" == *.app/ ]]; then
    TARGET="${TARGET%/}"
    PLIST="$TARGET/Contents/Info.plist"
    if [[ -f "$PLIST" ]]; then
        EXEC_NAME=$(/usr/libexec/PlistBuddy -c "Print :CFBundleExecutable" "$PLIST" 2>/dev/null || true)
        if [[ -n "$EXEC_NAME" ]]; then
            TARGET="$TARGET/Contents/MacOS/$EXEC_NAME"
        else
            # Fallback: use the .app basename
            APP_NAME=$(basename "$TARGET" .app)
            TARGET="$TARGET/Contents/MacOS/$APP_NAME"
        fi
    else
        APP_NAME=$(basename "$TARGET" .app)
        TARGET="$TARGET/Contents/MacOS/$APP_NAME"
    fi
fi

if [[ ! -x "$TARGET" ]]; then
    echo "Error: Target not found or not executable: $TARGET" >&2
    exit 1
fi

# Auto-detect probe from build directory
if [[ -z "$PROBE" ]]; then
    # Look for the probe dylib/so in build/lib/
    for pattern in "$BUILD_DIR"/lib/libqtPilot-probe*.dylib "$BUILD_DIR"/lib/libqtPilot-probe*.so; do
        # Skip symlinks, use the versioned one
        if [[ -f "$pattern" && ! -L "$pattern" ]]; then
            PROBE="$pattern"
            break
        fi
    done
    # Fallback: use symlink
    if [[ -z "$PROBE" ]]; then
        for pattern in "$BUILD_DIR"/lib/libqtPilot-probe*.dylib "$BUILD_DIR"/lib/libqtPilot-probe*.so; do
            if [[ -e "$pattern" ]]; then
                PROBE="$pattern"
                break
            fi
        done
    fi
fi

if [[ -z "$PROBE" || ! -e "$PROBE" ]]; then
    echo "Error: Could not find probe library. Build first or use --probe." >&2
    echo "  Searched: $BUILD_DIR/lib/" >&2
    exit 1
fi

PROBE="$(cd "$(dirname "$PROBE")" && pwd)/$(basename "$PROBE")"

# Build environment
ENV_VARS=()
ENV_VARS+=("QTPILOT_PORT=$PORT")

case "$(uname -s)" in
    Darwin)
        ENV_VARS+=("DYLD_INSERT_LIBRARIES=$PROBE")
        # Build DYLD_LIBRARY_PATH from extra libs
        if [[ ${#EXTRA_LIBS[@]} -gt 0 ]]; then
            EXTRA_PATH=$(IFS=:; echo "${EXTRA_LIBS[*]}")
            EXISTING="${DYLD_LIBRARY_PATH:-}"
            if [[ -n "$EXISTING" ]]; then
                ENV_VARS+=("DYLD_LIBRARY_PATH=$EXTRA_PATH:$EXISTING")
            else
                ENV_VARS+=("DYLD_LIBRARY_PATH=$EXTRA_PATH")
            fi
        fi
        ;;
    Linux)
        ENV_VARS+=("LD_PRELOAD=$PROBE")
        if [[ ${#EXTRA_LIBS[@]} -gt 0 ]]; then
            EXTRA_PATH=$(IFS=:; echo "${EXTRA_LIBS[*]}")
            EXISTING="${LD_LIBRARY_PATH:-}"
            if [[ -n "$EXISTING" ]]; then
                ENV_VARS+=("LD_LIBRARY_PATH=$EXTRA_PATH:$EXISTING")
            else
                ENV_VARS+=("LD_LIBRARY_PATH=$EXTRA_PATH")
            fi
        fi
        ;;
    *)
        echo "Error: Unsupported platform: $(uname -s)" >&2
        exit 1
        ;;
esac

echo "[launch-with-probe] Target: $TARGET" >&2
echo "[launch-with-probe] Probe:  $PROBE" >&2
echo "[launch-with-probe] Port:   $PORT" >&2
if [[ ${#EXTRA_LIBS[@]} -gt 0 ]]; then
    echo "[launch-with-probe] Extra libs: ${EXTRA_LIBS[*]}" >&2
fi
echo "" >&2

exec env "${ENV_VARS[@]}" "$TARGET" ${TARGET_ARGS[@]+"${TARGET_ARGS[@]}"}
