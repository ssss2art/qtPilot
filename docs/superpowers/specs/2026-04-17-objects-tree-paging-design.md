# qtPilot Design: Paging Support for Large Object Trees

**Status:** Draft — 2026-04-17
**Scope:** `qt.objects.tree` (probe + Python wrapper), new `qt.objects.children` tool.
**Related:** `docs/qtpilot-tree-model-support-spec.md` (tree-model navigation — orthogonal feature covering data models, not the QObject hierarchy).

## Problem

`qt_objects_tree` walks the QObject parent/child hierarchy and bundles the entire walk into a single response. Its only brake is `maxDepth`. This fails in four predictable ways on real Qt applications:

1. **Breadth** — one parent with thousands of children (e.g. a `QStandardItemModel`'s internal items, a busy layout) produces a giant response even at shallow depth.
2. **Depth** — deep widget trees force callers to re-invoke with a new `root` and re-walk ancestors just to get context.
3. **Total size** — every node carries `geometry`, `text`, and optional QML metadata regardless of need, so even a "reasonable" tree overwhelms the MCP channel and assistant context budget.
4. **Unknown shape** — a caller cannot probe a subtree safely; either they guess `maxDepth` or they risk a runaway response.

The goal is to make `qt_objects_tree` safe by default on any real application while preserving its ergonomics for small apps.

## Goals

- A bounded, predictable response from `qt_objects_tree` regardless of the underlying QObject hierarchy size.
- A drill-down tool for paging through a known parent's children.
- Per-parent counts that tell callers *where* the bulk is, so they can target drill-downs.
- Opt-in verbose fields (geometry, text, QML) so the default payload is small.
- No changes that require serializing or threading `QObject*` across the JSON-RPC boundary in new ways.

## Non-goals

- Pagination of `qt_models_data` — already covered by its `offset`/`limit`/`parent` parameters.
- Pagination of `qt_objects_query` / `qt_objects_findByClass` — separate problem, separate spec.
- Lazy/cursor-based paging with server-side tokens. Offset-based is sufficient given how rarely QObject trees mutate mid-inspection, and it keeps the probe stateless.
- Backward compatibility with the current `qt_objects_tree` response shape. The project is pre-1.0, and the current default is the bug being fixed.

## Design

### Two tools, distinct responsibilities

**`qt.objects.tree`** — *shape/summary.* Walks the QObject hierarchy breadth-first within caps. Always produces a bounded response.

**`qt.objects.children`** — *paged drill-down.* Returns one level of a known parent's children with `offset`/`limit`. Used after `qt.objects.tree` surfaces a truncated parent.

Separating the tools keeps each one's job legible: "give me the shape" versus "give me page N of this parent's children." It also mirrors how humans explore a tree — scan, focus, drill in.

### Node content: core + opt-in fields

Every returned node carries the **core fields** unconditionally:

| field | type | meaning |
|---|---|---|
| `id` | string | hierarchical path ID (existing format) |
| `className` | string | `QObject::metaObject()->className()` |
| `objectName` | string | `QObject::objectName()` (may be empty) |
| `childCount` | int | `parent->children().size()` — always the real count, regardless of truncation |
| `hasChildren` | bool | `childCount > 0` |

**Opt-in fields** are requested via a single `fields` parameter that accepts either a preset string or a list of field names:

| `fields` value | effect |
|---|---|
| `[]` or omitted | core only (default) |
| `"lean"` | alias for `[]` |
| `"full"` | alias for `["geometry", "text", "qml"]` |
| `["geometry"]` | core + geometry |
| `["geometry", "text", "qml"]` | core + these three |

Optional field contents:
- `geometry` → `{x, y, width, height}`, only present on `QWidget` subclasses that have non-zero geometry.
- `text` → non-empty string from the existing `getTextProperty()` heuristic, omitted if empty.
- `qml` → `{isQmlItem, qmlId?, qmlFile?, qmlTypeName}`, only present when built with `QTPILOT_HAS_QML` and the object is a QML item.

Unknown field names produce a `kInvalidField` JSON-RPC error listing valid names. No silent ignore.

### `qt.objects.tree` parameters

| param | type | default | purpose |
|---|---|---|---|
| `root` | string | `null` (app root) | start node — existing semantics |
| `maxDepth` | int | `3` | max descent from `root`; nodes at the cutoff carry `hasChildren`/`childCount` but no `children` array |
| `maxChildren` | int | `50` | per-parent cap on the `children` array; parents with more get `truncated: true` + real `childCount` |
| `maxNodes` | int | `500` | global cap on total nodes in the response (BFS); stops expansion of further parents once hit |
| `fields` | string \| string[] | `[]` | opt-in fields, as above |

Negative values for `maxChildren`, `maxNodes`, `maxDepth` → `kInvalidArgument`. `maxDepth=0` is valid and returns just the root node with `hasChildren`/`childCount` but no `children` array. There is deliberately no "unlimited depth" sentinel — callers who want broad descent pass a large integer and rely on `maxNodes` as the hard stop. This differs from the pre-spec behaviour where `maxDepth=-1` meant unlimited; that escape hatch is removed because the whole point of this work is that every response is bounded.

### `qt.objects.tree` response

```jsonc
{
  "tree": {
    "id": "QApplication",
    "className": "QApplication",
    "objectName": "",
    "childCount": 3,
    "hasChildren": true,
    "children": [
      {
        "id": "MainWindow",
        "className": "MainWindow",
        "objectName": "mainWindow",
        "childCount": 1237,
        "hasChildren": true,
        "truncated": true,
        "children": [ /* first 50 children, core only */ ]
      }
    ]
  },
  "nodeCount": 342,
  "truncated": false,
  "limits": { "maxDepth": 3, "maxChildren": 50, "maxNodes": 500 }
}
```

- `truncated: true` on a node = per-parent cap hit.
- `truncated: true` at the envelope root = global cap hit.
- When both conditions would apply to the same parent, both flags are set; callers should treat them as equivalent signals: *page this parent with `qt.objects.children`.*
- `children` is omitted when the node has none *or* when `maxDepth` / `maxNodes` prevented expansion. Use `hasChildren`/`childCount` to disambiguate.

### Truncation algorithm

BFS walker, checking stop conditions in this order for each candidate expansion:

1. **Depth check** — if node's depth from `root` equals `maxDepth`, do not expand. Node gets core + optional fields + `hasChildren`/`childCount`, no `children` array.
2. **Global cap check** — if `nodeCount >= maxNodes`, do not expand any further parents. Already-expanded nodes keep their children; remaining nodes get the same treatment as the depth cutoff. Envelope `truncated` becomes `true`.
3. **Per-parent cap** — include the first `maxChildren` children from `parent->children()`. If there are more, set `truncated: true` on the parent. Each included child counts toward `nodeCount`.

BFS rather than DFS so the response gives a balanced overview of the subtree — a little bit of each branch rather than all of one — matching the "overview first" intent of the summary tool.

`parent->children().size()` is O(1) on Qt's `QObjectList`, so reporting the real `childCount` on every capped node is cheap.

### `qt.objects.children` parameters

| param | type | default | purpose |
|---|---|---|---|
| `objectId` | string | required | parent |
| `offset` | int | `0` | index into `parent->children()` |
| `limit` | int | `50` | count |
| `fields` | string \| string[] | `[]` | same semantics as tree |

One level only — no `maxDepth`. If the caller needs to descend, they call `qt.objects.tree` rooted at one of the returned children.

### `qt.objects.children` response

```jsonc
{
  "objectId": "MainWindow/CentralWidget/QStandardItemModel",
  "totalCount": 1237,
  "offset": 0,
  "limit": 50,
  "children": [
    {
      "id": "MainWindow/CentralWidget/QStandardItemModel/QStandardItem",
      "className": "QStandardItem",
      "objectName": "",
      "childCount": 0,
      "hasChildren": false
    }
    // ... 49 more
  ]
}
```

- `offset >= totalCount` → empty `children` array, no error (matches `qt.models.data`'s convention).
- Ordering is `QObject::children()` order (insertion order). Stable across calls on an un-mutated tree. Callers who page across a mutating tree may see skips or repeats; acceptable given how rarely widget trees mutate during inspection.

### Errors

All new errors follow the existing `JsonRpcException` + `ErrorCode` pattern.

| condition | code | detail |
|---|---|---|
| Unknown field name in `fields` | `kInvalidField` (new) | `{ field: "...", validFields: [...] }` |
| Negative `maxChildren` / `maxNodes` / `maxDepth` / `limit` / `offset` | `kInvalidArgument` (existing) | parameter name |
| `objectId` not found | `kObjectNotFound` (existing) | unchanged |
| `offset >= totalCount` | *not an error* | returns empty `children`, correct `totalCount` |

### Python wrapper

`python/src/qtpilot/tools/native.py`:

```python
@mcp.tool
async def qt_objects_tree(
    root: str | None = None,
    maxDepth: int | None = None,
    maxChildren: int | None = None,
    maxNodes: int | None = None,
    fields: str | list[str] | None = None,
    ctx: Context = None,
) -> dict:
    ...

@mcp.tool
async def qt_objects_children(
    objectId: str,
    offset: int | None = None,
    limit: int | None = None,
    fields: str | list[str] | None = None,
    ctx: Context = None,
) -> dict:
    ...
```

Parameters are passed through to the probe only when not `None`, matching the existing pattern.

## Implementation touchpoints

- **`src/probe/introspection/object_id.h`** — add `TreeOptions` and `ChildrenOptions` structs; declare new `serializeChildren()` and refactored `serializeObjectTree()` signature.
- **`src/probe/introspection/object_id.cpp`** — split the current monolithic `serializeObjectInfo()` into `serializeCore()` plus per-field helpers (`appendGeometry`, `appendText`, `appendQml`). Rewrite `serializeObjectTree()` as a BFS walker consuming `TreeOptions`. Implement `serializeChildren()`.
- **`src/probe/api/native_mode_api.cpp:233`** — extend the `qt.objects.tree` registration to parse `maxChildren`, `maxNodes`, `fields`. Register new `qt.objects.children` method next to it, mirroring the structure of `qt.models.data`.
- **`src/probe/transport/error_codes.h`** (wherever `ErrorCode` is defined) — add `kInvalidField`.
- **`python/src/qtpilot/tools/native.py`** — update `qt_objects_tree` signature and add `qt_objects_children` tool.
- **`python/tests/test_tools.py`** — add `qt_objects_children` to the tool-name manifest assertion.
- **`tests/test_native_mode_api.cpp`** — new test cases (below).
- **`mcp-diagram.html`** — add `qt_objects_children` to the tool list.

Splitting `serializeObjectInfo()` is justified by this work: the function already bundles concerns (core + geometry + text + QML) that are now conditional. Without the split, field filtering lives as if-ladders inside the walker, which is harder to test.

No new external dependencies. No protocol wire-level changes beyond one new method and new optional params on the existing one.

## Testing

Qt Test cases in `tests/test_native_mode_api.cpp`, alongside the existing `testObjectsTree`:

- **`testObjectsTreeCoreFieldsByDefault`** — call with defaults; assert each node has exactly `{id, className, objectName, childCount, hasChildren}` plus optional `children`; no geometry/text/qml keys present.
- **`testObjectsTreeFieldsPresetFull`** — `fields="full"`; assert geometry/text/qml keys present where applicable.
- **`testObjectsTreeFieldsAllowList`** — `fields=["geometry"]`; assert geometry present, text/qml absent.
- **`testObjectsTreeFieldsLeanAlias`** — `fields="lean"` behaves identically to `fields=[]`.
- **`testObjectsTreeMaxChildrenTruncation`** — construct a parent with 100 children; call with `maxChildren=10`; assert parent has `truncated:true`, `childCount:100`, `children.size()==10`.
- **`testObjectsTreeMaxNodesTruncation`** — construct a wide shallow tree exceeding `maxNodes`; assert envelope `truncated:true`, `nodeCount == maxNodes`.
- **`testObjectsTreeMaxDepthNoChildrenArray`** — at depth cutoff, nodes carry `hasChildren`/`childCount` but no `children` key.
- **`testObjectsTreeUnknownField`** — `fields=["bogus"]` → `kInvalidField` with `validFields` detail.
- **`testObjectsTreeNegativeLimit`** — `maxChildren=-1` → `kInvalidArgument`.
- **`testObjectsChildrenPaging`** — round-trip offset/limit across a 100-child parent; verify continuity and `totalCount`.
- **`testObjectsChildrenOffsetPastEnd`** — `offset >= totalCount` returns empty array, not error.
- **`testObjectsChildrenFields`** — field semantics identical to tree tool.

Python-side smoke tests in `python/tests/test_tools.py` already validate tool registration; the manifest update covers the new name.

## End-to-end example

Inspecting a Hog 4 fixture schedule window (real-world case with a wide `FTreeView`):

```
# 1. Overview — what's under the main window?
qt_objects_tree(root="ScreenWindow/FixtureScheduleWin")
# Response shows FTreeView node with truncated:true, childCount:1237

# 2. Drill into FTreeView's children, first page
qt_objects_children(
    objectId="ScreenWindow/FixtureScheduleWin/FTreeView",
    offset=0,
    limit=20
)
# Response: totalCount:1237, 20 lightweight nodes

# 3. Spot a header we care about, grab its geometry
qt_objects_children(
    objectId="ScreenWindow/FixtureScheduleWin/FTreeView",
    offset=0,
    limit=20,
    fields=["geometry"]
)
```

Before this spec, step 1 would have either blown the MCP message size or filled the context window with unused geometry/text data. With it, the default response is bounded, and the caller opts into heavier fields only when needed.

## Summary

| Problem | Mechanism |
|---|---|
| One parent has many children | `maxChildren` per-parent cap + `truncated` flag + real `childCount` |
| Tree is deep | Existing `maxDepth` (preserved) |
| Response payload is heavy | Core-only default + opt-in `fields` (preset or list) |
| Unknown subtree shape | Probe via `qt.objects.tree` (safe by default); drill with `qt.objects.children` |
| Runaway response | `maxNodes` global BFS cap + envelope `truncated` flag |

The unifying principle: **always return a bounded, balanced summary by default; let callers opt into heavier data or page deeper when they know what they want.**
