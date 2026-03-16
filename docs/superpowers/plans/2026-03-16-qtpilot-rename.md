# qtPilot Rename Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rename the entire project from qtpilot/qtPilot to qtPilot across all layers (C++, Python, CMake, docs, GitHub).

**Architecture:** Big-bang scripted rename — file/directory renames via `git mv`, then `sed` replacements in longest-match-first order with post-fixups for casing, followed by clean rebuild and full test verification.

**Tech Stack:** bash, sed, git, cmake, ctest, Python

**Spec:** `docs/superpowers/specs/2026-03-16-qtpilot-rename-design.md`

---

## Task 1: Rename Files and Directories

These are `git mv` operations that must happen before text replacement so that sed operates on the correct paths.

**Files:**
- Rename: `python/src/qtpilot/` -> `python/src/qtpilot/`
- Rename: `cmake/qtPilot_inject_probe.cmake` -> `cmake/qtPilot_inject_probe.cmake`
- Rename: `cmake/qtPilotConfig.cmake.in` -> `cmake/qtPilotConfig.cmake.in`
- Rename: `qtPilot-specification.md` -> `qtPilot-specification.md`
- Rename: `qtPilot-compatibility-modes.md` -> `qtPilot-compatibility-modes.md`
- Rename: `qt515-tools/qtPilot-launcher.exe` -> `qt515-tools/qtPilot-launcher.exe`
- Rename: `qt515-tools/qtPilot-probe.dll` -> `qt515-tools/qtPilot-probe.dll`

- [ ] **Step 1: Rename Python package directory**

```bash
git mv python/src/qtpilot python/src/qtpilot
```

- [ ] **Step 2: Rename cmake files**

```bash
git mv cmake/qtPilot_inject_probe.cmake cmake/qtPilot_inject_probe.cmake
git mv cmake/qtPilotConfig.cmake.in cmake/qtPilotConfig.cmake.in
```

- [ ] **Step 3: Rename root-level spec files**

```bash
git mv qtPilot-specification.md qtPilot-specification.md
git mv qtPilot-compatibility-modes.md qtPilot-compatibility-modes.md
```

- [ ] **Step 4: Rename prebuilt binaries in qt515-tools**

```bash
git mv qt515-tools/qtPilot-launcher.exe qt515-tools/qtPilot-launcher.exe
git mv qt515-tools/qtPilot-probe.dll qt515-tools/qtPilot-probe.dll
```

- [ ] **Step 5: Verify renames**

```bash
# Should find zero files with qtpilot in their name (excluding build dirs and .git)
find . -path ./.git -prune -o -path ./build -prune -o -path ./build_qt_dir_test -prune -o -path ./build_verify -prune -o -path ./node_modules -prune -o -path './python/dist' -prune -o -name '*qtpilot*' -not -name '*.jsonl' -print
```

Expected: No output (the `.jsonl` log files are runtime artifacts, excluded).

---

## Task 2: Text Replacements (Longest-Match-First)

Apply `sed` replacements across all text files. Order matters — longest patterns first to prevent partial matches. Exclude binary files, build dirs, `.git/`, `node_modules/`, `python/dist/`, `package-lock.json`, and runtime log files.

**Files:** All `*.cpp`, `*.h`, `*.py`, `*.cmake`, `*.toml`, `*.json`, `*.md`, `*.in`, `*.yml`, `*.yaml`, `*.txt` files outside excluded directories.

- [ ] **Step 1: Create the replacement script**

Create a bash script `rename.sh` in the repo root that applies all replacements from the spec in order. The script uses `find` + `sed -i` with each pattern applied sequentially.

```bash
cat > rename.sh << 'SCRIPT'
#!/bin/bash
set -euo pipefail

# Find all text files to process (exclude build dirs, .git, binaries, lock files)
FILES=$(find . \
  -path ./.git -prune -o \
  -path ./build -prune -o \
  -path ./build_qt_dir_test -prune -o \
  -path ./build_verify -prune -o \
  -path ./node_modules -prune -o \
  -path './python/dist' -prune -o \
  -name 'package-lock.json' -prune -o \
  -name '*.jsonl' -prune -o \
  -type f \( \
    -name '*.cpp' -o -name '*.h' -o -name '*.py' -o \
    -name '*.cmake' -o -name '*.toml' -o -name '*.json' -o \
    -name '*.md' -o -name '*.in' -o -name '*.yml' -o \
    -name '*.yaml' -o -name '*.txt' -o -name '*.html' -o \
    -name '*.cfg' -o -name '*.ini' \
  \) -print)

echo "Processing $(echo "$FILES" | wc -l) files..."

# Phase 1: Ordered replacements (longest match first)
# Each sed call processes ALL files for ONE pattern

echo "Phase 1: Ordered replacements..."

# Rule 1-2: Filename references
sed -i 's/qtPilot-specification/qtPilot-specification/g' $FILES
sed -i 's/qtPilot-compatibility/qtPilot-compatibility/g' $FILES

# Rule 3: CMake module/function
sed -i 's/qtPilot_inject_probe/qtPilot_inject_probe/g' $FILES

# Rule 4-5: Binary names (hyphenated)
sed -i 's/qtPilot-test-app/qtPilot-test-app/g' $FILES
sed -i 's/qtPilot-launcher/qtPilot-launcher/g' $FILES

# Rule 6-8: CMake targets (underscored)
sed -i 's/qtPilot_test_app/qtPilot_test_app/g' $FILES
sed -i 's/qtPilot_launcher/qtPilot_launcher/g' $FILES
sed -i 's/qtPilot_add_test/qtPilot_add_test/g' $FILES

# Rule 9-11: Probe and shared targets
sed -i 's/qtPilot-probe/qtPilot-probe/g' $FILES
sed -i 's/qtPilot_probe/qtPilot_probe/g' $FILES
sed -i 's/qtPilot_shared/qtPilot_shared/g' $FILES

# Rule 12-13: Name map and log file prefix
sed -i 's/qtPilot-names/qtPilot-names/g' $FILES
sed -i 's/qtPilot-log-/qtPilot-log-/g' $FILES

# Rule 14: Discovery protocol
sed -i 's/qtPilot-discovery/qtPilot-discovery/g' $FILES

# Rule 15: UPPERCASE macros/env vars
sed -i 's/QTPILOT_/QTPILOT_/g' $FILES

# Rule 16-17: PascalCase display names
sed -i 's/qtPilot/qtPilot/g' $FILES
sed -i 's/qtPilot/qtPilot/g' $FILES

# Rule 18: Middleware log prefix check
sed -i 's/qtpilot_log_/qtpilot_log_/g' $FILES

# Rule 19: MCP tool names and CMake internal vars (underscore suffix)
sed -i 's/qtpilot_/qtpilot_/g' $FILES

# Rule 20: Python imports and JSON-RPC methods (dot suffix)
sed -i 's/qtpilot\./qtpilot./g' $FILES

# Rule 21: Catch-all remaining lowercase
sed -i 's/qtpilot/qtpilot/g' $FILES

# Rule 22: GitHub repo URLs (should already be handled by 17, but be explicit)
sed -i 's|ssss2art/qtPilot|ssss2art/qtPilot|g' $FILES

echo "Phase 1 complete."

# Phase 2: Post-replacement fixups (restore correct casing)
echo "Phase 2: Casing fixups..."

# C++ namespace (only in .cpp, .h files)
CPP_FILES=$(echo "$FILES" | grep -E '\.(cpp|h)$' || true)
if [ -n "$CPP_FILES" ]; then
  sed -i 's/namespace qtpilot/namespace qtPilot/g' $CPP_FILES
  sed -i 's/qtpilot::/qtPilot::/g' $CPP_FILES
  sed -i 's|// namespace qtpilot|// namespace qtPilot|g' $CPP_FILES
  sed -i 's/using namespace qtpilot/using namespace qtPilot/g' $CPP_FILES
fi

# Log prefix in C++ fprintf/qDebug strings
sed -i 's/\[qtpilot\]/[qtPilot]/g' $FILES

# Binary output names in CMake and C++ source
sed -i 's/qtPilot-probe/qtPilot-probe/g' $FILES
sed -i 's/qtPilot-launcher/qtPilot-launcher/g' $FILES
sed -i 's/qtPilot-test-app/qtPilot-test-app/g' $FILES

# .mcp.json server name
sed -i 's/"qtpilot"/"qtPilot"/g' .mcp.json

echo "Phase 2 complete."
echo "Done! Run grep to verify no qtpilot references remain."
SCRIPT
chmod +x rename.sh
```

- [ ] **Step 2: Run the replacement script**

```bash
bash rename.sh
```

Expected: Processes ~300+ files with no errors.

- [ ] **Step 3: Delete the script (it was a one-time tool)**

```bash
rm rename.sh
```

- [ ] **Step 4: Verify no qtpilot references remain**

```bash
grep -ri "qtpilot" --include="*.cpp" --include="*.h" --include="*.py" --include="*.cmake" --include="*.toml" --include="*.json" --include="*.md" --include="*.in" --include="*.yml" --include="*.txt" --include="*.html" . \
  | grep -v '\.git/' | grep -v '/build/' | grep -v 'build_qt_dir_test/' | grep -v 'build_verify/' | grep -v 'node_modules/' | grep -v 'python/dist/' | grep -v 'package-lock' | grep -v '\.jsonl' || echo "CLEAN - no qtpilot references found"
```

Expected: `CLEAN - no qtpilot references found`

If any hits remain, fix them manually with targeted `sed` commands before proceeding.

---

## Task 3: Build and Test C++

Clean rebuild required since CMake target names changed.

- [ ] **Step 1: Remove old build directory**

```bash
rm -rf build
```

- [ ] **Step 2: CMake configure**

```bash
QT_DIR=$(cat build_qt_dir_test/CMakeCache.txt 2>/dev/null | grep "QTPILOT_QT_DIR\|QTPILOT_QT_DIR" | head -1 | cut -d= -f2 || echo "")
# If QT_DIR is empty, check the old cache before it was deleted
# You may need to set this manually to your Qt installation path
cmake -B build -DQTPILOT_QT_DIR="$QT_DIR"
```

Note: The CMake variable is now `QTPILOT_QT_DIR` (was `QTPILOT_QT_DIR`). If configure fails because the Qt dir can't be found, set it explicitly: `cmake -B build -DQTPILOT_QT_DIR=/path/to/Qt/5.15.x/msvc2019_64`

- [ ] **Step 3: Build**

```bash
cmake --build build --config Release
```

Expected: Clean build, no errors. If there are errors, they are likely missed renames — fix and rebuild.

- [ ] **Step 4: Run all tests**

```bash
QT_DIR=$(grep "QTPILOT_QT_DIR:PATH=" build/CMakeCache.txt | cut -d= -f2)
cmd //c "set PATH=${QT_DIR}\bin;%PATH% && set QT_PLUGIN_PATH=${QT_DIR}\plugins && ctest --test-dir build -C Release --output-on-failure"
```

Expected: `100% tests passed, 0 tests failed out of 15`

- [ ] **Step 5: Verify binary names**

```bash
ls build/bin/Release/qtPilot-launcher.exe build/bin/Release/qtPilot-test-app.exe
ls build/bin/Release/qtPilot-probe-*.dll 2>/dev/null || ls build/bin/Release/libqtPilot-probe-*.so 2>/dev/null
```

Expected: All binaries use `qtPilot-` prefix.

---

## Task 4: Verify Python Package

- [ ] **Step 1: Check Python imports**

```bash
cd python && pip install -e . 2>&1 | tail -5 && cd ..
```

- [ ] **Step 2: Verify import works**

```bash
python -c "from qtpilot.server import create_mcp_server; print('OK')"
```

Expected: `OK`

- [ ] **Step 3: Check pyproject.toml values**

```bash
grep -E 'name|scripts|version-file' python/pyproject.toml
```

Expected output should show:
- `name = "qtpilot"`
- `qtpilot = "qtpilot.cli:main"`
- `version-file = "src/qtpilot/_version.py"`

---

## Task 5: Final Verification and Spot-Checks

- [ ] **Step 1: Verify .mcp.json**

```bash
cat .mcp.json
```

Expected: Server name is `"qtPilot"`, command is `"qtpilot"`.

- [ ] **Step 2: Verify CLAUDE.md references**

```bash
grep -c "qtPilot" CLAUDE.md
grep -c "qtpilot" CLAUDE.md
```

Expected: First grep returns many hits, second returns 0.

- [ ] **Step 3: Verify CI workflow files**

```bash
grep -c "qtpilot" .github/workflows/*.yml || echo "CLEAN"
```

Expected: `CLEAN`

- [ ] **Step 4: Verify discovery protocol string**

```bash
grep -r "qtPilot-discovery" src/probe/transport/discovery_broadcaster.cpp
```

Expected: Protocol string is `qtPilot-discovery`.

---

## Task 6: Commit, Rename Repo, and Push

- [ ] **Step 1: Stage all changes**

```bash
git add -A
```

- [ ] **Step 2: Review staged changes**

```bash
git diff --cached --stat | tail -5
```

Verify the file count looks reasonable (~250+ files changed).

- [ ] **Step 3: Commit**

```bash
git commit -m "$(cat <<'EOF'
rename: qtpilot -> qtPilot across entire project

Breaking change: wire protocol identifiers, JSON-RPC method names,
discovery protocol, CMake package name, and Python package name all
changed. Old probes and new servers are not compatible.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 4: Push current branch before repo rename**

```bash
git push origin main
```

- [ ] **Step 5: Rename the GitHub repository**

```bash
gh repo rename qtPilot --yes
```

- [ ] **Step 6: Update git remote URL**

```bash
git remote set-url origin https://github.com/ssss2art/qtPilot.git
```

- [ ] **Step 7: Verify remote**

```bash
git remote -v
```

Expected: Both fetch and push URLs point to `ssss2art/qtPilot.git`.

- [ ] **Step 8: Create release**

```bash
gh release create v2.0 --title "v2.0 - qtPilot" --notes "$(cat <<'EOF'
## Breaking: Project Renamed to qtPilot

This release renames the entire project from qtPilot to qtPilot. This is a **breaking change** — old probes and new servers are not compatible.

### What Changed
- C++ namespace: `qtpilot` -> `qtPilot`
- Python package: `qtpilot` -> `qtpilot`
- Binaries: `qtPilot-launcher` -> `qtPilot-launcher`, etc.
- MCP tools: `qtpilot_*` -> `qtpilot_*`
- Environment variables: `QTPILOT_*` -> `QTPILOT_*`
- Wire protocol: `qtPilot-discovery` -> `qtPilot-discovery`
- CMake: `find_package(qtPilot)` -> `find_package(qtPilot)`

### Migration
Replace all `qtpilot`/`QTPILOT` references with the corresponding `qtpilot`/`qtPilot`/`QTPILOT` variant. See the naming convention table in the README for the exact mapping.
EOF
)"
```
