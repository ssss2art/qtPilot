# Screenshots Always Go to a File — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `qt_ui_screenshot` and `cu_screenshot` always save the PNG to a file on the MCP server's machine and return its path, for images of any realistic size from a local or remote probe.

**Architecture:** The server-side helper unwraps the probe's `{meta, result: {...}}` envelope, writes the PNG (to `save_to` or a per-run temp folder) and returns metadata only. The probe WebSocket connection raises `websockets`' 1 MiB `max_size` to 256 MiB so large images arrive.

**Tech Stack:** Python 3.11+ (`from __future__ import annotations`), FastMCP, `websockets` 16 (asyncio client), pytest + pytest-asyncio.

**Spec:** `docs/superpowers/specs/2026-11-25-screenshot-to-file-design.md`

## Global Constraints

- Work in `E:\AI-files\QtMcp-screenshot-file` on branch `fix/screenshot-to-file`.
- Run Python tests from `python/` with: `UV_PROJECT_ENVIRONMENT=.venv-312 uv run --python 3.12 --extra dev --extra mcp-stable python -m pytest <args> -p no:cacheprovider`. (The default 3.14 release candidate breaks pydantic; `uv run pytest` picks up a stale global `qtpilot`.)
- Known unrelated failures in this environment: `tests/test_launcher_signals.py` cannot import on Windows (`signal.SIGHUP`; pass `--ignore`), and 3 TLS tests in `tests/test_connection_when_ready.py` fail on `main` too. Anything else red is yours.
- Return value for both tools, exactly: `{"saved_to": <absolute path str>, "width": int, "height": int, "bytes": int}`. No base64 image data anywhere in a tool result.
- `as_image` is removed from both tools.
- Default folder: `<tempfile.gettempdir()>/qtpilot/screenshots/<server start time>-<pid>/`; file name `<4-digit sequence>-<tool name>.png`, e.g. `0003-qt_ui_screenshot.png`.
- Probe message cap: 256 MiB, as a named module constant in `connection.py`, not a CLI option.
- Strict red/green TDD (AGENTS.md): each new test seen failing before its fix. Zero pytest warnings.
- Confidentiality (AGENTS.md): no driven-app names in code, tests, docs, commits or PR text.

## Review Focus

1. **A full-window screenshot over 1 MiB from a real probe.** Expect it to arrive and save. Pinned by Task 3's 2 MiB test and Task 4's live check.
2. **Two screenshots in one run.** Expect two distinct files; the second never overwrites the first. Pinned in Task 1.
3. **The probe reply has no `width`/`height`** (`qt.ui.screenshot` sends only `image`). Expect dimensions read from the PNG header. Pinned in Task 1.
4. **A reply with no image or an empty one.** Expect an error and no file written. Pinned in Task 1.
5. **`save_to` pointing into a folder that doesn't exist yet.** Expect the folders created. Kept as an existing test in Task 1.

---

### Task 1: The helper saves every screenshot to a file

**Files:**
- Modify: `python/src/qtpilot/tools/screenshot_helper.py`
- Test: `python/tests/test_screenshot_efficiency.py` (class `TestProcessScreenshotResponse`)

**Interfaces:**
- Produces: `process_screenshot_response(resp: dict, *, tool: str, save_to: str | None = None) -> dict`, returning the Global Constraints shape. `resp` is what `ProbeConnection.call()` returns: the envelope `{"meta": {...}, "result": {"image": b64, "width"?: int, "height"?: int}}`; a flat `{"image": ...}` is still accepted. Raises `ValueError("probe reply contained no image")` when `image` is missing or empty, before writing anything.
- Produces: `screenshot_run_dir() -> Path`, the default folder for this server run (not created until first save).
- Keeps: `extract_png_dimensions(raw_png: bytes) -> tuple[int, int]` unchanged.

- [ ] **Step 1: Write the failing tests**

Replace `TestProcessScreenshotResponse` (drop the `ImageContent` / `as_image=False` tests). Add a helper `_envelope(b64: str, **extra) -> dict` returning `{"meta": {"timestamp": 1}, "result": {"image": b64, **extra}}`, and an autouse fixture that points the default folder at `tmp_path` with `monkeypatch.setattr(tempfile, "tempdir", str(tmp_path))`.

```python
def test_envelope_is_saved_to_the_run_folder(tmp_path):
    b64, raw = _make_dummy_png(50, 60)
    result = process_screenshot_response(_envelope(b64), tool="qt_ui_screenshot")
    path = Path(result["saved_to"])
    assert path.parent == screenshot_run_dir()
    assert screenshot_run_dir().is_relative_to(tmp_path / "qtpilot" / "screenshots")
    assert re.fullmatch(r"\d{4}-qt_ui_screenshot\.png", path.name)
    assert path.read_bytes() == raw
    assert result == {"saved_to": str(path), "width": 50, "height": 60, "bytes": len(raw)}

def test_two_captures_never_share_a_file():
    b64, _ = _make_dummy_png()
    first = process_screenshot_response(_envelope(b64), tool="cu_screenshot")["saved_to"]
    second = process_screenshot_response(_envelope(b64), tool="cu_screenshot")["saved_to"]
    assert first != second and Path(first).exists() and Path(second).exists()

def test_dimensions_come_from_the_png_when_the_reply_has_none():
    b64, _ = _make_dummy_png(120, 80)
    result = process_screenshot_response(_envelope(b64), tool="qt_ui_screenshot")
    assert (result["width"], result["height"]) == (120, 80)

def test_reply_dimensions_win_over_the_png_header():
    b64, _ = _make_dummy_png(10, 10)
    result = process_screenshot_response(_envelope(b64, width=80, height=40), tool="cu_screenshot")
    assert (result["width"], result["height"]) == (80, 40)

@pytest.mark.parametrize("resp", [{"meta": {}, "result": {}}, {"meta": {}, "result": {"image": ""}}, {}])
def test_no_image_is_an_error_and_writes_nothing(resp):
    with pytest.raises(ValueError, match="no image"):
        process_screenshot_response(resp, tool="qt_ui_screenshot")
    assert not screenshot_run_dir().exists()

def test_flat_reply_is_still_accepted():
    b64, raw = _make_dummy_png()
    result = process_screenshot_response({"image": b64}, tool="qt_ui_screenshot")
    assert Path(result["saved_to"]).read_bytes() == raw

def test_save_to_creates_folders_and_is_used_verbatim(tmp_path):
    b64, raw = _make_dummy_png(64, 48)
    out = tmp_path / "nested" / "capture.png"
    result = process_screenshot_response(_envelope(b64), tool="qt_ui_screenshot", save_to=str(out))
    assert result["saved_to"] == str(out.resolve())
    assert out.read_bytes() == raw
```

Also assert, in the first test, that no value in `result` is the base64 string.

- [ ] **Step 2: Run them to verify they fail**

Run: `... python -m pytest tests/test_screenshot_efficiency.py -k TestProcessScreenshotResponse`
Expected: FAIL. `screenshot_run_dir` doesn't exist and `tool` is not a parameter (ImportError/TypeError); the envelope tests would otherwise fail on an empty image.

- [ ] **Step 3: Implement in `screenshot_helper.py`**

- `_RUN_ID = f"{time.strftime('%Y%m%d-%H%M%S')}-{os.getpid()}"` and a module-level `itertools.count(1)`, both computed once at import.
- `screenshot_run_dir()` returns `Path(tempfile.gettempdir()) / "qtpilot" / "screenshots" / _RUN_ID`, read at call time so the test fixture's `tempfile.tempdir` applies.
- Unwrap with `payload = resp.get("result", resp)` when that value is a dict.
- Hold the return value in a `@dataclass(frozen=True, slots=True) class SavedScreenshot` (`saved_to: str`, `width: int`, `height: int`, `bytes: int`) and return `asdict(...)`.
- Remove the `mcp.types` import and the `create_image_content` path.

- [ ] **Step 4: Run them to verify they pass**

Run the same command. Expected: all `TestProcessScreenshotResponse` and `TestPngDimensionExtraction` tests PASS, with zero warnings. The tool tests further down the file will fail until Task 2.

- [ ] **Step 5: Commit**

```bash
git add python/src/qtpilot/tools/screenshot_helper.py python/tests/test_screenshot_efficiency.py
git commit -m "fix(server): screenshot helper unwraps the probe envelope and always saves to a file"
```

---

### Task 2: Both screenshot tools return a file path

**Files:**
- Modify: `python/src/qtpilot/tools/native.py` (`qt_ui_screenshot`), `python/src/qtpilot/tools/cu.py` (`cu_screenshot`), `python/src/qtpilot/prompts.py` (line 31 guidance)
- Modify (docs): `docs/MACOS.md`, `docs/TROUBLESHOOTING.md`, `docs/QML-A11Y-EVALUATION.md`, `docs/TREE-MODEL-SPEC.md` wherever they show inline screenshot output or `as_image`; `.claude/skills/test-mcp-modes/SKILL.md` Phase 2 step 4 and Phase 5 step 3. Leave dated design docs under `docs/superpowers/` as they are.
- Test: `python/tests/test_screenshot_efficiency.py` (classes `TestNativeScreenshotTool`, `TestCuScreenshotTool`), plus any test in `python/tests/` that passes `as_image` (grep for it)

**Interfaces:**
- Consumes: Task 1's `process_screenshot_response(resp, *, tool=..., save_to=...)`.
- Produces: tool signatures `qt_ui_screenshot(objectId: str, fullWindow: bool | None = None, region: dict | None = None, save_to: str | None = None) -> dict` and `cu_screenshot(save_to: str | None = None) -> dict`.

- [ ] **Step 1: Write the failing tests**

Rewrite the four tool tests so the mocked `probe.call` returns the real envelope (`_envelope(b64)` for `qt_ui_screenshot`, `_envelope(b64, width=80, height=40)` for `cu_screenshot`). For each tool:

```python
res = await client.call_tool("qt_ui_screenshot", {"objectId": "MainWindow"})
assert len(res.content) == 1 and res.content[0].type == "text"
data = json.loads(res.content[0].text)
assert set(data) == {"saved_to", "width", "height", "bytes"}
assert Path(data["saved_to"]).read_bytes() == raw
assert b64 not in res.content[0].text
```

Keep one `save_to` test per tool, asserting `data["saved_to"] == str(Path(out_file).resolve())`. Add one test per tool that passing `as_image` is rejected by the tool's input schema (`call_tool` raises).

- [ ] **Step 2: Run them to verify they fail**

Run: `... python -m pytest tests/test_screenshot_efficiency.py`
Expected: FAIL. The tools still return `ImageContent` or empty data, and still accept `as_image`.

- [ ] **Step 3: Implement**

- In both tools, drop the `as_image` parameter and pass `tool="qt_ui_screenshot"` / `tool="cu_screenshot"` to the helper.
- Rewrite each docstring: the PNG is always saved, to `save_to` or a temp folder for this server run, and the reply is `{saved_to, width, height, bytes}`. To see the image, open the file at `saved_to`.
- `prompts.py` line 31: "Verify visual state with `qt_ui_screenshot()` and open the file it returns."
- Update the docs and the skill listed under Files to match: "returns a saved file path"; expect `saved_to` with non-zero `bytes`.

- [ ] **Step 4: Run the whole Python suite**

Run: `... python -m pytest tests --ignore=tests/test_launcher_signals.py`
Expected: everything passes except the 3 known TLS failures, with zero warnings.

- [ ] **Step 5: Commit**

```bash
git add python/src/qtpilot/tools/native.py python/src/qtpilot/tools/cu.py python/src/qtpilot/prompts.py python/tests docs .claude/skills/test-mcp-modes/SKILL.md
git commit -m "fix(server): qt_ui_screenshot and cu_screenshot always return a saved file path"
```

---

### Task 3: The probe connection accepts screenshot-sized messages

**Files:**
- Modify: `python/src/qtpilot/connection.py` (the `connect(...)` call in `ProbeConnection.connect`, ~line 277)
- Test: `python/tests/test_connection_message_size.py` (new)

**Interfaces:**
- Produces: `MAX_PROBE_MESSAGE_BYTES: int = 256 * 1024 * 1024` in `qtpilot.connection`.

- [ ] **Step 1: Write the failing tests**

Model the fake probe on `tests/test_connection_when_ready.py` (`websockets.asyncio.server.serve` on `127.0.0.1:0`). The fake answers any request with `{"jsonrpc": "2.0", "id": ..., "result": {"meta": {}, "result": {"image": "A" * (2 * 1024 * 1024)}}}`.

```python
@pytest.mark.asyncio
async def test_a_reply_over_one_mebibyte_arrives():
    async with serve(_big_reply, "127.0.0.1", 0) as server:
        conn = ProbeConnection(f"ws://127.0.0.1:{server.sockets[0].getsockname()[1]}")
        await conn.connect()
        try:
            resp = await conn.call("qt.ui.screenshot", {"objectId": "MainWindow"}, timeout=10)
            assert len(resp["result"]["image"]) == 2 * 1024 * 1024
        finally:
            await conn.disconnect()

def test_the_cap_is_256_mebibytes():
    assert MAX_PROBE_MESSAGE_BYTES == 256 * 1024 * 1024
```

- [ ] **Step 2: Run them to verify they fail**

Run: `... python -m pytest tests/test_connection_message_size.py`
Expected: the first test fails with the connection closed for a message that is too big (close code 1009 surfacing as a connection error), and the second with an ImportError.

- [ ] **Step 3: Implement**

Add the constant, with a one-line comment that it exists so full-window screenshots from local and remote probes fit. Pass `max_size=MAX_PROBE_MESSAGE_BYTES` in the `connect(...)` call.

- [ ] **Step 4: Run them to verify they pass**

Run the same command, then the whole suite as in Task 2 Step 4. Expected: PASS, with only the known failures elsewhere.

- [ ] **Step 5: Commit**

```bash
git add python/src/qtpilot/connection.py python/tests/test_connection_message_size.py
git commit -m "fix(server): accept probe messages up to 256 MiB so large screenshots arrive"
```

---

### Task 4: Live check against the test app

**Files:** none, unless the check finds something.

- [ ] **Step 1: Build and launch**

From the worktree, build the Debug probe, launcher and test app with Qt 5.15.1, as in `build/dbg` of the sibling worktree (`-G "Visual Studio 18 2026" -A x64 -T v143`, `QTPILOT_QT_DIR=E:/git/_hog4/hog_dependencies/win64_vc16/qt/5.15.1/debug_shared`). Launch `qtPilot-test-app.exe` through `qtPilot-launcherd.exe` with the Qt `bin` folder on `PATH`.

- [ ] **Step 2: Drive it through the branch's server code**

Write a short script in the session scratchpad, run with the `.venv-312` interpreter, that connects a `ProbeConnection` to the probe's **LAN address** (the `ws_url` discovery reports, not `localhost`). It should:
- make `MainWindow` as large as the screen allows with `qt.methods.invoke` `showMaximized` (a public slot);
- call `qt.ui.screenshot` (`fullWindow: true`) and `cu.screenshot`;
- pass each reply through `process_screenshot_response`.

Expected: both return `saved_to` files whose sizes match `bytes` and that open as valid PNGs of the reported dimensions. Report the encoded reply sizes. If neither passes 1 MiB (a flat UI compresses well), say so: the over-1 MiB path is then proven only by Task 3's test, and this check covers the end-to-end save from a LAN address.

- [ ] **Step 3: Close the app**, and record the commands and results under `logs/` per `logs/README.md`.
