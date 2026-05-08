---
name: test-mcp-modes
description: Comprehensive end-to-end test of the qtPilot MCP server across all three modes (native, chrome, computer_use), plus logging and recording subsystems, using the test app. Use this skill whenever the user asks to test the MCP server, verify modes work, run a smoke test, check that qtPilot tools are functioning after a build or change, or wants to validate logging/recording functionality.
---

# Test qtPilot MCP Server — Full E2E Suite

Comprehensive end-to-end test of the qtPilot MCP server covering all three interaction modes (native, chrome, computer_use), the message logging subsystem, and the event recording subsystem.

## Tool surface

Native-mode consolidation merged discovery/inspection into two tools: `qt_objects_search` (filter by `objectName` / `className` / `properties`) and `qt_objects_inspect(objectId, parts=[...])` with parts `info`, `properties`, `methods`, `signals`, `qml`, `geometry`, `model`. Session state (mode, connection, discovered probes) lives in `qtpilot_status`. Recording uses `qtpilot_recording_{start,stop,status}`. Logging uses `qtpilot_log_{start,stop,status}` — tail recent entries via `qtpilot_log_status(tail=N)`.

## Prerequisites

The project must be built (`cmake --build build --config Release`). The MCP server `qtpilot` must be configured in `.mcp.json`.

## Procedure

### Phase 1: Launch, Connect, and Start Logging

1. **Find the Qt directory** from `build/CMakeCache.txt` (`QTPILOT_QT_DIR:PATH=...`).
2. **Launch the test app** in the background with Qt DLLs on PATH:
   ```
   QT_DIR=<from cache>
   cmd //c "set PATH=<QT_DIR>\bin;%PATH% && set QT_PLUGIN_PATH=<QT_DIR>\plugins && <abs_path>/build/bin/Release/qtPilot-launcher.exe <abs_path>/build/bin/Release/qtPilot-test-app.exe"
   ```
   Use `run_in_background: true`. Wait 4 seconds for startup.
3. **Discover and connect:**
   - `qtpilot_status()` — expect at least one probe under `.discovery.probes[]` (each has `ws_url`, `app_name`, `pid`, `qt_version`, `uptime`, `connected`). App name should be "qtPilot Test App".
   - `qtpilot_connect_probe(ws_url=...)` — connect using a `ws_url` from the probes list
   - `qt_ping()` — expect `pong: true`

Record: probe PID, Qt version, WebSocket URL.

4. **Start verbose logging** to capture all traffic during the test:
   - `qtpilot_log_start(level=3)` — expect `logging: true`, a file path, and `level: 3`
   - Record the log file path for later verification.

5. **Check logging status:**
   - `qtpilot_log_status()` — expect `logging: true`, correct path and level

### Phase 2: Native Mode Tests (qt_* tools)

Verify mode: `qtpilot_status()` — expect `.mode == "native"` (or `"all"`, depending on server launch args).

Run these tests, recording pass/fail for each:

1. **Object discovery:**
   - `qt_objects_search(className="QLineEdit")` — expect `{count: 2, objects: [...], truncated: false}` with hierarchical `objectId`s containing `nameEdit` and `emailEdit`
   - `qt_objects_search(className="QPushButton")` — expect 3+ results including `submitButton`, `clearButton`
   - `qt_objects_search(objectName="nameEdit")` — expect 1 result

2. **Properties:**
   - `qt_properties_get(objectId="...", name="placeholderText")` on the nameEdit — expect "Enter your name"
   - `qt_properties_get(objectId="...", name="text")` on submitButton — expect "Submit"

3. **UI interaction:**
   - `qt_ui_click(objectId=<nameEdit>)` — expect success
   - `qt_ui_sendKeys(objectId=<nameEdit>, text="Test User")` — expect success
   - `qt_ui_click(objectId=<emailEdit>)` — expect success
   - `qt_ui_sendKeys(objectId=<emailEdit>, text="test@example.com")` — expect success
   - `qt_properties_get(objectId=<nameEdit>, name="text")` — expect "Test User"

4. **Screenshot:**
   - `qt_ui_screenshot(objectId="MainWindow", fullWindow=true)` — expect base64 image data

5. **Object tree:**
   - `qt_objects_tree(maxDepth=3)` — expect nested structure with className and children

6. **Submit and clear:**
   - `qt_ui_click(objectId=<submitButton>)` — expect success
   - `qt_ui_click(objectId=<clearButton>)` — expect success
   - `qt_properties_get(objectId=<nameEdit>, name="text")` — expect "" (empty after clear)

7. **Methods:**
   - `qt_objects_inspect(objectId=<nameEdit>, parts=["methods"])` — expect `{methods:[…]}` containing `clear`, `selectAll`
   - `qt_ui_sendKeys(objectId=<nameEdit>, text="Method Test")` — fill a value first
   - `qt_methods_invoke(objectId=<nameEdit>, method="clear")` — expect `{result: null}` for this void method (null is success, not error)
   - `qt_properties_get(objectId=<nameEdit>, name="text")` — expect "" (cleared by method)

8. **Signals:**
   - `qt_objects_inspect(objectId=<nameEdit>, parts=["signals"])` — expect `{signals:[…]}` containing `textChanged`, `textEdited`
   - `qt_signals_subscribe(objectId=<nameEdit>, signal="textChanged")` — expect a `subscriptionId`
   - `qt_ui_sendKeys(objectId=<nameEdit>, text="Sig")` — trigger the signal
   - `qt_signals_unsubscribe(subscriptionId=<from above>)` — expect success

9. **Named objects (symbolic names):**
   - `qt_names_register(name="myNameField", path=<nameEdit objectId>)` — expect success
   - `qt_names_list()` — expect entry with "myNameField"
   - `qt_names_validate()` — expect all valid
   - `qt_names_unregister(name="myNameField")` — expect success

10. **Models (table data, flat model):**
    - `qt_models_list()` — expect at least 2 models (tableWidget's internal model, treeView's model). Each entry carries `rowCount` and `columnCount` — verify the tableWidget model reports `rowCount >= 3` and `columnCount >= 3`.
    - Pick the tableWidget model objectId from the list
    - `qt_models_data(objectId=<tableModel>)` — expect a rows array with display cells; the first row should contain "Alpha"

11. **Tree navigation (qt_models_data with parent, qt_models_search, qt_ui_clickItem):**
    - Switch to the Tree tab: `qt_properties_set(objectId="MainWindow/centralWidget/tabWidget", name="currentIndex", value=3)` — expect `{ok: true}`. (Clicking the tab page widget also works, but setting `currentIndex` is more robust.) The canonical tree-view path is `MainWindow/centralWidget/tabWidget/qt_tabwidget_stackedwidget/treeTab/treeView`; bare `"treeView"` works via probe fallback resolution.
    - `qt_models_data(objectId="treeView")` — expect top-level rows (ETC, Martin, BulkManufacturer), each with `hasChildren=true`, each row having a `path` field
    - `qt_models_data(objectId="treeView", parent=[0])` — expect ETC's children (fos4..., ColorSource PAR), `path` starts with `[0, ...]`
    - `qt_models_data(objectId="treeView", parent=[0, 0])` — expect fos4's children (Mode 8ch, Mode 12ch)
    - `qt_models_data(objectId="treeView", parent=[99])` — expect an MCP error call response with message `Parent path invalid at segment 0`
    - `qt_models_search(objectId="treeView", value="Aura", match="contains")` — expect at least 1 match with `path=[1, 0]` (under Martin)
    - `qt_models_search(objectId="treeView", value="Fixture 0500", match="exact", parent=[2])` — expect 1 match inside the BulkManufacturer subtree
    - `qt_ui_clickItem(objectId="treeView", itemPath=["Martin","MAC Aura XB","Mode 12ch"], action="select")` — expect `found=true`, `path=[1,0,1]`, ancestors auto-expanded
    - `qt_ui_clickItem(objectId="treeView", path=[0, 0], action="select")` — dual-addressing proof; expect `found=true`
    - Pagination: `qt_models_data(objectId="treeView", parent=[2], offset=1100, limit=100)` — expect 100 rows, `hasMore=false`, first row path `[2, 1100]`

12. **Hit test and geometry:**
    - `qt_ui_geometry(objectId=<submitButton>)` — expect response with both `local` (widget-relative) and `global` (screen) keys, each with `x`, `y`, `width`, `height`
    - `qt_ui_hitTest(x=<center_x>, y=<center_y>)` using the submitButton center in **global** (screen) coords — expect result identifying the submitButton

13. **Events:**
    - `qt_events_start()` — expect `{capturing: true}`
    - `qt_ui_click(objectId=<submitButton>)` — generate some events
    - `qt_events_stop()` — expect `{capturing: false}`. Note: this call only toggles the global event filter; it does **not** return captured events. Captured events flow via the recording subsystem (Phase 3) or signal subscriptions.

14. **QML inspect** (may return null for non-QML widgets, but should not error):
    - `qt_objects_inspect(objectId=<nameEdit>, parts=["qml"])` — expect `{"qml": null}` for a plain QWidget. `null` is a PASS (part doesn't apply), not an error.

### Phase 3: Recording Tests

Test the event recording subsystem before switching modes.

1. **Start recording:**
   - `qtpilot_recording_start(targets=[{"object_id": "<nameEdit objectId>"}])` — targets are a list of dicts; each is `{object_id, signals?, recursive?}`. Expect `{recording: true, subscriptions, targets, capture_events}`.
   - `qtpilot_recording_status()` — expect `recording: true` with `event_count` and `duration`

2. **Generate recorded events:**
   - `qt_ui_click(objectId=<nameEdit>)` — click to focus
   - `qt_ui_sendKeys(objectId=<nameEdit>, text="Record Test")` — type to generate signals/events
   - `qt_ui_click(objectId=<clearButton>)` — click clear

3. **Stop recording and verify:**
   - `qtpilot_recording_stop()` — expect `{recording: false, duration, event_count, events:[…]}`
   - Verify the `events` list contains entries (timestamps `t`, event/signal types, object IDs)

### Phase 4: Chrome Mode Tests (chr_* tools)

1. **Switch mode:** `qtpilot_set_mode(mode="chrome")` — expect `{ok: true, mode: "chrome", previous_mode: "…"}`
2. **Verify:** `qtpilot_status()` — expect `.mode == "chrome"`

Note: mode switches cause the MCP server to re-advertise a different tool list. Native `qt_*` tools become unavailable until the client refetches tools (usually next tool call). This is normal.

Run these tests:

3. **Page reading:**
   - `chr_readPage()` — expect accessibility tree with roles, names, refs
   - `chr_getPageText()` — expect visible text including labels and button text

4. **Find elements:**
   - `chr_find(query="Submit")` — expect at least 1 result with a ref

5. **Form interaction:**
   - Find a text input ref from `chr_readPage()`
   - `chr_formInput(ref=<input_ref>, value="Chrome Test")` — expect success
   - `chr_click(ref=<submit_ref>)` — expect success

6. **Tab context:**
   - `chr_tabsContext()` — expect window/tab info

7. **Console messages:**
   - `chr_readConsoleMessages(limit=3)` — accept either a successful response **or** an MCP "result exceeds maximum allowed tokens" overflow as PASS (the call itself succeeded). The current build may not honor `limit`; either outcome is acceptable.

8. **Navigate:**
   - Find a tab ref from `chr_readPage()`
   - `chr_navigate(ref=<tab_ref>, action="activateTab")` — expect success (switches tab)

### Phase 5: Computer Use Mode Tests (cu_* tools)

1. **Switch mode:** `qtpilot_set_mode(mode="cu")` — expect `{ok: true, mode: "cu", previous_mode: "…"}`
2. **Verify:** `qtpilot_status()` — expect `.mode == "cu"`

**Coordinate system note:** `cu_leftClick`, `cu_rightClick`, `cu_doubleClick`, `cu_mouseMove`, `cu_scroll` use **window-relative** coordinates by default (`screenAbsolute` omitted or `false`). Passing screen-absolute coords with `screenAbsolute=true` fails with "No widget found at screen coordinates …" when the widget is on a non-visible tab. `cu_cursorPosition` returns both `{x, y}` (window-relative) and `{screenX, screenY}`.

Run these tests:

3. **Screenshot:**
   - `cu_screenshot()` — expect base64 image data with `width`/`height`

4. **Cursor:**
   - `cu_cursorPosition()` — expect `{x, y, screenX, screenY}`

5. **Click and type** (use window-relative coordinates):
   - First switch briefly to all mode: `qtpilot_set_mode(mode="all")`
   - Ensure the form tab is visible: `qt_properties_set(objectId="MainWindow/centralWidget/tabWidget", name="currentIndex", value=0)`
   - `qt_ui_geometry(objectId=<nameEdit>)` — use the **`local`** (window-relative) fields for CU coords. (Alternatively: subtract the window origin from `global`.)
   - Switch back: `qtpilot_set_mode(mode="cu")`
   - `cu_leftClick(x=<local center_x>, y=<local center_y>)` — expect `{success: true}`
   - `cu_type(text="CU Test")` — expect success
   - `cu_key(key="Tab")` — expect success (moves to next field)

6. **Mouse operations:**
   - `cu_mouseMove(x=100, y=100)` — expect success
   - `cu_cursorPosition()` — expect `x ~100, y ~100` (window-relative)
   - `cu_rightClick(x=<local center_x>, y=<local center_y>)` — expect success (may open context menu)
   - `cu_doubleClick(x=<nameEdit local center_x>, y=<nameEdit local center_y>)` — expect success (selects word)

7. **Scroll:**
   - `cu_scroll(x=200, y=200, direction="down", amount=3)` — expect success

8. **Key combos:**
   - `cu_key(key="Ctrl+a")` — select all
   - `cu_key(key="Delete")` — delete selection
   - `cu_type(text="Final CU Test")` — type replacement text

### Phase 6: Logging Verification

1. **Tail recent log entries:**
   - `qtpilot_log_status(tail=20)` — returns status fields plus `.entries` (the most recent 20 ring-buffer entries) when `tail > 0`. Verify entries have a `dir` field (`mcp_in`, `mcp_out`, `req`, `res`) and timestamps. `req`/`res` appear only at level ≥ 2; `mcp_in`/`mcp_out` are always present.
   - Verify entries include tool calls from the preceding test phases.

2. **Stop logging:**
   - `qtpilot_log_stop()` — expect `logging: false`, a file path, `entries` count > 0, and `duration` > 0

3. **Verify log file on disk:**
   - Read the first and last few lines of the log file (path from qtpilot_log_stop)
   - Verify it is valid JSONL (one JSON object per line)
   - Verify it contains `mcp_in`, `req`, `res`, `mcp_out` entry types

### Phase 7: Cleanup

1. **Switch back to native mode:** `qtpilot_set_mode(mode="native")`
2. **Close the app:** `qt_methods_invoke(objectId="QApplication", method="quit")`
   - Expect "WebSocket closed" error — this is the PASS signal (the app exited and tore down the connection).
3. Wait for the background launcher process to exit.
4. **Clean up log file:** Delete the test log file (path from Phase 6).

### Phase 8: Report

Print a summary table:

```
## qtPilot MCP Full E2E Test Results

| Phase | Test | Result |
|-------|------|--------|
| Connect | status_probes | PASS/FAIL |
| Connect | connect_probe | PASS/FAIL |
| Connect | ping | PASS/FAIL |
| Connect | log_start | PASS/FAIL |
| Connect | log_status | PASS/FAIL |
| Native | object_discovery | PASS/FAIL |
| Native | properties | PASS/FAIL |
| Native | ui_interaction | PASS/FAIL |
| Native | screenshot | PASS/FAIL |
| Native | object_tree | PASS/FAIL |
| Native | submit_and_clear | PASS/FAIL |
| Native | methods | PASS/FAIL |
| Native | signals | PASS/FAIL |
| Native | named_objects | PASS/FAIL |
| Native | models_flat_read | PASS/FAIL |
| Native | models_tree_read | PASS/FAIL |
| Native | models_search | PASS/FAIL |
| Native | ui_clickItem | PASS/FAIL |
| Native | hit_test_geometry | PASS/FAIL |
| Native | events | PASS/FAIL |
| Native | qml_inspect | PASS/FAIL |
| Recording | start_recording | PASS/FAIL |
| Recording | generate_events | PASS/FAIL |
| Recording | stop_and_verify | PASS/FAIL |
| Chrome | switch_mode | PASS/FAIL |
| Chrome | read_page | PASS/FAIL |
| Chrome | find_elements | PASS/FAIL |
| Chrome | form_interaction | PASS/FAIL |
| Chrome | tabs_context | PASS/FAIL |
| Chrome | console_messages | PASS/FAIL |
| Chrome | navigate | PASS/FAIL |
| CU | switch_mode | PASS/FAIL |
| CU | screenshot | PASS/FAIL |
| CU | cursor_position | PASS/FAIL |
| CU | click_and_type | PASS/FAIL |
| CU | mouse_operations | PASS/FAIL |
| CU | scroll | PASS/FAIL |
| CU | key_combos | PASS/FAIL |
| Logging | log_tail | PASS/FAIL |
| Logging | log_stop | PASS/FAIL |
| Logging | log_file_verify | PASS/FAIL |
| Cleanup | quit_app | PASS/FAIL |
```

Mark a test as PASS if the tool returned the expected result type without error. Mark FAIL with a brief reason.

Report total: **X/42 tests passed**.

## Error Handling

- If the test app fails to launch (e.g., Qt DLLs not found), report the launcher output and stop.
- If a mode switch fails, skip that mode's tests and continue to the next.
- If an individual test fails, record FAIL and continue — don't abort the whole suite.
- Always attempt cleanup (Phase 7) even if earlier phases failed.

## Appendix: Known object paths

The test app exposes these hierarchical `objectId`s. Most tools also accept bare last-segment names (e.g. `"treeView"`) via probe fallback resolution, but the full path is more robust.

- `MainWindow/centralWidget/tabWidget` — set `currentIndex` (`0`=Form, `1`=List, `2`=Table, `3`=Tree) to switch tabs
- `MainWindow/centralWidget/tabWidget/qt_tabwidget_stackedwidget/formTab/{nameEdit,emailEdit,submitButton,clearButton,messageEdit,comboBox,checkBox,slider,resultGroup,spawnChildButton}`
- `MainWindow/centralWidget/tabWidget/qt_tabwidget_stackedwidget/tableTab/tableWidget/QTableModel`
- `MainWindow/centralWidget/tabWidget/qt_tabwidget_stackedwidget/treeTab/treeView`
- `QApplication` (for the quit method in cleanup)
