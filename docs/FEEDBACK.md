# qtPilot Feedback & Improvement Suggestions

Observations from real-world testing against a large Qt 6.10 application (DaVinci CAD app, ~2300 library types, multiple dock widgets, custom QMainWindow subclass).

## Bugs

- **CRASH: `findByClassName` causes SIGSEGV in ObjectRegistry (stale pointers)** — `ObjectRegistry::findAllByClassName()` crashes with `EXC_BAD_ACCESS` (pointer authentication failure) at offset +788. Reproduced 3 times against a real app. The registry holds stale raw pointers to destroyed QObjects. The crash is always at the same offset, suggesting the iteration dereferences a freed object's metaobject.

- **CRASH: `screenshot` crashes on null/empty pixmap** — `Screenshot::captureWindow()` → `QPixmap::save()` → `QImageWriter::write()` crashes when `QWidget::grab()` returns a corrupt or empty pixmap (e.g., missing permissions, minimized windows). Must check for null/empty pixmap before encoding.

- **Crash: `sendEvent` to null QObject during idle** — After extended runtime with the probe loaded, the app crashed in `QCoreApplication::sendEvent` with a null pointer dereference. The probe was not on the crash stack but was loaded. Likely caused by the probe's object hooks keeping references to destroyed objects.

- **`getProperty` returns "Property not found" for valid properties** — `listProperties` shows properties like `geometry`, `windowTitle`, etc., but `getProperty(objectId='MainWindow', propertyName='geometry')` returns `"Property not found"`. The error message contains an empty string (`"Property not found: "`) suggesting the property name isn't reaching the lookup.

- **Qt 6.10 compatibility: `QAccessible::Label` enum change** — `QAccessible::Label` moved from `Role` to `RelationFlag` in Qt 6.10. Causes `-Wenum-compare-switch` in `chrome_mode_api.cpp`. Fixed by removing the case (already covered by `StaticText`).

- **Qt 6.10 compatibility: `Q_GLOBAL_STATIC` variadic macro warning** — Qt 6.10's `Q_GLOBAL_STATIC` uses C++20 variadic macro extensions. Triggers `-Wvariadic-macro-arguments-omitted` with `-Werror -Wpedantic`. Fixed by adding `-Wno-variadic-macro-arguments-omitted`.

## Usability

- **No quick-test script for verifying the probe** — Testing manually requires `pip install websockets` and writing a Python script. A `scripts/test_connection.py` with instructions would lower the barrier.

- **`findByObjectName` returns wrong object for ambiguous names** — `findByObjectName("MainWindow")` returned a plain `QObject` inside `Core::Library`, not the actual `MainWindow` widget. Every name tested ("toolBar", "centralwidget", "statusbar", "menubar") returned the same wrong object. Real apps have internal model objects with names that collide with widget objectNames. Should return all matches, prioritize QWidget subclasses, or accept a `className` filter parameter.

- **Launcher should accept `--extra-lib-path`** — Real-world Qt apps often link against non-Qt third-party libraries. When rpaths don't resolve (e.g., worktree builds), users need a way to prepend additional library search paths.

## Architecture / Design

- **ObjectRegistry holds stale pointers — #1 blocking issue** — The `findAllByClassName` crash proves the registry iterates raw pointers without validity checks. Must use `QPointer<QObject>` or guard iteration with validity checks. This is the most critical issue for real-world use.

- **Object tree misses top-level widgets** — `getObjectTree(maxDepth=5)` returns only 15 objects (QApplication internals). Top-level widgets (QMainWindow, QDockWidget, etc.) are parentless — tracked by `QApplication::topLevelWidgets()` but absent from the tree. The walker should include `topLevelWidgets()` as synthetic children of the root.

- **`findByObjectName` resolves to model-layer, not widgets** — Searches the QObject tree but resolves to non-widget objects. Returned the same `Core::Library/QObject` path for every name tested. Workaround: use `hitTest` to discover widget paths, then use those paths directly.

- **`hitTest` is the best discovery mechanism** — Unlike `findByObjectName` and `getObjectTree`, `hitTest(x, y)` correctly resolves to deep widget paths. This is currently the only reliable way to discover the UI hierarchy. Consider adding a `topLevelWidgets` method to complement it.

- **Probe version reports 0.1.0 but CMake version is 0.3.1** — `getVersion` returns `{"version":"0.1.0"}`. Should be injected from `PROJECT_VERSION` at build time.

## What Works Well

- **JSON-RPC API is clean and well-structured** — `ping`, `echo`, `getVersion`, `getModes` all work perfectly. Error responses are JSON-RPC 2.0 compliant.
- **`hitTest` is excellent** — Correctly identifies widgets at pixel coordinates, returns full object paths usable with other APIs.
- **`getObjectInfo` and `listProperties` are thorough** — 66-70 properties per widget including geometry, visibility, class hierarchy (`superClasses`).
- **`listMethods` and `listSignals` work perfectly** — 38 methods and 11 signals on MainWindow, correctly reflecting the QMainWindow API.
- **Direct objectId addressing works** — Using `"MainWindow"` as an objectId (not via findByObjectName) correctly resolves to the actual widget.
- **All 16 unit tests pass** — Clean pass after Qt 6.10 compat fixes.

## API Test Summary

| API | Status | Notes |
|-----|--------|-------|
| `ping` | OK | |
| `echo` | OK | |
| `getVersion` | OK | Reports wrong version (0.1.0) |
| `getModes` | OK | Returns `[native, computer_use, chrome]` |
| `getObjectTree` | Partial | Only shows QApplication internals, misses widgets |
| `findByObjectName` | Broken | Returns wrong objects (model-layer, not widgets) |
| `findByClassName` | CRASH | Stale pointers in ObjectRegistry |
| `getObjectInfo` | OK | Works with direct objectIds |
| `listProperties` | OK | 66-70 properties per widget |
| `getProperty` | Broken | "Property not found" for valid properties |
| `listMethods` | OK | 38 methods on MainWindow |
| `listSignals` | OK | 11 signals on MainWindow |
| `getGeometry` | OK | Returns local + global coords with DPR |
| `hitTest` | OK | Best discovery mechanism for widgets |
| `screenshot` | CRASH | Missing null-pixmap guard |
| `click` | Untested | |
| `sendKeys` | Untested | |
