---
status: diagnosed
phase: 03-native-mode
source: 03-01-SUMMARY.md, 03-02-SUMMARY.md, 03-03-SUMMARY.md
started: 2026-01-31T19:30:00Z
updated: 2026-01-31T19:55:00Z
---

## Current Test

[testing complete]

## Tests

### 1. qt.* Method Namespace Available
expected: Send a JSON-RPC request like qt.ping to the probe WebSocket. Should receive a valid response, not "method not found". All 29 qt.* methods should be accessible.
result: pass

### 2. Response Envelope Format
expected: Every qt.* method response should be wrapped in {result: {...}, meta: {timestamp: "ISO-8601"}}. The meta.timestamp should be a valid ISO timestamp.
result: pass

### 3. Object Discovery via qt.objects.find
expected: Calling qt.objects.find with an objectName should return matching objects. Calling qt.objects.findByClass with a class name should return objects of that type. qt.objects.tree should return the full object hierarchy.
result: pass

### 4. Object Inspection via qt.objects.info
expected: Calling qt.objects.info with an object ID should return the object's class name, object name, properties, methods, and signals. qt.objects.inspect should return detailed meta information.
result: pass

### 5. Multi-Style Object ID Resolution
expected: Objects can be referenced by hierarchical path (e.g., QMainWindow/centralWidget/QPushButton), by numeric ID (#1, #2), or by symbolic name (if registered). All three styles should resolve correctly.
result: pass

### 6. Symbolic Name Map Management
expected: qt.names.register assigns a symbolic name to an object path. qt.names.list shows all registered names. qt.names.unregister removes a name. Names auto-load from QTPILOT_NAME_MAP env var or qtPilot-names.json file.
result: issue
reported: "qt.names.register, qt.names.list, qt.names.unregister all work correctly. However, using a symbolic name (e.g., 'myStyle') as objectId in qt.objects.info returns 'Object not found' instead of resolving through the name map. The name map CRUD works, but symbolic names don't resolve when used as object IDs in other methods."
severity: major

### 7. Property Read/Write via qt.properties
expected: qt.properties.list returns all properties of an object. qt.properties.get reads a specific property value. qt.properties.set writes a new value. Changed properties should reflect immediately.
result: pass

### 8. Method Invocation via qt.methods
expected: qt.methods.list returns available methods/slots. qt.methods.invoke calls a method by name with arguments and returns the result.
result: pass

### 9. Signal Monitoring via qt.signals
expected: qt.signals.list returns available signals on an object. qt.signals.subscribe starts monitoring a signal. When the signal fires, a push notification is sent. qt.signals.unsubscribe stops monitoring.
result: pass

### 10. UI Interaction via qt.ui
expected: qt.ui.geometry returns widget position/size. qt.ui.screenshot captures a base64 PNG image. qt.ui.click simulates a mouse click on a widget. qt.ui.sendKeys simulates keyboard input.
result: pass

### 11. Structured Error Responses
expected: Sending a request with missing required params (e.g., qt.objects.info without objectId) returns a JSON-RPC error with a specific error code (not generic -32603) and a data field with hints about what's wrong.
result: pass

### 12. Backward Compatibility with qtpilot.* Methods
expected: Old-style methods like qtpilot.listObjects, qtpilot.getProperty, etc. still work alongside the new qt.* methods. No "method not found" errors for old method names.
result: pass

### 13. All Integration Tests Pass
expected: Running `ctest --test-dir build -C Debug` shows 8/8 tests passing including test_native_mode_api with 29 test cases covering all API domains.
result: pass

## Summary

total: 13
passed: 12
issues: 1
pending: 0
skipped: 0

## Gaps

- truth: "Symbolic names resolve when used as objectId in any qt.* method"
  status: failed
  reason: "User reported: qt.names.register, qt.names.list, qt.names.unregister all work correctly. However, using a symbolic name as objectId in qt.objects.info returns 'Object not found' instead of resolving through the name map."
  severity: major
  test: 6
  root_cause: "getTopLevelObjects() in object_id.cpp returns app->children() but excludes QApplication itself. ID generation includes QApplication as root segment, so findByObjectId() fails because segment[0]='QApplication' can't match any child. Symbolic names resolve to paths that hit this broken tree-walk fallback. Numeric IDs and hierarchical paths from qt.objects.tree also affected."
  artifacts:
    - path: "src/probe/introspection/object_id.cpp"
      issue: "getTopLevelObjects() excludes QCoreApplication from search roots (line ~113)"
    - path: "src/probe/core/object_registry.cpp"
      issue: "findById() cache lookup requires exact ~N suffix match (line ~282)"
  missing:
    - "Include QCoreApplication::instance() in getTopLevelObjects() search roots"
    - "Tree-walk fallback will then correctly resolve QApplication/... paths"
  debug_session: ".planning/debug/symbolic-name-resolve-fail.md"
