---
status: diagnosed
phase: 04-computer-use-mode
source: 04-01-SUMMARY.md, 04-02-SUMMARY.md, 04-03-SUMMARY.md
started: 2026-01-31T21:00:00Z
updated: 2026-01-31T21:45:00Z
---

## Current Test

[testing complete]

## Tests

### 1. Take Screenshot
expected: Send cu.screenshot via WebSocket. Response contains result.image (base64 PNG), result.width and result.height as integers.
result: pass

### 2. Click a Button
expected: Send cu.click with x,y coordinates targeting a visible button in the test app. The button's clicked signal fires (observable effect like text change or state toggle). Response contains result.success: true.
result: pass

### 3. Type Text into Input Field
expected: Click a QLineEdit to focus it, then send cu.type with text "hello world". The text appears in the input field. Response contains result.success: true.
result: pass

### 4. Send Key Combination
expected: With text in a QLineEdit, send cu.key with key "ctrl+a" then cu.key with key "Delete". The text is selected then deleted, leaving the field empty.
result: pass

### 5. Right-Click
expected: Send cu.rightClick with x,y coordinates on a widget. Response contains result.success: true (context menu may appear depending on widget).
result: pass

### 6. Double-Click
expected: Send cu.doubleClick with x,y on a QLineEdit containing text. The word under the cursor gets selected.
result: pass

### 7. Mouse Drag
expected: Send cu.drag with startX, startY, endX, endY coordinates. The drag operation executes (e.g., moving a slider or selecting text). Response contains result.success: true.
result: pass

### 8. Scroll
expected: Send cu.scroll with x, y, direction ("down"), and amount (3). Response contains result.success: true. Scrollable content moves in the specified direction.
result: pass

### 9. Query Cursor Position
expected: Send cu.cursorPosition. Response contains result.x, result.y (integers) and result.className (string identifying the widget under cursor).
result: pass

### 10. Screenshot with Action
expected: Send cu.click with include_screenshot: true. Response contains both result.success: true AND result.screenshot (base64 PNG showing state after the click).
result: pass

## Summary

total: 10
passed: 10
issues: 3
pending: 0
skipped: 0

## Gaps

- truth: "qt.* methods (NativeModeApi) should be registered alongside cu.* methods"
  status: failed
  reason: "All qt.* methods return -32601 Method not found. Only legacy qtpilot.* and cu.* methods are registered. NativeModeApi is not being wired into the probe at runtime despite being compiled."
  severity: major
  test: discovered during UAT setup
  root_cause: "NativeModeApi constructor likely throws an exception during DLL injection that is silently swallowed. Tests pass in controlled QTest environment but fail in live injection. ComputerUseModeApi works because its lambdas only call simple InputSimulator/Screenshot functions, while NativeModeApi lambdas call complex introspection functions (serializeObjectTree, ObjectRegistry lookups) that may fail in the injected context."
  artifacts:
    - path: "src/probe/core/probe.cpp"
      issue: "NativeModeApi instantiation at line ~158 has no exception handling"
    - path: "src/probe/api/native_mode_api.cpp"
      issue: "Constructor registers lambdas calling introspection functions that may fail in DLL context"
  missing:
    - "Add try/catch with stderr logging around NativeModeApi instantiation in probe.cpp"
    - "Investigate which specific introspection call fails during DLL injection"
  debug_session: ""

- truth: "qtpilot.* object IDs returned by findByClassName should be resolvable by getObjectInfo and getGeometry"
  status: failed
  reason: "qtpilot.findByClassName returns IDs like QObject~20, but qtpilot.getObjectInfo and qtpilot.getGeometry return 'Object not found' for those same IDs."
  severity: major
  test: discovered during UAT setup
  root_cause: "Parameter name mismatch. Legacy qtpilot.getObjectInfo and qtpilot.getGeometry read param 'id' (jsonrpc_handler.cpp lines 318, 553), but the UAT test client was passing 'objectId' (the modern qt.* convention). The error message 'Object not found: ' with empty string confirms the id parameter was empty. The legacy API is internally consistent — all methods expect 'id', not 'objectId'."
  artifacts:
    - path: "src/probe/transport/jsonrpc_handler.cpp"
      issue: "Line 318: qtpilot.getObjectInfo reads doc.object()['id']; Line 553: qtpilot.getGeometry reads doc.object()['id']"
  missing:
    - "Not a code bug — test client was using wrong parameter name. Legacy API correctly expects 'id'. Could add backward compat to accept both 'id' and 'objectId' for friendlier API."
  debug_session: ""

- truth: "cu.cursorPosition should report the widget under the coordinate used in the most recent cu.mouseMove, not the physical OS cursor"
  status: failed
  reason: "cu.cursorPosition uses QCursor::pos() which reads the physical system cursor. cu.mouseMove sends QMouseEvents but does not move the OS cursor. So cursorPosition always returns wherever the physical mouse is, not where CU interactions are happening."
  severity: minor
  test: 9
  root_cause: "cu.mouseMove (line 308-329 in computer_use_mode_api.cpp) calls InputSimulator::mouseMove() which sends QMouseEvent via sendEvent but does NOT call QCursor::setPos(). cu.cursorPosition (line 581-606) reads QCursor::pos() — the physical OS cursor. These are completely disconnected. Only cu.mouseMove with screenAbsolute=true calls QCursor::setPos()."
  artifacts:
    - path: "src/probe/api/computer_use_mode_api.cpp"
      issue: "cu.cursorPosition reads QCursor::pos() (line 583); cu.mouseMove only sends QMouseEvent without tracking position"
    - path: "src/probe/interaction/input_simulator.cpp"
      issue: "mouseMove (lines 147-160) sends QMouseEvent but doesn't move physical cursor"
  missing:
    - "Add m_lastSimulatedPosition QPoint member to ComputerUseModeApi"
    - "Update cu.mouseMove, cu.click, cu.mouseDown, cu.mouseUp, cu.drag to track last position"
    - "Update cu.cursorPosition to return virtual position when simulated position exists"
  debug_session: ""
