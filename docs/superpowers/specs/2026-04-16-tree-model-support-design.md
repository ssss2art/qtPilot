# qtPilot Tree Model Support — Design

**Supersedes (in part):** `docs/qtpilot-tree-model-support-spec.md` — that spec proposed five tools; this design trims to three and clarifies semantics.

## Goal

Enable an agent to find and act on items in a Qt tree/table/list view at arbitrary depth, including models with thousands of rows.

## Scope

Three tools, all **native mode only**:

1. **Extend** `qt_models_data` — replace `parentRow`/`parentCol` with `parent=[int]` row path; preserve `offset`/`limit` pagination at any depth.
2. **New** `qt_models_find` — Qt-native recursive search with match modes, scoped to an optional subtree.
3. **New** `qt_ui_clickItem` — one-shot select/click/edit by text path **or** row path, with auto-expand/scroll.

**Works on:** QWidget views (`QTreeView`, `QTableView`, `QListView`, `QComboBox`) and QML views whose model is a C++ `QAbstractItemModel`. Pure JS-array QML models are out of scope.

**Non-goals (cut from the original spec):**

- `qt_models_select` — subsumed by `qt_ui_clickItem`.
- `qt_models_selection` — verification via `qt_models_data` re-read or screenshot is sufficient.
- Exposing `QModelIndex` across the JSON-RPC boundary — row paths remain the sole serializable proxy.
- Selection-model reads (`selectedIndexes`, `selectedRows`) — deferred.

## Design Decisions

- **Row paths are the proxy for `QModelIndex`.** An integer array `[0, 2, 1]` uniquely addresses any node. Column is always 0 for path addressing; column is a separate parameter where needed.
- **Text paths are the proxy for human intent.** A string array `["ETC", "fos4 Fresnel Lustr X8 direct"]` is how an agent thinks about an item. Unlike Squish's dot-delimited string, the array avoids separator-ambiguity (item text can contain any character).
- **Exact match only for text path segments.** Per-segment fuzzy match is ambiguous for click actions (five "Fres…" items ⇒ which one?). Fuzzy discovery goes through `qt_models_find`, which returns row paths the agent then passes back via `qt_ui_clickItem(path=[...])`.
- **Replace, don't duplicate, `parentRow`/`parentCol`.** The Python MCP wrapper never exposed them, so no agent breaks. qtPilot is v0.3.0 with no external API freeze.
- **Lazy models must work.** The probe calls `canFetchMore`/`fetchMore` at every descending level. `QFileSystemModel` and similar lazy models navigate transparently.

## Tool 1: `qt_models_data` (extended)

**JSON-RPC method:** `qt.models.data`

| Param | Type | Default | Notes |
|-------|------|---------|-------|
| `objectId` | string | — | Required. View or model objectId. |
| `parent` | int[] | `[]` | Row path to parent node. `[]`=root, `[0]`=first row's children, `[0,2]`=third child of first row. |
| `offset` | int | 0 | Paging offset among children of `parent`. |
| `limit` | int | auto | `-1` or omitted → all if ≤100 rows, else 100. |
| `roles` | (string\|int)[] | `["display"]` | Roles to fetch per cell. |

**Response:**

```jsonc
{
  "parent": [0],
  "rows": [
    {
      "path": [0, 0],
      "cells": [
        {"display": "ETC"},
        {"display": "fos4 Fresnel Lustr X8 direct"}
      ],
      "hasChildren": false
    }
  ],
  "totalRows": 5243,
  "totalColumns": 11,
  "offset": 0,
  "limit": 100,
  "hasMore": true
}
```

**Errors:**

- `kNotAModel` — `objectId` resolves to neither a model nor a view-with-model.
- `kInvalidParentPath` (new) — segment of `parent` references non-existent row. Detail: `{ path, failedSegment }`.

**C++ signature change** in `ModelNavigator`:

```cpp
// Before:
static QJsonObject getModelData(QAbstractItemModel*, int offset, int limit,
                                const QList<int>& roles, int parentRow, int parentCol);

// After:
static QJsonObject getModelData(QAbstractItemModel*, const QList<int>& parentPath,
                                int offset, int limit, const QList<int>& roles);
```

## Tool 2: `qt_models_find`

**JSON-RPC method:** `qt.models.find`

| Param | Type | Default | Notes |
|-------|------|---------|-------|
| `objectId` | string | — | Required. View or model objectId. |
| `value` | string | — | Required. Search term. For `regex`, a PCRE-compatible pattern. |
| `column` | int | 0 | Column whose cell data is matched. |
| `role` | string\|int | `"display"` | Role to match against. |
| `match` | string | `"contains"` | `exact`, `contains`, `startsWith`, `endsWith`, `regex`. Case-insensitive. |
| `maxHits` | int | 10 | Cap on results. `-1` = unlimited. |
| `parent` | int[] | `[]` | Restrict search to this subtree. |

**Response:**

```jsonc
{
  "matches": [
    {
      "path": [0, 2, 1],
      "cells": [
        {"column": 0, "display": "ETC"},
        {"column": 1, "display": "fos4 Fresnel Lustr X8 direct"}
      ]
    }
  ],
  "count": 1,
  "truncated": false      // true if maxHits was hit; more matches may exist
}
```

**Implementation — custom recursive walker (not `QAbstractItemModel::match()`):**

Qt's `match()` does not call `fetchMore`, so using it against a lazy model yields false negatives. The probe implements its own depth-first walker with this contract:

```
findRecursive(model, parentIdx, value, column, role, match, maxHits, results):
    ensureFetched(model, parentIdx)           // canFetchMore + fetchMore
    for row in 0 .. model->rowCount(parentIdx) - 1:
        cellIdx = model->index(row, column, parentIdx)
        if matches(model->data(cellIdx, role), value, match):
            results.append(indexToRowData(model, cellIdx.sibling(row, 0)))
            if results.size() == maxHits: return TRUNCATED
        childParent = model->index(row, 0, parentIdx)
        if model->hasChildren(childParent):
            if findRecursive(..., childParent, ...) == TRUNCATED: return TRUNCATED
    return COMPLETE
```

**Guarantees:**

- `ensureFetched` is called **before** every `rowCount`/`index` call on a given parent. Lazy models with `canFetchMore=true` have their children loaded before enumeration. No false negatives.
- Traversal is bounded by `maxHits`. `truncated=true` ⇔ walker aborted early; `false` ⇔ entire subtree scanned.
- `maxHits=-1` disables the cap (full walk). Agent is responsible for bounding cost.
- `matches()` applies the selected mode (`exact`, `contains`, `startsWith`, `endsWith`, `regex`) case-insensitively. Regex compilation happens once before the walk.
- `parent=[]` starts the walk at the invalid root index; `parent=[...]` resolves via `pathToIndex` first (which also calls `ensureFetched` at each segment).

The walker lives in `ModelNavigator` alongside `pathToIndex`.

**Errors:**

- `kNotAModel`, `kInvalidParentPath` — as in Tool 1.
- `kModelRoleNotFound` — role name unresolved.
- `kInvalidRegex` (new) — `match="regex"` with malformed `value`.

## Tool 3: `qt_ui_clickItem`

**JSON-RPC method:** `qt.ui.clickItem`

| Param | Type | Default | Notes |
|-------|------|---------|-------|
| `objectId` | string | — | Required. View (`QAbstractItemView` subclass or `QComboBox`). |
| `itemPath` | string[] | — | Text at each level. Exactly one of `itemPath`/`path` required. Case-sensitive exact match. |
| `path` | int[] | — | Row path from `qt_models_find` or `qt_models_data`. |
| `column` | int | 0 | Column the **action** operates on. With `itemPath`, this is also the column whose display text is matched at each level. With `path`, it selects which cell of the addressed row is acted upon. |
| `action` | string | `"click"` | `select`, `click`, `doubleClick`, `edit`. |
| `editColumn` | int | `column` | Override the action column for `edit` (opens editor on this column instead of `column`). |
| `expand` | bool | true | Expand ancestor nodes (QTreeView only; no-op elsewhere). |
| `scroll` | bool | true | `view->scrollTo(index)` before acting. |

**Response:**

```jsonc
{
  "found": true,
  "path": [0, 0],
  "itemPath": ["ETC", "fos4 Fresnel Lustr X8 direct"],
  "action": "edit",
  "cells": [
    {"column": 0, "display": "ETC"},
    {"column": 1, "display": "fos4 Fresnel Lustr X8 direct"},
    {"column": 5, "display": "0"}
  ]
}
```

**Implementation outline:**

```cpp
QAbstractItemView* view = resolveView(objectId);
QAbstractItemModel* model = view->model();

// 1. Resolve the row index (column 0 always, for row identity).
QModelIndex rowTarget = has("itemPath")
    ? textPathToIndex(model, itemPath, column)   // fetchMore + exact match each level, matching against `column`
    : pathToIndex(model, path);                  // fetchMore each level; column-0 siblings used as parents
if (!rowTarget.isValid()) return error(kItemNotFound, buildNotFoundDetail(...));

// 2. Validate the action column against the target row's parent.
const int colCount = model->columnCount(rowTarget.parent());
if (column < 0 || column >= colCount)
    return error(kInvalidColumn, {{"column", column}, {"columnCount", colCount}});

// 3. Compute the action target (the actual cell to act on).
QModelIndex actionTarget = model->index(rowTarget.row(), column, rowTarget.parent());

if (expand) {
    if (auto* tree = qobject_cast<QTreeView*>(view)) {
        for (auto p = actionTarget.parent(); p.isValid(); p = p.parent()) tree->expand(p);
    }
}
if (scroll) view->scrollTo(actionTarget);

switch (action) {
  case Select:      view->setCurrentIndex(actionTarget); break;
  case Click:       view->setCurrentIndex(actionTarget);
                    InputSimulator::mouseClick(view->viewport(), Left,
                                               view->visualRect(actionTarget).center()); break;
  case DoubleClick: view->setCurrentIndex(actionTarget);
                    InputSimulator::mouseDoubleClick(view->viewport(), Left,
                                                     view->visualRect(actionTarget).center()); break;
  case Edit: {
      const int editCol = has("editColumn") ? editColumn : column;
      if (editCol < 0 || editCol >= colCount)
          return error(kInvalidColumn, {{"editColumn", editCol}, {"columnCount", colCount}});
      QModelIndex editIdx = model->index(rowTarget.row(), editCol, rowTarget.parent());
      if (!(model->flags(editIdx) & Qt::ItemIsEditable))
          return error(kNotEditable, {{"path", asPath(editIdx)}, {"editColumn", editCol}});
      view->setCurrentIndex(editIdx); view->edit(editIdx);
      break;
  }
}
```

**Parameter validation (order):**

1. Exactly one of `itemPath`/`path` required → else `kInvalidParams`.
2. Resolve `rowTarget`. Failure → `kItemNotFound`.
3. `column` in `[0, columnCount(rowTarget.parent()))` → else `kInvalidColumn` with `{ column, columnCount }`.
4. (for `edit`) `editColumn` in same range → else `kInvalidColumn` with `{ editColumn, columnCount }`.
5. (for `edit`) `flags(editIdx) & Qt::ItemIsEditable` → else `kNotEditable` with `{ path, editColumn }`.

**Departures from original spec:**

- Clicks dispatched via existing `InputSimulator` (not `QTest::mouseClick`) — consistent with `qt_ui_click`. `InputSimulator::mouseDoubleClick` already exists ([input_simulator.h:52](src/probe/interaction/input_simulator.h:52)); no new helper needed.

**QComboBox handling (concrete):**

`QComboBox` is not a `QAbstractItemView` — it *owns* one via `comboBox->view()`, and its model via `comboBox->model()`. When `objectId` resolves to a `QComboBox`:

- **Walking** — use `comboBox->model()` for path resolution. Combo models are flat, so `itemPath`/`path` must have length 1 (else `kItemNotFound` with `failedSegment=1`).
- **Actions:**
  - `select`, `click`, `doubleClick` — all collapse to `comboBox->setCurrentIndex(row)`. The popup is **not** shown; actions only change the current value. Agents needing the popup open can `qt_methods_invoke(method="showPopup")` separately.
  - `edit` — `kNotEditable`. Combo boxes don't have per-cell editors in the same sense as views.
  - `expand` / `scroll` — no-ops.
- **`column`** — must be 0 (combo models are single-column); non-zero → `kInvalidColumn`.

## Shared Helpers (ModelNavigator)

```cpp
// Walk a row path, fetching more at each level. Returns invalid on failure;
// outFailedSegment gives the 0-based index where the walk broke.
QModelIndex pathToIndex(QAbstractItemModel* model, const QList<int>& path,
                        int* outFailedSegment = nullptr);

// Walk a text path (exact match at each level).
QModelIndex textPathToIndex(QAbstractItemModel* model, const QStringList& itemPath,
                            int matchColumn, int* outFailedSegment = nullptr);

// Convert an index to {path, cells, hasChildren} for responses.
QJsonObject indexToRowData(QAbstractItemModel* model, const QModelIndex& index,
                           const QList<int>& roles);

// Ensure lazy children at parentIdx are loaded.
void ensureFetched(QAbstractItemModel* model, const QModelIndex& parentIdx);
```

## New Error Codes

| Code | Meaning | Detail object |
|------|---------|---------------|
| `kInvalidParentPath` | A `parent` segment references a non-existent row. | `{ path: int[], failedSegment: int, availableRows: int }` — `availableRows` is `rowCount` at the failed level. |
| `kItemNotFound` | `qt_ui_clickItem` couldn't resolve `itemPath` or `path`. | For `itemPath`: `{ mode: "text", failedSegment: int, segmentText: string, partialPath: int[] }` — `partialPath` is the row path walked successfully before failure. For `path`: `{ mode: "row", failedSegment: int, requestedRow: int, availableRows: int, partialPath: int[] }`. |
| `kInvalidColumn` | `column` or `editColumn` out of range for the target row's parent. | `{ column: int, columnCount: int }` (uses key `editColumn` when the failing field is `editColumn`). |
| `kNotEditable` | `action="edit"` on a cell whose `flags` lack `Qt::ItemIsEditable`. | `{ path: int[], editColumn: int }`. |
| `kInvalidRegex` | `qt_models_find` with `match="regex"` and malformed `value`. | `{ pattern: string, error: string }` (error is `QRegularExpression::errorString()`). |

## Python Wrapper Changes (`python/src/qtpilot/tools/native.py`)

- **Modify** `qt_models_data` — **remove** the current `row`/`column`/`role` params (single-value). Confirmed callers: zero in `python/tests/test_tools.py`; one in `.claude/skills/test-mcp-modes/SKILL.md:95` which is being rewritten as part of this work (see Testing section). **No deprecation window** — straight removal in the same commit that adds `parent`/`roles`. New signature: `qt_models_data(objectId, parent: list[int] | None = None, offset: int | None = None, limit: int | None = None, roles: list[str] | None = None)`.
- **Add** `qt_models_find(objectId, value, column=0, role="display", match="contains", max_hits=10, parent=None)`.
- **Add** `qt_ui_clickItem(objectId, item_path=None, path=None, column=0, action="click", edit_column=None, expand=True, scroll=True)`.

## Testing

### C++ unit tests (`tests/test_model_navigator.cpp`, `tests/test_native_mode_api.cpp`)

| Scenario | Proves |
|----------|--------|
| 3-level tree, read children at `parent=[0,2]` | Multi-level path walk |
| `parent=[99]` on 5-row model | `kInvalidParentPath` with `failedSegment=0` |
| Custom lazy model, descend through lazy branch | `fetchMore` is called at each level |
| 5000-row flat list, `offset=4900 limit=100` | Pagination works with new path param |
| `qt_models_find` recursive across 3 levels, `match="contains"` | Custom DFS walker visits every subtree; `ensureFetched` is invoked at each descending parent |
| `qt_models_find` on a lazy model whose `rowCount` reports 0 until `fetchMore` is called | Walker finds matches in lazy branches (no false negatives) |
| `qt_models_find` with `parent=[1]` | Scope restriction honored |
| `qt_models_find` with `match="regex"`, malformed value | `kInvalidRegex` |
| `qt_ui_clickItem` with `itemPath=["A","B","C"]` on QTreeView, `action="edit"` | Expand + scroll + edit invoked |
| `qt_ui_clickItem` with `path=[0,0]` alternative | Dual addressing works |
| `qt_ui_clickItem` on `QComboBox` | View-subclass dispatch works |
| `qt_ui_clickItem` with non-existent text | `kItemNotFound` with `failedSegment` |

### Python smoke tests (`python/tests/test_tools.py`)

One test per new/modified tool, mocking the probe `call()`.

### End-to-end: demo app + `test-mcp-modes` skill

**Demo app changes (`test_app/mainwindow.ui`, `mainwindow.cpp`):** Add a **new `treeTab`** containing a `QTreeView` named `treeView` backed by a `QStandardItemModel` populated with:

- A 3-level nested hierarchy (Manufacturer → Fixture → Mode), e.g.:
  ```
  Manufacturers
  ├── ETC
  │   ├── fos4 Fresnel Lustr X8 direct
  │   └── ColorSource PAR
  └── Martin
      └── MAC Aura XB
          ├── Mode 8ch
          └── Mode 12ch
  ```
- Multiple columns (name, type, count) to exercise the `column` param.
- One parent populated with 1000+ synthetic rows to exercise pagination at depth.

**Skill updates (`.claude/skills/test-mcp-modes/SKILL.md`):**

- **Fix** the existing `qt_models_data(row=0, column=0)` call (those params don't match the wrapper or C++ API).
- **Add** a Native-mode tree scenario block:
  - Switch to `treeTab`.
  - `qt_models_data(objectId="treeView")` → top-level rows with `hasChildren=true`.
  - `qt_models_data(objectId="treeView", parent=[0])` → ETC's children.
  - `qt_models_data(objectId="treeView", parent=[0,0])` → empty (leaf).
  - `qt_models_find(objectId="treeView", value="Aura", match="contains")` → `path=[1,0]`.
  - `qt_ui_clickItem(objectId="treeView", itemPath=["Martin","MAC Aura XB","Mode 12ch"])` → `found=true`, ancestors expanded.
  - `qt_ui_clickItem(objectId="treeView", path=[0,0])` — dual-addressing proof.
  - Pagination: `qt_models_data(objectId="treeView", parent=[<large-parent>], offset=900, limit=50)` → 50 rows, `hasMore=false`.
- **Update** the results table with `models_tree_read`, `models_find`, `ui_clickItem` rows.

## Files Touched

- `src/probe/introspection/model_navigator.{h,cpp}` — signature change, new helpers, fetchMore support.
- `src/probe/api/native_mode_api.cpp` — register `qt.models.find`, `qt.ui.clickItem`; update `qt.models.data` handler.
- `src/probe/api/error_codes.h` — four new codes.
- `python/src/qtpilot/tools/native.py` — wrapper updates.
- `python/tests/test_tools.py` — smoke tests.
- `tests/test_model_navigator.cpp`, `tests/test_native_mode_api.cpp` — C++ unit tests.
- `test_app/mainwindow.ui`, `test_app/mainwindow.cpp`, `test_app/mainwindow.h` — new `treeTab`.
- `.claude/skills/test-mcp-modes/SKILL.md` — tree scenarios, fix stale call.

## Open Points for Plan Phase

The following are implementation-mechanics questions (not design questions) that the plan phase should resolve before writing code:

- **Python wrapper back-compat** — whether any caller of the current `qt_models_data(row, column, role)` exists in tests or examples (`python/tests/test_tools.py`). If yes → keep params as deprecation aliases for one release; if no → remove. Concrete check, not a design choice.
- **Synthetic pagination data** — exact shape of the 1000+ row subtree in the demo app. Procedural generation at startup is intended; specific field values (names, indices) are a test-data decision.
