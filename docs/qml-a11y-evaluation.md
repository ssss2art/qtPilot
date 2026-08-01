# QML + Accessibility Evaluation

**Goal:** Assess how well qtPilot can drive and introspect **Qt Quick / QML** applications
— with particular focus on **accessibility (a11y)** — using a real QML target (a pure Qt Quick
gallery app).

This document is the working plan and findings log. Fill in the **Result** columns as we
probe the live app.

---

## 1. Target & build readiness

- **Target app:** a pure Qt Quick (QML) gallery app.
  - A prior Widgets-based gallery was exercised earlier; the QML variant is the focus here.
- **Qt version:** The target app builds against **Qt 6.11.1** (`/path/to/Qt/6.11.1`).
  The probe is injected into the target app's process, so qtPilot **must** be built against the
  same Qt minor version (ABI match). Configure with
  `-DQTPILOT_QT_DIR=/path/to/Qt/6.11.1`.
- **QML support:** `QtQml.framework` + `QtQuick.framework` + `QtQuickControls2.framework`
  are present in the Qt install → `QTPILOT_HAS_QML` will be **ON**. Confirm the configure
  log prints `Found Qt6 Qml/Quick - QML introspection enabled`.
- **Launch:** `build/bin/qtPilot-launcher --port 9222 <QtQuickApp.app>` (run in background;
  discovery on UDP 9221, WS on 9222).

---

## 2. Current capability map (from source, pre-test)

How each subsystem discovers its root objects — this predicts QML coverage:

| Subsystem | Root enumeration | Reaches QML? | Source |
|---|---|---|---|
| **Native mode** (`qt_objects_*`, `qt_properties_*`, `qt_methods_*`) | `QGuiApplication::allWindows()` → `QQuickWindow` → `contentItem` children | **Yes (expected)** | `src/probe/core/probe.cpp:131` |
| **Screenshot** (`qt_ui_screenshot`) | `QGuiApplication::primaryScreen()` grab | **Yes (expected)** | `src/probe/interaction/screenshot.cpp:84` |
| **Chrome mode + a11y walk** (`chr_*`) | `QApplication::activeWindow()` / `topLevelWidgets()` | **No (suspected)** — QWidget-only; a pure Qt Quick app has zero top-level QWidgets | `src/probe/api/chrome_mode_api.cpp:83,88` |
| **Computer-use mode** (`cu_*` window-relative) | `QApplication::activeWindow()` / `topLevelWidgets()` | **No (suspected)** for window resolution; screen-absolute coords may still work | `src/probe/api/computer_use_mode_api.cpp:44,49` |

### Key architectural gap (hypothesis)

`AccessibilityTreeWalker::walk()` takes a `QWidget*` root and the whole Chrome-mode path
resolves its root via `QApplication::topLevelWidgets()`. A **pure** Qt Quick app
(`QGuiApplication` + `QQuickWindow`, no `QApplication`) exposes **no top-level QWidgets**,
so `chr_readPage` / the a11y tree is expected to come back **empty**.

Note the *walker itself* is `QAccessibleInterface`-based, and QQuickItems **do** expose
`QAccessibleInterface` (via `Accessible.*` attached properties). So the walk algorithm
should work for QML — only the **entry point / root discovery** is widget-bound. A fix
would route through `QAccessible::queryAccessibleInterface(QWindow*)` for `QQuickWindow`s.

### QML metadata already supported

`src/probe/introspection/qml_inspector.{h,cpp}` extracts, for a `QQuickItem`:
- `qmlId` (via `QQmlContext::nameForObject`)
- `qmlFile` (source URL)
- `shortTypeName` (className with `QQuick` prefix stripped, e.g. `QQuickRectangle` → `Rectangle`)

Surfaced through `qt_objects_inspect(parts=["qml"])`.

---

## 3. Test checklist (run against live QML gallery)

Legend — **status**: ✅ works · ⚠️ partial · ❌ broken · ⬚ not yet determined
Legend — **basis**: `live` observed against a running pure-QML app (date) ·
`test` covered by a unit test in `tests/` · `src` determined by source inspection

> **Read the basis column.** A `test` result proves the handler accepts and routes
> a QML target correctly; it does not prove the end-to-end behaviour against a real
> QML control. Rows marked ⬚ need a live pure-Qt-Quick app and have not been run.

### 3a. Discovery & connection
| Step | Tool | Expected | Result | Basis |
|---|---|---|---|---|
| Status snapshot | `qtpilot_status` | mode/connection/discovered probes | ✅ | live 2026-07-08 |
| Connect | `qtpilot_connect_probe(ws_url=...)` | connected | ✅ | live 2026-07-08 |
| Ping | `qt_ping` | pong | ✅ | live 2026-07-08 |
| Version | `qt_version` | Qt 6.11.1 | ✅ | live 2026-07-08 |

Nothing here is QML-specific — the probe injects and serves identically for a
`QGuiApplication`.

### 3b. Native introspection
| Step | Tool | Expected | Result | Basis |
|---|---|---|---|---|
| Object tree reaches QQuickWindow + items | `qt_objects_tree(maxDepth=6)` | QQuickWindow → contentItem → QML items | ✅ was ❌ **I1**, fixed | test (`test_object_id`) |
| Search by QML type | `qt_objects_search(className=...)` | finds QML controls | ✅ | live 2026-07-08 |
| QML metadata | `qt_objects_inspect(parts=["qml"])` | qmlId / qmlFile / shortTypeName | ⬚ | src (`qml_inspector.cpp` implements it) |
| Properties (static) | `qt_objects_inspect(parts=["properties"])` | QML item props | ⬚ | — |
| Read/write property | `qt_properties_get` / `qt_properties_set` | e.g. text, checked, value | ✅ | live 2026-07-09 (read back a QML `checked`) |
| Geometry | `qt_ui_geometry` | item bounds (scene/screen) | ✅ was ❌, **fixed on this branch** | test (`test_qml_interaction`) |
| Invoke method | `qt_methods_invoke` | QML-invokable / slots | ⬚ | — |
| ListModel/model access | `qt_models_*` | QML list/table models | ⬚ | — |

### 3c. Accessibility (the focus)
| Step | Tool | Expected | Result | Basis |
|---|---|---|---|---|
| Semantic tree of QML window | `chr_readPage` | ARIA-style tree of QML controls | ✅ was ❌ **F1**, fixed — 43 nodes | live 2026-07-17 |
| Find by role/name | `chr_find` | QML controls with `Accessible.name` | ✅ | live 2026-07-08 |
| a11y-driven click | `chr_click(ref)` | uses accessibility action iface | ⬚ | src (action iface is type-agnostic) |
| a11y form input | `chr_formInput(ref,value)` | sets text via editableText | ⬚ | — |
| Do QML items expose roles? | inspect a11y states | role/name/state from `Accessible.*` | ✅ correct roles (checkbox/radio/textbox/combobox/tab/status) | live 2026-07-17 |
| Unlabeled vs labeled controls | — | how the fallback name chain behaves for QML | ⬚ | — |

### 3d. Interaction & visuals
| Step | Tool | Expected | Result | Basis |
|---|---|---|---|---|
| Native click | `qt_ui_click` | clicks QML control | ✅ was ❌, **fixed on this branch** | test (`test_qml_interaction`) |
| Send keys | `qt_ui_sendKeys` | types into focused QML input | ✅ was ❌, **fixed on this branch** | test (`test_qml_interaction`) |
| Hit test | `qt_ui_hitTest` | resolves the QML item at a point | ✅ was ❌, **fixed on this branch** | test (`test_qml_interaction`) |
| Screenshot (window) | `qt_ui_screenshot(fullWindow=true)` | renders QML scene | ✅ | test (`test_qml_screenshot`) + live 2026-07-17 |
| Screenshot (item) | `qt_ui_screenshot(objectId=...)` | crops a QML item | ✅ | test (`test_qml_screenshot`) |
| Computer-use click | `cu_leftClick` | drives the QQuickWindow | ✅ | live 2026-07-09 |

### 3e. Events & signals
| Step | Tool | Expected | Result | Basis |
|---|---|---|---|---|
| Subscribe to QML signal | `qt_signals_subscribe` | fires on property change / signal | ⬚ | — |
| Event capture | `qt_events_start` / `qt_events_stop` | QML input events | ⬚ | — |

### What is still genuinely unknown

Eight rows remain ⬚. They fall into two groups:

1. **Needs a live pure-QML app** — QML metadata, property listing, method
   invocation, model access, `chr_click`/`chr_formInput` against QML, the
   unlabeled-control name chain, and both signal/event rows. None of these is
   known to be broken; they have simply never been exercised against Qt Quick.
2. **No blocking defect suspected** — every one of these paths is type-agnostic
   in source (they operate on `QObject`, not `QWidget`), unlike the four
   handlers fixed on this branch, which cast to `QWidget` explicitly.

Closing these requires running the checklist against a pure Qt Quick target. The
Luminol gallery referenced in earlier sessions is a **QWidgets** app and cannot
serve as that target.


---

## 4. Findings & recommended fixes

### Live validation — 2026-07-08 (the QML gallery app, pure Qt Quick, Qt 6.11.1)

Target: `a pure Qt Quick gallery app bundle`
(links QtQuick/QtQml, **no** QtWidgets → confirmed pure QML). Probe injected via launcher on ws://localhost:9222.

| Path | Reaches QML window? | Reaches QML items? | Result |
|---|---|---|---|
| Native `qt_objects_search` | n/a | **Yes** — found 3× `AppButton_QMLTYPE_2` (registry tracks all QObjects via hooks) | ✅ |
| Native `qt_objects_tree` | **No** — tree shows only `QGuiApplication` + direct QObject children; `QQuickWindow` absent | — | ❌ **I1** |
| **Chrome `chr_readPage`** | **Yes** — 11 interactive nodes, root `role:window` "the QML gallery" | **Yes** — buttons/checkboxes/switches/scrollbars with roles, bounds, `states` (checked/disabled), bridged objectIds | ✅ **F1 FIXED** |
| **Chrome `chr_find`** | Yes | Yes — found "Switch to Dark" button | ✅ |

**F1 fix confirmed working live.** Before the fix `chr_*` threw `kNoActiveWindow` on this app.

### Confirmed gaps (post-F1)
- [x] **F1 — Chrome/a11y blind to pure QML** → FIXED & validated (QWindow-aware discovery + `walk(QObject*)`).
- [ ] **I1 — native `qt_objects_tree` can't see the QQuickWindow.** Top-level `QWindow`s have
  `parent()==nullptr`, so the parent-based tree walk rooted at the app never reaches them.
  (Native *search* still works because the registry is hook-based, not tree-based.)
  Fix: enumerate `QGuiApplication::topLevelWindows()` as additional tree roots — mirrors the
  chrome-mode fix. **Newly confirmed live; strong follow-up candidate.**
- [ ] **Computer-Use mode (`cu.*`)** still QWidget-bound (`getActiveWindow()` + `QWidget::childAt`).
  Needs a `QWindow`/`QQuickItem` hit-test path.
- [ ] **F12 — QML object IDs don't round-trip** (`matchesSegment` vs `generateIdSegment`).

### Recommended fixes (priority order)
1. [x] QML-aware root discovery for Chrome/a11y — `getActiveWindowObject()` + `walk(QObject*)`.
2. [x] I1: top-level `QWindow`s as native tree roots.
3. [x] F12: reconcile `matchesSegment` with `generateIdSegment`.
4. [x] Computer-Use `QWindow`/`QQuickWindow` path.

### Computer-Use validation — 2026-07-09 (the QML gallery app)
Full `cu.*` path made QWindow-aware end to end (InputSimulator + Screenshot QWindow
overloads; `CuTarget` dispatch). Events are delivered with
`QCoreApplication::sendEvent(QQuickWindow, ...)` at scene coords (Qt Quick routes to the
item); screenshots use `QQuickWindow::grabWindow()`. Verified live: `cu.leftClick` on a
QML `a QML switch control` flipped its `checked` state; `cu.cursorPosition` reported the
`QQuickWindow` with correct screen→window mapping. All 16 tests pass.

**Branch complete:** F1 + I1 + F12 + CU path — the QML discovery, introspection, a11y,
and interaction surfaces all now work against pure Qt Quick apps.

---

## 5. Notes
- Prefer `qt_names_register` friendly names over text-derived objectIds (QML ids may be
  more stable; verify).
- Rebuild qtPilot against the target app's current Qt before probing (re-check with
  `grep CMAKE_PREFIX_PATH build/macos-local/CMakeCache.txt` in the target app repo).

---

## Findings (live audit, 2026-07-17)

Ran against a pure Qt Quick gallery built with Qt 6.11.1.

- **A11y introspection: works.** `chr.readPage` returned the full accessibility
  tree for the pure-QML app (43 nodes) with correct roles (checkbox/radio/
  textbox/combobox/tab/status) and names. The QML-aware Chrome-mode root
  discovery on this branch resolves the earlier "widget-only entry point" gap.
- **Screenshot / geometry were widget-only (fixed for screenshot).** The
  `qtpilot.screenshot`, `qtpilot.getGeometry`, `qtpilot.hitTest`, and
  `qtpilot.click`/`sendKeys` handlers `qobject_cast<QWidget*>` and throw
  *"Object is not a widget"* for any `QQuickItem`. Fixed `qtpilot.screenshot`:
  it now routes `QQuickWindow`/`QQuickItem` to the existing `Screenshot`
  `QWindow` overloads (`QQuickWindow::grabWindow()`, cropping an item to its
  scene rect). Also relaxed `grabWindowPixmap` from `isExposed()` to
  `isVisible()` so an occluded / background-Space window still grabs its real
  scene graph (offscreen) instead of falling back to a screen-region grab.
  **Resolved 2026-08-01** — see below.

---

## Findings (2026-08-01) — the remaining widget-only handlers

Branch: `analysis/qml-a11y-results`. Closes the "Still TODO" left by the
2026-07-17 audit and fills in §3's empty Result columns.

### What was still broken

Four probe handlers in `src/probe/transport/jsonrpc_handler.cpp` still did an
unconditional `qobject_cast<QWidget*>` and threw *"Object is not a widget"* for
any QML target. Verified still true on `main` at `c51dfdf`:

| Handler | MCP tool | Was |
|---|---|---|
| `qtpilot.click` | `qt_ui_click` | ❌ widget-only |
| `qtpilot.sendKeys` | `qt_ui_sendKeys` | ❌ widget-only |
| `qtpilot.getGeometry` | `qt_ui_geometry` | ❌ widget-only |
| `qtpilot.hitTest` | `qt_ui_hitTest` | ❌ widget-only |

`qtpilot.screenshot` had already been routed (2026-07-17) and served as the
template.

### What changed

- **`HitTest`** gained QML/QWindow equivalents of its widget entry points:
  `windowGeometry()`, `itemGeometry()`, `itemAt()`, `quickItemIdAt()`.
  `itemGeometry()` also reports a **`scene`** rect — window-local coordinates,
  the space Qt Quick input events actually use — which the widget shape has no
  equivalent for and which a caller needs in order to synthesise a click.
- **`InputSimulator`** gained `sendKeySequence(QWindow*)`. The QWindow overloads
  added in the Computer-Use branch covered `mouseClick`, `sendText`, and
  `sendKey`, but not key *sequences*, so `qt_ui_sendKeys(sequence=...)` had no
  QML path even after routing.
- **The four handlers** now branch widget → QQuickWindow → QQuickItem, mirroring
  `qtpilot.screenshot`. Item-relative positions are mapped through
  `mapToScene()` before delivery, because a `QQuickWindow` only accepts scene
  coordinates.
- **`hitTest`** with no `parentId` now falls back to scanning top-level
  `QQuickWindow`s when the widget hit test finds nothing — a pure Qt Quick app
  has no widget anywhere for the original path to hit.

### Decisions worth recording

- **`sendKeys` on a `QQuickItem` calls `forceActiveFocus()` first.** The QWidget
  path calls `setFocus()`; without the equivalent, text would land on whichever
  item happened to already hold focus rather than the requested one.
- **An unrendered `QQuickItem` reports `global: null`,** not `0,0`. It has valid
  local and scene coordinates but no screen position, and a zero origin is a
  plausible-looking value a caller could act on incorrectly.
- **Hit testing walks children last-first.** Later siblings paint on top in Qt
  Quick, so the last match in child order is the one visually under the point.
- **Errors now say "not a widget or QML item"** rather than "not a widget", so a
  genuine non-visual target is not mistaken for the old widget-only limitation.

### Verification

`tests/test_qml_interaction.cpp` — 14 cases across the four handlers, covering
the success paths, the non-visual rejection, and the unattached/unrendered item
cases. Full suite: **19/19 passing** against Qt 6.11.1 with `QTPILOT_HAS_QML=ON`.

These are unit tests against synthetic `QQuickWindow`/`QQuickItem` objects. They
prove routing and coordinate mapping; they do **not** substitute for a live run
against real QML controls, which is what the remaining ⬚ rows in §3 need.
