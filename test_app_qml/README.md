# qtPilot QML Model Test App

A pure Qt Quick app (`QGuiApplication` + `QQmlApplicationEngine`, **no
QtWidgets**) whose views are backed by real `QAbstractItemModel` instances.

It exists because `qt_models_*` could not be verified against QML: the widget
test app doesn't cover it, and most QML demo apps build their views from
`ListElement` or JS arrays, which create no Qt model for the probe to find.

## Building

Qt 6 only (`qt_add_qml_module` has no Qt 5 equivalent). Built automatically when
tests/test-app are enabled and QML support is detected:

```bash
cmake -B build -DQTPILOT_QT_DIR=/path/to/Qt/6.x/<spec> -DQTPILOT_BUILD_TEST_APP=ON
cmake --build build --target qtPilot_test_app_qml
```

## Running

```bash
build/bin/qtPilot-launcher --port 9222 build/bin/qtPilot-test-app-qml.app   # macOS
build/bin/qtPilot-launcher --port 9222 build/bin/qtPilot-test-app-qml       # Linux/Windows
```

## What each model covers

| Model | `objectName` | Covers |
|---|---|---|
| `TaskListModel` | `taskModel` | flat list, 4 custom roles (`title`, `owner`, `priority`, `done`) with mixed types |
| `FileTreeModel` | `treeModel` | 2-level hierarchy — `parent` path navigation, recursive search |
| `LazyLogModel` | `lazyModel` | `canFetchMore`/`fetchMore` — 500 rows, only 5 loaded initially |

Contents are fixed and deterministic so results can be asserted against.

## Exercising `qt_models_*`

Note the parameter names — `parent` (not `parentPath`), and search uses
`value`/`match`/`role` (not `query`/`mode`). Passing the wrong name is silently
accepted and changes the result: a search with no `value` matches every row.

```jsonc
// Discover models. lazyModel starts partially loaded (rowCount < 500).
qt.models.list {}

// Custom roles by name, with types preserved.
qt.models.data {"objectId":"taskModel","roles":["title","owner","priority","done"],"limit":2}
//   -> cells: {"title":"Write the spec","owner":"ada","priority":1,"done":true}, path [0]

// Descend into the tree: parent [0] is "src".
qt.models.data {"objectId":"treeModel","parent":[0],"roles":["name","size"]}
//   -> main.cpp (1200) at [0,0], probe.cpp (4800) at [0,1]

// Recursive search finds a nested node.
qt.models.search {"objectId":"treeModel","value":"probe","match":"contains","role":"name"}
//   -> probe.cpp at path [0,1]

// Role-scoped search across a flat list.
qt.models.search {"objectId":"taskModel","value":"ada","match":"exact","role":"owner"}
//   -> 2 matches, paths [0] and [2]

// The lazy case: the sentinel lives at row 400, far past what any view renders.
// A lazy-aware search must call fetchMore itself to reach it.
qt.models.search {"objectId":"lazyModel","value":"sentinel-needle","match":"exact","role":"message"}
//   -> 1 match at path [400]; lazyModel rowCount afterwards is 500
```

The last one is the sharpest check: re-run `qt.models.list` before and after and
the row count should climb (55 → 500 in a sample run). If a future change makes
search stop driving `fetchMore`, that search returns zero matches.

## Note on QML's internal model

`qt.models.list` also reports a `QQmlTreeModelToTableModel`. That is Qt's own
adapter, created by `TreeView` to flatten `treeModel` for display — not
something this app defines. Seeing it is correct.
