#!/usr/bin/env bash
# qt-ctest.sh — run the full ctest suite with Qt DLLs on PATH.
#
# Usage:
#   scripts/qt-ctest.sh                      # run all tests
#   scripts/qt-ctest.sh -R test_model_nav    # filter by regex
#
# All arguments after the script name are forwarded to ctest.

set -euo pipefail

if [[ ! -f build/CMakeCache.txt ]]; then
  echo "error: build/CMakeCache.txt not found — configure first with cmake -B build ..." >&2
  exit 1
fi

qt_dir=$(grep '^QTPILOT_QT_DIR:PATH=' build/CMakeCache.txt | cut -d= -f2)
if [[ -z "$qt_dir" ]]; then
  echo "error: QTPILOT_QT_DIR not set in build/CMakeCache.txt" >&2
  exit 1
fi

case "$OSTYPE" in
  msys*|cygwin*|win32)
    env_prefix="set PATH=${qt_dir}\\bin;%PATH% && set QT_PLUGIN_PATH=${qt_dir}\\plugins"
    cmd //c "${env_prefix} && ctest --test-dir build -C Release --output-on-failure $*"
    ;;
  *)
    QT_PLUGIN_PATH="${qt_dir}/plugins" LD_LIBRARY_PATH="${qt_dir}/lib" \
      ctest --test-dir build -C Release --output-on-failure "$@"
    ;;
esac
