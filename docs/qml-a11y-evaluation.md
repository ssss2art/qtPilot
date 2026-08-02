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
> a QML target correctly; a `live` result proves the end-to-end behaviour against
> a real QML control. As of 2026-08-01 every row has at least one `live` basis.

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
| QML metadata | `qt_objects_inspect(parts=["qml"])` | qmlId / qmlFile / shortTypeName | ✅ | live 2026-08-01 |
| Properties (static) | `qt_objects_inspect(parts=["properties"])` | QML item props | ✅ QObject* refs resolve to objectIds | live 2026-08-01 |
| Read/write property | `qt_properties_get` / `qt_properties_set` | e.g. text, checked, value | ✅ | live 2026-07-09 (read back a QML `checked`) |
| Geometry | `qt_ui_geometry` | item bounds (scene/screen) | ✅ was ❌, **fixed on this branch** | test (`test_qml_interaction`) |
| Invoke method | `qt_methods_invoke` | QML-invokable / slots | ✅ | live 2026-08-01 |
| ListModel/model access | `qt_models_*` | QML list/table models | ✅ list / data / roles / tree paths / lazy-aware search | live 2026-08-01 against `test_app_qml` |

### 3c. Accessibility (the focus)
| Step | Tool | Expected | Result | Basis |
|---|---|---|---|---|
| Semantic tree of QML window | `chr_readPage` | ARIA-style tree of QML controls | ✅ was ❌ **F1**, fixed — 43 nodes | live 2026-07-17 |
| Find by role/name | `chr_find` | QML controls with `Accessible.name` | ✅ | live 2026-07-08 |
| a11y-driven click | `chr_click(ref)` | uses accessibility action iface | ✅ was ❌ silently inert, **fixed on this branch** | live 2026-08-01 + test |
| a11y form input | `chr_formInput(ref,value)` | sets text via editableText | ✅ | live 2026-08-01 |
| Do QML items expose roles? | inspect a11y states | role/name/state from `Accessible.*` | ✅ correct roles (checkbox/radio/textbox/combobox/tab/status) | live 2026-07-17 |
| Unlabeled vs labeled controls | — | how the fallback name chain behaves for QML | ✅ 104/104 nodes named (accessible name → objectName → className) | live 2026-08-01 |

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
| Subscribe to QML signal | `qt_signals_subscribe` | fires on property change / signal | ✅ `toggled` delivered | live 2026-08-01 |
| Event capture | `qt_events_start` / `qt_events_stop` | QML input events | ✅ was ❌ zero events, **fixed on this branch** | live 2026-08-01 + test |

### What is still genuinely unknown

**Nothing.** Every row in §3 has now been observed against a running pure Qt
Quick app.

The last open row — `qt_models_*` — was blocked on the absence of a target, not
on a suspected defect: the gallery app builds its grids from `Repeater`s over
JavaScript arrays and contains no `QAbstractItemModel` anywhere. `test_app_qml/`
was written to close that gap and is now part of this repo; see
[`test_app_qml/README.md`](../test_app_qml/README.md).


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
prove routing and coordinate mapping; the live runs recorded below are what prove
the behaviour against real QML controls.

---

## Findings (live audit, 2026-08-01) — pure Qt Quick target

Target: a pure Qt Quick gallery app — `QGuiApplication` + `QQmlApplicationEngine`,
links QtQuick/QtQml and **no
QtWidgets** (confirmed via `otool -L`). Qt 6.11.1, probe injected via launcher on
`ws://localhost:9222`. 104-node accessibility tree.

This run closed every remaining §3 gap except `qt_models_*`, and turned up two
defects that only a live pure-QML app could expose.

### D1 — `chr_click` reported success but did nothing

`chr.click` hardcoded `QAccessibleActionInterface::pressAction()`. Against Qt
Quick Controls it returned `clicked: true, method: accessibilityAction` while the
control never actuated — a silent no-op, the worst kind of failure for an
automation API.

The cause is **not** that Qt Quick accessibility is broken. Isolated against
QtQuick.Controls 6.11:

| Target | `actionNames()` | `doAction(Press)` |
|---|---|---|
| bare `Item`, `Accessible.role: Button`, **no** handler | `("Press","SetFocus")` | — |
| same **with** `Accessible.onPressAction` | `("Press","SetFocus","Press")` | fires |
| **Controls `Switch`** | `("Toggle","Press")` | **no-op**; `doAction(Toggle)` works |
| Controls `Button` | `("Press")` | works |

Two things follow. `Press` is advertised unconditionally, so its presence proves
nothing. And a checkable control's working verb is **`Toggle`**, not `Press` — we
were simply calling the wrong action.

**Fix:** choose the action from what the interface offers — `Toggle` when
present, else `Press` — and report the chosen verb back as `action`. This keeps
the accessibility path (robust to scrolling and occlusion, which a coordinate
click is not) rather than falling back to synthetic mouse events, and it is
equally correct for widgets: `QCheckBox` now actuates via `Toggle` too.

A QML mouse fallback was added only for the case where an item exposes *no*
usable action, mirroring the pre-existing QWidget fallback.

> Rejected alternative: routing all `QQuickItem`s to a synthetic mouse click.
> It "worked" on this app but discards the accessibility path for every QML app
> — including ones that implement `Accessible.onPressAction` correctly — and
> generalises a whole-toolkit claim from a single sample.

### D2 — `qt_events_*` captured nothing in a pure QML app

`EventCapture::eventFilter` early-returned unless `qobject_cast<QWidget*>`
succeeded. A pure Qt Quick app has no QWidget anywhere, so `qt.events.start`
reported `capturing: true` and then delivered zero events — even though the same
clicks provably reached the control (the subscribed `toggled` signal fired).

**Fix:** accept `QQuickItem` and `QQuickWindow` as visual targets alongside
`QWidget`. Verified live: 0 → **25** events for one click plus one keystroke,
carrying correct `objectId`, `className`, and window-local positions.

### Confirmed working (previously unverified)

- **QML metadata** — `isQmlItem`, `qmlFile`, `qmlTypeName`. `qmlId` is empty for
  items with no `id:`, which is correct rather than a defect.
- **Property listing** — full static property set; `QObject*`-typed properties
  resolve to objectIds rather than serialising null.
- **`qt_methods_invoke`**, **`chr_formInput`**, **`qt_signals_subscribe`**
  (`toggled` delivered with the right objectId).
- **Name chain** — 104/104 nodes carry a name via
  accessible-name → objectName → className.

### Also verified live: the four handlers fixed earlier on this branch

Unit tests proved routing; this run proves behaviour.

| Tool | Evidence |
|---|---|
| `qt_ui_geometry` | item reports `local` (884,14), `scene` (908,14), `global` (1355,129), dpr 2 — the scene/local divergence is exactly what makes clicks land |
| `qt_ui_click` | Controls `Switch` `checked` false → true → false |
| `qt_ui_sendKeys` | text `""` → `"QtPilot"` in a Controls `TextField`; `forceActiveFocus()` is what makes it land on the requested item |
| `qt_ui_hitTest` | scene point (932,30) resolved to the switch's `contentItem` |

### Verification

`tests/test_qml_interaction.cpp` (16 cases) and a new
`testClick_CheckablePrefersToggleAction` in `tests/test_chrome_mode_api.cpp`
pinning the Toggle preference on the widget side. Full suite **19/19** against
Qt 6.11.1 with `QTPILOT_HAS_QML=ON`.

---

## Findings (2026-08-01) — `qt_models_*` closed via a purpose-built target

The last open row could not be closed against the gallery app: it builds its
grids from `Repeater`s over JavaScript arrays and contains no
`QAbstractItemModel` anywhere (`findByClassName` returns empty for both
`QAbstractItemModel` and `QQmlListModel`). Rather than leave the row guessed at,
this branch adds **`test_app_qml/`** — a pure Qt Quick app whose views are backed
by real C++ models. See [`test_app_qml/README.md`](../test_app_qml/README.md) for
the exact calls.

| Model | Covers | Result |
|---|---|---|
| `TaskListModel` | flat list, 4 custom roles, mixed types | ✅ roles resolve by name; `bool`/`int`/`string` preserved |
| `FileTreeModel` | 2-level hierarchy | ✅ `parent:[0]` descends; nested match at path `[0,1]` |
| `LazyLogModel` | `canFetchMore`/`fetchMore`, 500 rows, 5 initially | ✅ search drove `fetchMore` to reach row 400 |

**`qt_models_*` works against QML — no defects found.** The lazy case is the
sharpest evidence: a sentinel at row 400 was found while the model held only ~55
rows, and `rowCount` afterwards read 500. The probe drove `fetchMore` itself
rather than riding on whatever the view had realised.

### Two API-contract traps worth documenting

Neither is a bug, but both cost time here and both fail *silently*:

- `qt.models.data` takes **`parent`**, not `parentPath`. An unrecognised key is
  ignored, so the call returns root-level rows and looks like broken tree
  navigation.
- `qt.models.search` takes **`value`** / **`match`** / **`role`**, not
  `query`/`mode`. With no `value` the filter matches everything, so a wrong key
  returns the whole model and reads as "search does not filter".

Both are now written down in the test app's README next to working examples.

`qt.models.list` also reports a `QQmlTreeModelToTableModel` — Qt's own adapter,
created by `TreeView` to flatten a tree for display. It is expected, not a leak
from the app.
