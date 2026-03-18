# Bundled Test App Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship the test app with bundled Qt DLLs in release archives so users can try qtPilot with `qtpilot serve --demo`.

**Architecture:** CI builds the test app (already happens), then a new post-build step uses `windeployqt` (Windows) or manual lib copy (Linux) to create a self-contained `testapp/` directory. The release workflow includes this directory in the existing archives. A new `--demo` flag on `qtpilot serve` auto-launches the bundled test app.

**Tech Stack:** CMake, GitHub Actions, Python (argparse, pathlib), windeployqt

---

### Task 1: Add CMake install rule for test app

**Files:**
- Modify: `CMakeLists.txt:283-285`

- [ ] **Step 1: Add install rule**

In `CMakeLists.txt`, after `add_subdirectory(test_app)` (~line 284), add an install rule:

```cmake
if(QTPILOT_BUILD_TEST_APP)
    add_subdirectory(test_app)
    install(TARGETS qtPilot_test_app RUNTIME DESTINATION bin)
endif()
```

This replaces the existing two-line block (lines 283-285) that only has `add_subdirectory`.

- [ ] **Step 2: Verify locally**

```bash
cmake --build build --config Release --target qtPilot_test_app
cmake --install build --prefix install/test --config Release
ls install/test/bin/qtPilot-test-app*
```

Expected: `qtPilot-test-app.exe` appears in `install/test/bin/`.

- [ ] **Step 3: Commit**

```bash
git add CMakeLists.txt
git commit -m "build: add install rule for test app"
```

---

### Task 2: Add test app bundling step to CI workflow

**Files:**
- Modify: `.github/workflows/ci.yml:6-12` (paths trigger)
- Modify: `.github/workflows/ci.yml:86-92` (after Install step, before Upload)

- [ ] **Step 1: Add `test_app/**` to paths trigger**

In `.github/workflows/ci.yml`, add `"test_app/**"` to the `paths:` list (after line 9):

```yaml
    paths:
      - "src/**"
      - "tests/**"
      - "test_app/**"
      - "CMakeLists.txt"
      - "cmake/**"
      - "CMakePresets.json"
      - ".github/workflows/**"
```

- [ ] **Step 2: Add Windows bundle step**

After the "Install" step (line 86) and before the "Upload artifact" step (line 88), add:

```yaml
      - name: Bundle test app (Windows)
        if: runner.os == 'Windows'
        shell: bash
        run: |
          PRESET="${{ matrix.preset }}"
          # Resolve Qt prefix (same logic as Configure step)
          if [ -n "$QT_ROOT_DIR" ]; then
            QT_PATH="$QT_ROOT_DIR"
          elif [ -n "$Qt6_DIR" ]; then
            QT_PATH="$(cd "$Qt6_DIR/../../.." && pwd)"
          elif [ -n "$Qt5_DIR" ]; then
            QT_PATH="$(cd "$Qt5_DIR/../../.." && pwd)"
          fi
          mkdir -p "install/$PRESET/testapp"
          cp "build/$PRESET/bin/Release/qtPilot-test-app.exe" "install/$PRESET/testapp/"
          "$QT_PATH/bin/windeployqt.exe" --no-translations "install/$PRESET/testapp/qtPilot-test-app.exe"
          echo "Test app bundle size:"
          du -sh "install/$PRESET/testapp/"
```

- [ ] **Step 3: Add Linux bundle step**

After the Windows bundle step, add:

```yaml
      - name: Bundle test app (Linux)
        if: runner.os == 'Linux'
        shell: bash
        run: |
          PRESET="${{ matrix.preset }}"
          QT_MAJOR=$(echo "${{ matrix.qt }}" | cut -c1)
          if [ -n "$QT_ROOT_DIR" ]; then
            QT_PATH="$QT_ROOT_DIR"
          elif [ -n "$Qt6_DIR" ]; then
            QT_PATH="$(cd "$Qt6_DIR/../../.." && pwd)"
          elif [ -n "$Qt5_DIR" ]; then
            QT_PATH="$(cd "$Qt5_DIR/../../.." && pwd)"
          fi
          mkdir -p "install/$PRESET/testapp/lib" "install/$PRESET/testapp/plugins/platforms"
          cp "build/$PRESET/bin/qtPilot-test-app" "install/$PRESET/testapp/"
          # Copy minimal Qt runtime
          for lib in libQt${QT_MAJOR}Core.so* libQt${QT_MAJOR}Gui.so* libQt${QT_MAJOR}Widgets.so* libQt${QT_MAJOR}DBus.so* libQt${QT_MAJOR}XcbQpa.so* libicui18n.so* libicuuc.so* libicudata.so*; do
            find "$QT_PATH/lib" -name "$lib" -exec cp {} "install/$PRESET/testapp/lib/" \; 2>/dev/null || true
          done
          cp "$QT_PATH/plugins/platforms/libqxcb.so" "install/$PRESET/testapp/plugins/platforms/" 2>/dev/null || true
          cp "$QT_PATH/plugins/platforms/libqminimal.so" "install/$PRESET/testapp/plugins/platforms/" 2>/dev/null || true
          # Create wrapper script
          cat > "install/$PRESET/testapp/qtPilot-test-app.sh" << 'WRAPPER'
#!/bin/sh
DIR="$(cd "$(dirname "$0")" && pwd)"
export LD_LIBRARY_PATH="$DIR/lib:$LD_LIBRARY_PATH"
export QT_PLUGIN_PATH="$DIR/plugins"
exec "$DIR/qtPilot-test-app" "$@"
WRAPPER
          chmod +x "install/$PRESET/testapp/qtPilot-test-app.sh"
          echo "Test app bundle size:"
          du -sh "install/$PRESET/testapp/"
```

- [ ] **Step 4: Commit**

```bash
git add .github/workflows/ci.yml
git commit -m "ci: bundle test app with Qt runtime in artifacts"
```

---

### Task 3: Include test app in release archives

**Files:**
- Modify: `.github/workflows/release.yml:57-73`

- [ ] **Step 1: Add testapp copy to staging**

In the "Prepare release archives" step, after the probe + launcher copy block (after line 73), add:

```bash
            # Copy bundled test app (self-contained with Qt DLLs)
            if [ -d "$dir/testapp/" ]; then
              mkdir -p "$staging/testapp"
              cp -r "$dir/testapp/"* "$staging/testapp/"
            fi
```

- [ ] **Step 2: Fix zip command for subdirectories**

The current zip command on line 87 uses `-j` (junk paths) which flattens everything. Change line 87 from:

```bash
              (cd "$staging_dir" && zip -j "../../release/$archive" *)
```

to:

```bash
              (cd "$staging_dir" && zip -r "../../release/$archive" .)
```

This preserves the `testapp/` subdirectory structure in the zip.

- [ ] **Step 3: Commit**

```bash
git add .github/workflows/release.yml
git commit -m "release: include bundled test app in archives"
```

---

### Task 4: Add `get_testapp_path()` to download module

**Files:**
- Modify: `python/src/qtpilot/download.py` (add function after `get_launcher_filename`, ~line 112)
- Test: `python/tests/test_download.py`

- [ ] **Step 1: Write test**

Add to `python/tests/test_download.py`:

```python
class TestGetTestappPath:
    def test_returns_none_when_not_found(self, tmp_path):
        """Returns None when test app doesn't exist."""
        from qtpilot.download import get_testapp_path
        assert get_testapp_path(output_dir=tmp_path, platform_name="windows") is None

    def test_finds_windows_testapp(self, tmp_path):
        """Finds qtPilot-test-app.exe in testapp/ subdirectory."""
        from qtpilot.download import get_testapp_path
        testapp_dir = tmp_path / "testapp"
        testapp_dir.mkdir()
        exe = testapp_dir / "qtPilot-test-app.exe"
        exe.touch()
        result = get_testapp_path(output_dir=tmp_path, platform_name="windows")
        assert result == exe

    def test_finds_linux_testapp(self, tmp_path):
        """Finds qtPilot-test-app.sh wrapper in testapp/ subdirectory."""
        from qtpilot.download import get_testapp_path
        testapp_dir = tmp_path / "testapp"
        testapp_dir.mkdir()
        wrapper = testapp_dir / "qtPilot-test-app.sh"
        wrapper.touch()
        result = get_testapp_path(output_dir=tmp_path, platform_name="linux")
        assert result == wrapper
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cd python && pytest tests/test_download.py::TestGetTestappPath -v
```

Expected: `ImportError` — `get_testapp_path` doesn't exist yet.

- [ ] **Step 3: Implement `get_testapp_path()`**

In `python/src/qtpilot/download.py`, after `get_launcher_filename()` (~line 112), add:

```python
def get_testapp_path(output_dir: Path | None = None, platform_name: str | None = None) -> Path | None:
    """Find the bundled test app in the download directory.

    Args:
        output_dir: Directory where tools were extracted (default: cwd)
        platform_name: Platform name (auto-detected if None)

    Returns:
        Path to test app executable/wrapper, or None if not found.
    """
    if output_dir is None:
        output_dir = Path.cwd()
    if platform_name is None:
        platform_name = detect_platform()

    if platform_name == "windows":
        path = output_dir / "testapp" / "qtPilot-test-app.exe"
    else:
        path = output_dir / "testapp" / "qtPilot-test-app.sh"

    return path if path.exists() else None
```

- [ ] **Step 4: Run test to verify it passes**

```bash
cd python && pytest tests/test_download.py::TestGetTestappPath -v
```

Expected: 3 PASS.

- [ ] **Step 5: Commit**

```bash
git add python/src/qtpilot/download.py python/tests/test_download.py
git commit -m "feat: add get_testapp_path() to download module"
```

---

### Task 5: Add `--demo` flag to CLI and server

**Files:**
- Modify: `python/src/qtpilot/cli.py:96-153` (serve_parser args)
- Modify: `python/src/qtpilot/cli.py:11-34` (cmd_serve)
- Modify: `python/src/qtpilot/cli.py:37-73` (cmd_download_tools)

- [ ] **Step 1: Add `--demo` argument to serve parser**

In `python/src/qtpilot/cli.py`, after the `--arch` argument (line 152), add:

```python
    serve_parser.add_argument(
        "--demo",
        action="store_true",
        help="Launch the bundled test app (requires download-tools first)",
    )
```

- [ ] **Step 2: Handle `--demo` in `cmd_serve()`**

Replace the existing `cmd_serve()` function (lines 11-34) with:

```python
def cmd_serve(args: argparse.Namespace) -> int:
    """Run the MCP server."""
    # Handle --demo flag
    if getattr(args, "demo", False):
        if args.target:
            print("Error: Cannot use --demo with --target", file=sys.stderr)
            return 1
        from qtpilot.download import get_testapp_path
        testapp = get_testapp_path()
        if testapp is None:
            print(
                "Error: Test app not found. Run: qtpilot download-tools --qt-version <version>",
                file=sys.stderr,
            )
            return 1
        args.target = str(testapp)

    # ws_url is None unless explicitly provided or a target is specified
    ws_url = None
    if args.target:
        ws_url = f"ws://localhost:{args.port}"
    elif args.ws_url:
        ws_url = args.ws_url

    from qtpilot.server import create_server

    server = create_server(
        mode=args.mode,
        ws_url=ws_url,
        target=args.target,
        port=args.port,
        launcher_path=args.launcher_path,
        discovery_port=args.discovery_port,
        discovery_enabled=not args.no_discovery,
        qt_version=args.qt_version,
        qt_dir=args.qt_dir,
    )
    server.run()
    return 0
```

- [ ] **Step 3: Print test app path in `cmd_download_tools()`**

In `cmd_download_tools()`, after printing the launcher path (line 57), add:

```python
        from qtpilot.download import get_testapp_path
        testapp = get_testapp_path(output_dir=args.output)
        if testapp:
            print(f"Test app:           {testapp}")
            print(f"\nRun 'qtpilot serve --demo' to try it out!")
```

- [ ] **Step 4: Commit**

```bash
git add python/src/qtpilot/cli.py
git commit -m "feat: add --demo flag to serve command"
```

---

### Task 6: Update README with demo quickstart

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Update quickstart**

The README already has an Examples section (added earlier this session). Add a "Try it now" subsection at the top of Quick Start, before "Option 1: pip install":

```markdown
### Try it now

```bash
pip install qtpilot
qtpilot download-tools --qt-version 6.8
qtpilot serve --demo
```

This downloads everything you need — probe, launcher, and a bundled test app with Qt runtime. Once running, Claude can interact with the test app. Try asking "Show me the widget tree" or "Fill out the form with my name."

### Option 1: pip install (Recommended)
...
```

- [ ] **Step 2: Commit**

```bash
git add README.md
git commit -m "docs: add --demo quickstart to README"
```

---

### Task 7: Push and verify CI

- [ ] **Step 1: Push all commits**

```bash
git push
```

- [ ] **Step 2: Watch CI build**

```bash
gh run list --limit 1
gh run watch <run-id> --exit-status
```

Expected: All jobs pass. The new bundling steps produce `testapp/` directories in artifacts.

- [ ] **Step 3: Check artifact sizes**

Look at the CI build logs for the "Bundle test app" step — it prints `du -sh` output. Windows bundles should be ~15-25MB, Linux ~10-20MB.

- [ ] **Step 4: Tag and release**

```bash
git tag v0.2.0
git push origin v0.2.0
gh run watch <release-run-id> --exit-status
```

Expected: Release workflow passes, archives include `testapp/` directory, PyPI package publishes.

- [ ] **Step 5: Verify end-to-end**

```bash
pip install qtpilot==0.2.0
qtpilot download-tools --qt-version 6.9
qtpilot serve --demo
```

Expected: Test app launches, MCP server starts, Claude can interact.
