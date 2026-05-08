# qtPilot Feature Request: Tree Model Navigation Support

## Problem

qtPilot's native mode has no way to programmatically navigate, search, or select items in Qt tree views (`QTreeView` / `QAbstractItemModel` hierarchies). The root cause is that `QModelIndex` — the fundamental addressing type in Qt's model/view framework — is an opaque, non-serializable C++ type that cannot be passed through qtPilot's JSON-RPC interface.

This means:

- **`qt_models_data`** only returns top-level rows. There is no way to specify a parent node to read its children.
- **`QItemSelectionModel` properties** (`currentIndex`, `selectedIndexes`, `selection`) all serialize as `null` because `QModelIndex` and `QModelIndexList` have no JSON representation.
- **`qt_methods_invoke`** cannot call methods that accept or return `QModelIndex` (e.g., `model.match()`, `view.setCurrentIndex()`, `selectionModel.selectedRows()`).

### Impact

Tree views are extremely common in Qt applications. Without tree model support, qtPilot users must resort to:

1. Keyboard navigation (arrow keys) — fragile and non-deterministic for deep trees
2. Screenshot + coordinate clicking — slow and resolution-dependent
3. Narrowing filters to make navigation predictable — works only when a search UI exists

This is the single biggest gap in native mode for real-world application automation.

## Proposed Solution: Row Path Addressing

### Key Design Decision

Instead of trying to serialize `QModelIndex` (which is fundamentally an opaque handle tied to a model instance), introduce **row paths** as the JSON-serializable proxy.

A row path is an integer array representing the chain of row indices from the root to a node:

```
[]        = root (top-level)
[0]       = first top-level row
[0, 2]    = third child of the first top-level row
[0, 2, 1] = second child of the third child of the first top-level row
```

The probe reconstructs the `QModelIndex` internally by chaining `model.index(row, 0, parentIndex)` calls down the path. Column is always 0 for path addressing (column is specified separately where needed).

This is lossless: every `QModelIndex` in a tree model can be uniquely described by its row path, and vice versa. The probe handles all `QModelIndex` construction/destruction internally.

## New and Modified Tools

### 1. Extend `qt_models_data` with `parent` parameter

**Current behavior:** Returns only top-level rows.

**Proposed:** Add optional `parent` parameter (row path) to read children of any node.

```jsonc
// Read top-level rows (unchanged)
qt_models_data(objectId="myTreeView")

// Read children of the first top-level row
qt_models_data(objectId="myTreeView", parent=[0])

// Read grandchildren: children of row 2 under row 0
qt_models_data(objectId="myTreeView", parent=[0, 2])
```

**Response changes:** Add `path` field to each returned row so callers know the full address.

```jsonc
{
  "rows": [
    {
      "path": [0, 0],
      "cells": [{"display": "ETC"}, {"display": "fos4 Fresnel Lustr X8 direct"}, ...],
      "hasChildren": false
    },
    {
      "path": [0, 1],
      "cells": [{"display": "ETC"}, {"display": "Fos4PD16 direct"}, ...],
      "hasChildren": false
    }
  ],
  "totalRows": 8,
  "totalColumns": 11
}
```

**Implementation:** In the probe's model data handler:

```cpp
QModelIndex parentIndex;
if (request.has("parent")) {
    for (int row : request["parent"].toArray()) {
        parentIndex = model->index(row, 0, parentIndex);
        if (!parentIndex.isValid()) {
            return error("Invalid parent path");
        }
    }
}
int rowCount = model->rowCount(parentIndex);
// ... iterate rows using model->index(row, col, parentIndex)
```

### 2. New tool: `qt_models_find`

Search for rows matching a value, with recursive tree support.

**Parameters:**

| Parameter  | Type     | Required | Description |
|-----------|----------|----------|-------------|
| objectId  | string   | yes      | Tree view or model objectId |
| value     | string   | yes      | Value to search for |
| column    | int      | no       | Column to search (default: 0) |
| role      | string   | no       | Qt role name (default: "display") |
| match     | string   | no       | Match mode: "exact", "contains", "startsWith", "endsWith", "regex" (default: "contains") |
| maxHits   | int      | no       | Maximum results (default: 10) |
| parent    | int[]    | no       | Row path to search from (default: root) |

**Example:**

```jsonc
qt_models_find(
    objectId="myTreeView",
    value="fos4 Fresnel Lustr X8 direct",
    column=1,
    match="exact"
)
```

**Response:**

```jsonc
{
  "matches": [
    {
      "path": [0, 0],
      "cells": [
        {"column": 0, "display": "ETC"},
        {"column": 1, "display": "fos4 Fresnel Lustr X8 direct"},
        {"column": 5, "display": "0"},
        {"column": 9, "display": "12ch"}
      ]
    }
  ],
  "totalMatches": 1
}
```

**Implementation:** Uses `QAbstractItemModel::match()` with `Qt::MatchRecursive`:

```cpp
Qt::MatchFlags flags = Qt::MatchRecursive;
if (matchMode == "exact")      flags |= Qt::MatchExactly;
if (matchMode == "contains")   flags |= Qt::MatchContains;
if (matchMode == "startsWith") flags |= Qt::MatchStartsWith;
// ...

QModelIndex startIndex = model->index(0, column, parentIndex);
QModelIndexList matches = model->match(startIndex, role, value, maxHits, flags);

// Convert each QModelIndex to a row path + cell data
for (const QModelIndex& idx : matches) {
    result.append(indexToRowData(model, idx));
}
```

The helper `indexToRowData` walks up via `idx.parent()` to reconstruct the row path.

### 3. New tool: `qt_models_select`

Select a row in a view by path or by search.

**Parameters:**

| Parameter  | Type     | Required | Description |
|-----------|----------|----------|-------------|
| objectId  | string   | yes      | Tree view objectId |
| path      | int[]    | no*      | Row path to select |
| value     | string   | no*      | Value to search and select |
| column    | int      | no       | Column for value search (default: 0) |
| match     | string   | no       | Match mode (default: "contains") |
| edit      | bool     | no       | Open editor on the selected cell (default: false) |
| editColumn| int      | no       | Column to edit (default: same as column) |
| scroll    | bool     | no       | Scroll view to selection (default: true) |
| expand    | bool     | no       | Expand parent nodes if collapsed (default: true) |

*Either `path` or `value` must be provided.

**Examples:**

```jsonc
// Select by path
qt_models_select(objectId="myTreeView", path=[0, 0])

// Select by search
qt_models_select(
    objectId="myTreeView",
    value="fos4 Fresnel Lustr X8 direct",
    column=1,
    match="exact"
)

// Select and open editor on Count column
qt_models_select(
    objectId="myTreeView",
    value="fos4 Fresnel Lustr X8 direct",
    column=1,
    editColumn=5,
    edit=true
)
```

**Response:**

```jsonc
{
  "selected": true,
  "path": [0, 0],
  "cells": [
    {"column": 0, "display": "ETC"},
    {"column": 1, "display": "fos4 Fresnel Lustr X8 direct"},
    {"column": 5, "display": "10"}
  ]
}
```

**Implementation:**

```cpp
QModelIndex targetIndex;
if (request.has("path")) {
    targetIndex = pathToIndex(model, request["path"]);
} else {
    // Use match() to find it
    QModelIndexList matches = model->match(...);
    if (matches.isEmpty()) return error("No match found");
    targetIndex = matches.first();
}

if (expand) {
    // Expand all ancestors
    QModelIndex parent = targetIndex.parent();
    while (parent.isValid()) {
        treeView->expand(parent);
        parent = parent.parent();
    }
}

treeView->setCurrentIndex(targetIndex);
if (scroll) treeView->scrollTo(targetIndex);
if (edit)   treeView->edit(model->index(targetIndex.row(), editColumn, targetIndex.parent()));
```

### 4. New tool: `qt_models_selection`

Read the current selection as data (not as `QModelIndex`).

**Parameters:**

| Parameter  | Type     | Required | Description |
|-----------|----------|----------|-------------|
| objectId  | string   | yes      | Tree view objectId |

**Example:**

```jsonc
qt_models_selection(objectId="myTreeView")
```

**Response:**

```jsonc
{
  "hasSelection": true,
  "currentItem": {
    "path": [0, 0],
    "cells": [
      {"column": 0, "display": "ETC"},
      {"column": 1, "display": "fos4 Fresnel Lustr X8 direct"},
      {"column": 5, "display": "10"},
      {"column": 9, "display": "12ch"}
    ]
  },
  "selectedItems": [
    {
      "path": [0, 0],
      "cells": [...]
    }
  ],
  "selectedCount": 1
}
```

**Implementation:**

```cpp
QItemSelectionModel* selModel = treeView->selectionModel();
QModelIndex current = selModel->currentIndex();

if (current.isValid()) {
    result["currentItem"] = indexToRowData(model, current);
}

QModelIndexList selected = selModel->selectedRows();
for (const QModelIndex& idx : selected) {
    result["selectedItems"].append(indexToRowData(model, idx));
}
```

## Helper: `indexToRowData`

Shared implementation for converting a `QModelIndex` to a serializable row path + cell data:

```cpp
QJsonObject indexToRowData(QAbstractItemModel* model, const QModelIndex& index) {
    QJsonObject row;

    // Build path by walking up to root
    QJsonArray path;
    QModelIndex walk = index;
    while (walk.isValid()) {
        path.prepend(walk.row());
        walk = walk.parent();
    }
    row["path"] = path;

    // Read cell data for all columns
    QJsonArray cells;
    QModelIndex parent = index.parent();
    for (int col = 0; col < model->columnCount(parent); ++col) {
        QModelIndex cellIndex = model->index(index.row(), col, parent);
        QJsonObject cell;
        cell["column"] = col;
        QVariant displayData = model->data(cellIndex, Qt::DisplayRole);
        cell["display"] = displayData.isNull() ? QJsonValue() : QJsonValue::fromVariant(displayData);
        cells.append(cell);
    }
    row["cells"] = cells;
    row["hasChildren"] = model->hasChildren(index);

    return row;
}
```

## End-to-End Example: Hog Fixture Schedule

Without these features (current state):
```
# 1. Expand tree — hope for the best
qt_methods_invoke(objectId="..FTreeView", method="expandAll")

# 2. Click first row
qt_ui_click(objectId="..FTreeView", position={x:100, y:30})

# 3. Arrow down — guessing which row we're on
qt_ui_sendKeys(objectId="..FTreeView", sequence="Right")
qt_ui_sendKeys(objectId="..FTreeView", sequence="Down")

# 4. Screenshot to verify — decode base64, view image
qt_ui_screenshot(objectId="..FTreeView")

# 5. Type value — hoping we're on the right row
qt_ui_sendKeys(objectId="..FTreeView", text="10")
```

With these features (proposed):
```
# 1. Find the fixture type
qt_models_find(
    objectId="..FTreeView",
    value="fos4 Fresnel Lustr X8 direct",
    column=1,
    match="exact"
)
# Returns: path=[0, 0], confirming it exists

# 2. Select it and open the Count editor
qt_models_select(
    objectId="..FTreeView",
    value="fos4 Fresnel Lustr X8 direct",
    column=1,
    editColumn=5,
    edit=true
)
# Row is selected, scrolled into view, Count cell editor is open

# 3. Type the count
qt_ui_sendKeys(objectId="..FTreeView", text="10")

# 4. Verify
qt_models_selection(objectId="..FTreeView")
# Returns: path=[0,0], Count="10" — confirmed
```

## Summary

| Current gap | Proposed tool | Mechanism |
|------------|--------------|-----------|
| Can't read tree children | `parent` param on `qt_models_data` | Row path `[0, 2, 1]` replaces `QModelIndex` |
| Can't search tree | `qt_models_find` | `QAbstractItemModel::match()` with `Qt::MatchRecursive` |
| Can't select by value/path | `qt_models_select` | Constructs `QModelIndex` from path, calls `setCurrentIndex()` |
| Can't read current selection | `qt_models_selection` | Reads `currentIndex()` → converts to data + path |

The unifying design principle: **use row paths (integer arrays) as the serializable proxy for `QModelIndex`**. The probe handles all `QModelIndex` construction and destruction internally. No `QModelIndex` ever crosses the JSON-RPC boundary.

---

## Alternative: Text Path Addressing with `qt_ui_clickItem`

The row-path approach above (integer arrays like `[0, 2, 1]`) solves the low-level problem of `QModelIndex` serialization. But for most automation scenarios, **users don't know or care about row indices** — they want to find an item by its visible text, just as they would in a manual test.

Squish (the commercial Qt UI testing tool) provides `waitForObjectItem(view, itemPath)` which does exactly this — you give it a view widget and a text path like `"ETC.fos4 Fresnel Lustr X8 direct"`, and it walks the model to find the matching item. However, Squish's approach has a design flaw: it uses dot (`.`) as the path separator for trees and slash (`/`) for tables. If item text contains these characters, the path breaks. There's no escaping mechanism.

### Better design: JSON string arrays

Instead of a separator-delimited string, use a **JSON array of strings** — one element per tree level:

```jsonc
// Squish: "ETC.fos4 Fresnel Lustr X8 direct"  (breaks if text contains ".")
// Proposed: ["ETC", "fos4 Fresnel Lustr X8 direct"]  (always unambiguous)
```

Advantages:
- **No separator ambiguity** — item text can contain any characters (dots, slashes, spaces, etc.)
- **One format for all widget types** — trees, lists, tables, combos all use `string[]`
- **Already JSON** — no string parsing in the probe; the array maps directly to tree traversal
- **Self-documenting** — array length = tree depth; `["ETC", "fos4 Fresnel Lustr X8 direct"]` is clearly a two-level lookup
- **Consistent with row paths** — integer arrays for positional addressing, string arrays for text addressing

### New tool: `qt_ui_clickItem`

Find an item in any `QAbstractItemView` subclass by text path, then interact with it.

**Parameters:**

| Parameter  | Type     | Required | Description |
|-----------|----------|----------|-------------|
| objectId  | string   | yes      | View widget objectId (QTreeView, QTableView, QListView, QComboBox) |
| itemPath  | string[] | yes      | Text at each tree level; single-element array for flat views |
| column    | int      | no       | Column to match display text against (default: 0) |
| action    | string   | no       | `"select"`, `"click"`, `"doubleClick"`, `"edit"` (default: `"click"`) |
| editColumn| int      | no       | Column to edit if action is `"edit"` |
| expand    | bool     | no       | Expand parent nodes before interacting (default: true) |
| scroll    | bool     | no       | Scroll item into view (default: true) |

**Examples:**

```jsonc
// Tree view: click a fixture type under a manufacturer
qt_ui_clickItem(
    objectId="ScreenWindow/FixtureScheduleWin/FTreeView",
    itemPath=["ETC", "fos4 Fresnel Lustr X8 direct"]
)

// Tree view: select and open editor on Count column
qt_ui_clickItem(
    objectId="ScreenWindow/FixtureScheduleWin/FTreeView",
    itemPath=["ETC", "fos4 Fresnel Lustr X8 direct"],
    action="edit",
    editColumn=5
)

// Combo box: select an item (single-element array)
qt_ui_clickItem(
    objectId="..QComboBox",
    itemPath=["Hog PC"]
)

// List view: click a file
qt_ui_clickItem(
    objectId="..FFileView",
    itemPath=["Desktop"]
)

// Deep tree: three levels
qt_ui_clickItem(
    objectId="..QTreeView",
    itemPath=["Level 1", "Level 2", "Level 3"]
)
```

**Response:**

```jsonc
{
  "found": true,
  "path": [0, 0],         // integer row path (for programmatic re-use)
  "itemPath": ["ETC", "fos4 Fresnel Lustr X8 direct"],  // echo back
  "cells": [
    {"column": 0, "display": "ETC"},
    {"column": 1, "display": "fos4 Fresnel Lustr X8 direct"},
    {"column": 5, "display": "0"},
    {"column": 9, "display": "12ch"}
  ]
}
```

**Implementation:**

```cpp
QJsonObject handleUiClickItem(const QJsonObject& request) {
    QAbstractItemView* view = findView(request["objectId"]);
    QAbstractItemModel* model = view->model();
    QJsonArray itemPath = request["itemPath"].toArray();
    int matchColumn = request.value("column", 0).toInt();

    // Walk the tree level by level, matching display text
    QModelIndex current; // root (invalid = top level)

    for (const auto& segment : itemPath) {
        QString text = segment.toString();
        bool found = false;
        int rowCount = model->rowCount(current);

        for (int row = 0; row < rowCount; ++row) {
            QModelIndex idx = model->index(row, matchColumn, current);
            if (model->data(idx, Qt::DisplayRole).toString() == text) {
                current = idx;
                found = true;
                break;
            }
        }

        if (!found) {
            return {{"found", false}, {"error", "Item not found: " + text}};
        }
    }

    // Expand all ancestors so the item is visible
    if (request.value("expand", true).toBool()) {
        QModelIndex parent = current.parent();
        while (parent.isValid()) {
            static_cast<QTreeView*>(view)->expand(parent);
            parent = parent.parent();
        }
    }

    // Scroll into view
    if (request.value("scroll", true).toBool()) {
        view->scrollTo(current);
    }

    // Perform the requested action
    QString action = request.value("action", "click").toString();

    if (action == "select") {
        view->setCurrentIndex(current);
    } else if (action == "click") {
        view->setCurrentIndex(current);
        QRect rect = view->visualRect(current);
        QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, rect.center());
    } else if (action == "doubleClick") {
        view->setCurrentIndex(current);
        QRect rect = view->visualRect(current);
        QTest::mouseDClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, rect.center());
    } else if (action == "edit") {
        int editCol = request.value("editColumn", matchColumn).toInt();
        QModelIndex editIdx = model->index(current.row(), editCol, current.parent());
        view->setCurrentIndex(editIdx);
        view->edit(editIdx);
    }

    // Return item data + both path formats
    QJsonObject result = indexToRowData(model, current);
    result["found"] = true;
    result["itemPath"] = itemPath;
    return result;
}
```

### Comparison: Squish vs qtPilot (proposed)

**Squish:**
```javascript
mouseClick(
    waitForObjectItem(fixtureScheduleWinFTreeView, "ETC.fos4 Fresnel Lustr X8 direct"),
    83, 10, Qt.NoModifier, Qt.LeftButton
);
```

**qtPilot (proposed):**
```jsonc
qt_ui_clickItem(
    objectId="ScreenWindow/FixtureScheduleWin/FTreeView",
    itemPath=["ETC", "fos4 Fresnel Lustr X8 direct"]
)
```

### End-to-End: Hog Fixture Schedule with `qt_ui_clickItem`

```
# 1. Search to filter the list
qt_properties_set(objectId="..FLineEdit", name="text", value="fos4 Fresnel Lustr X8")

# 2. Click the fixture type — one call, deterministic
qt_ui_clickItem(
    objectId="ScreenWindow/FixtureScheduleWin/FTreeView",
    itemPath=["ETC", "fos4 Fresnel Lustr X8 direct"],
    action="edit",
    editColumn=5
)
# Item found, expanded, selected, Count editor opened

# 3. Type the count
qt_ui_sendKeys(objectId="..FTreeView", text="10")

# 4. Verify
qt_models_selection(objectId="..FTreeView")
```

### Relationship to Row Path Tools

`qt_ui_clickItem` with text paths and `qt_models_data`/`qt_models_select` with integer row paths serve complementary purposes:

| Approach | Addressing | Best for |
|----------|-----------|----------|
| Text path (`string[]`) | `["ETC", "fos4 Fresnel Lustr X8 direct"]` | Automation scripts, test authoring — human-readable and stable across model changes |
| Row path (`int[]`) | `[0, 0]` | Programmatic traversal, reading model data, iterating children |
| Value search | `value="fos4 Fresnel Lustr X8 direct"` | Finding items when the tree position is unknown |

All three ultimately construct `QModelIndex` internally. The string array approach is the most practical for automation — it's what users think in, it's stable across data changes (unlike integer indices), and it works uniformly across all view types.
