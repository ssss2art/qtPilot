#!/usr/bin/env bash
# qt-test.sh — build and run a qtPilot test binary with Qt DLLs on PATH.
#
# Usage:
#   scripts/qt-test.sh <target> [test_filter_args...]
#
# Examples:
#   scripts/qt-test.sh test_model_navigator
#   scripts/qt-test.sh test_model_navigator testEnsureFetchedCallsFetchMoreOnLazyModel
#   scripts/qt-test.sh test_jsonrpc
#
# Runs `cmake --build build --config Release --target <target>` first,
# then invokes `build/bin/Release/<target>.exe -platform minimal` with
# PATH and QT_PLUGIN_PATH set from QTPILOT_QT_DIR in build/CMakeCache.txt.
# Works from bash / git-bash on Windows (uses `cmd //c`), and from Linux.

set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "usage: scripts/qt-test.sh <target> [test_filter_args...]" >&2
  exit 2
fi

target="$1"; shift

if [[ ! -f build/CMakeCache.txt ]]; then
  echo "error: build/CMakeCache.txt not found — configure first with cmake -B build ..." >&2
  exit 1
fi

qt_dir=$(grep '^QTPILOT_QT_DIR:PATH=' build/CMakeCache.txt | cut -d= -f2)
if [[ -z "$qt_dir" ]]; then
  echo "error: QTPILOT_QT_DIR not set in build/CMakeCache.txt" >&2
  exit 1
fi

cmake --build build --config Release --target "$target"

exe="build/bin/Release/${target}.exe"
if [[ ! -f "$exe" ]]; then
  # Linux fallback
  exe="build/bin/Release/${target}"
fi

case "$OSTYPE" in
  msys*|cygwin*|win32)
    # git-bash / mingw: cmd needs backslashes in paths it executes.
    # Qt Test on Windows writes its PASS/FAIL report to a stream that cmd
    # pipelines eat; route it through -o <file>,txt and dump the file.
    exe_win="${exe//\//\\}"
    log="build/${target}.log"
    log_win="${log//\//\\}"
    env_prefix="set PATH=${qt_dir}\\bin;%PATH% && set QT_PLUGIN_PATH=${qt_dir}\\plugins"
    cmd //c "${env_prefix} && ${exe_win} -platform minimal -o ${log_win},txt $*"
    rc=$?
    # Show a one-line summary plus any failing lines so the caller doesn't
    # have to dig through thousands of QDEBUG records.
    if [[ -f "$log" ]]; then
      grep -E '^FAIL|^Totals' "$log" || true
    fi
    exit $rc
    ;;
  *)
    QT_PLUGIN_PATH="${qt_dir}/plugins" LD_LIBRARY_PATH="${qt_dir}/lib" "$exe" -platform minimal "$@"
    ;;
esac
