# qtPilot Design: Native-Mode Tool Consolidation

**Status:** Draft — 2026-04-17
**Scope:** Reshape the MCP tool surface exposed by qtPilot native mode. Pre-1.0 hard break; no backward compatibility required.
**Related:** `2026-04-17-objects-tree-paging-design.md` — apply this consolidation *first*, then the paging changes on top.

## Problem

qtPilot native mode exposes ~48 MCP tools today. The surface has accumulated the way most fast-moving APIs do: useful new verbs were added next to existing ones rather than folding into them. The result — in priority order of the pain caused:

1. **Cognitive load.** The agent spends effort choosing between overlapping tools. "Do I use `qt_objects_find`, `qt_objects_findByClass`, or `qt_objects_query`?" is a real choice-paralysis moment, and picking wrong produces an awkward multi-call chain. The same pattern appears with `qt_properties_list` / `qt_methods_list` / `qt_signals_list` vs `qt_objects_inspect`.
2. **API inconsistency.** Different tools use different naming conventions (`qtPilot_probe_status` is mixed-case; everything else is snake_case), different return shapes (`{success: true}` vs `{changed: true}` vs `{disconnected: true}`), and different verbs for the same idea (`find` vs `search`).
3. **Prompt budget.** Every tool definition is tokens in every request. Smaller surface = smaller prompt overhead. Secondary, but real.

The goal is a tool surface where an agent never has to pick between overlapping variants, naming is predictable, and return shapes compose cleanly.

## Goals

- **One canonical tool per job.** No overlapping verbs, no convenience aliases.
- **Bundled reads via one tool** (`qt_objects_inspect`) with a `parts` parameter — single place to go for read-only introspection.
- **Uniform naming** — snake_case everywhere, noun-first lifecycle triads, single verb vocabulary.
- **Uniform return envelopes** — filter tools all return the same shape; actions all return `{ok: true, ...}`.
- **No regression in capability** — every current use case remains expressible after consolidation.

## Non-goals

- Backward compatibility. Pre-1.0; this is a versioned hard break. No aliasing old names.
- Changing chrome-mode or computer-use-mode tool surfaces. Out of scope for this spec.
- Adding new capabilities. This work removes/merges only. New capabilities (property-name glob, class-level schema, etc.) are separate specs.

## Summary of changes

| Metric | Before | After | Δ |
|---|---|---|---|
| Total native-mode tools | 50 | 39 | −11 |
| `qt_objects_*` tools | 6 | 4 | −2 |
| Introspection listing tools | 3 (`properties/methods/signals_list`) | 0 (folded into `inspect`) | −3 |
| Mode/status tools | 4 (`qt_modes`, `qtpilot_get_mode`, `qtpilot_list_probes`, `qtPilot_probe_status`) | 1 (`qtpilot_status`) | −3 |
| Camel-case tool names | 3 | 0 | −3 |

Arithmetic check: 50 current tools − 14 killed + 2 new (`qt_objects_search`, `qtpilot_status`) + 1 added by the companion paging spec (`qt_objects_children`) = 39. Five tools on the rename list keep their count.

The numerical reduction matters less than the *structural* reduction in choices: each tool now has a single clear job.

## Kill list

Tools removed outright — callers migrate to the absorbing tool.

| Removed | Absorbed by |
|---|---|
| `qt_objects_find` | `qt_objects_search(objectName=...)` |
| `qt_objects_findByClass` | `qt_objects_search(className=...)` |
| `qt_objects_query` | `qt_objects_search(...)` |
| `qt_objects_info` | `qt_objects_inspect(parts=["info"])` |
| `qt_properties_list` | `qt_objects_inspect(parts=["properties"])` |
| `qt_methods_list` | `qt_objects_inspect(parts=["methods"])` |
| `qt_signals_list` | `qt_objects_inspect(parts=["signals"])` |
| `qt_qml_inspect` | `qt_objects_inspect(parts=["qml"])` |
| `qt_models_info` | `qt_objects_inspect(parts=["model"])` |
| `qt_modes` | `qtpilot_status().available_modes` |
| `qtpilot_get_mode` | `qtpilot_status().mode` |
| `qtpilot_list_probes` | `qtpilot_status().discovery.probes` |
| `qtPilot_probe_status` | `qtpilot_status()` (also fixes the camelCase leak) |
| `qtpilot_log_tail` | `qtpilot_log_status(tail=N)` |

## Rename list

Consistency-only renames; semantics unchanged.

| Old | New | Reason |
|---|---|---|
| `qtpilot_start_recording` | `qtpilot_recording_start` | Noun-first lifecycle triad |
| `qtpilot_stop_recording` | `qtpilot_recording_stop` | same |
| `qt_events_startCapture` | `qt_events_start` | Drop camelCase; match `start`/`stop` lifecycle |
| `qt_events_stopCapture` | `qt_events_stop` | same |
| `qt_models_find` | `qt_models_search` | Standardise on `search` verb |

## Consolidated tools — detailed specification

### `qt_objects_search`

Replaces `qt_objects_find`, `qt_objects_findByClass`, `qt_objects_query`.

**Params:**

| param | type | default | purpose |
|---|---|---|---|
| `objectName` | string | — | Exact match on `QObject::objectName()` |
| `className` | string | — | Exact match on `metaObject()->className()`. Subclass-aware (today's `findByClass` semantics) |
| `properties` | object | `{}` | Property-value filters; every listed property must compare equal to the supplied value |
| `root` | string | — | Restrict search to this subtree |
| `limit` | int | `50` | Maximum matches returned |

At least one of `objectName`, `className`, `properties` must be supplied. Calling with only `root` (or with nothing) → `kInvalidArgument` with detail `"specify at least one of objectName, className, properties"`. No "return every object in the app" foot-gun.

**Response (uniform envelope):**

```jsonc
{
  "objects": [
    { "objectId": "MainWindow/centralWidget/myButton",
      "className": "QPushButton",
      "objectName": "myButton",
      "numericId": 12 }
  ],
  "count": 1,
  "truncated": false
}
```

- Always a list, even when filters uniquely identify one object. Removes today's `find`-returns-single / `query`-returns-list shape split.
- `truncated: true` when more results existed beyond `limit`.
- `objectName` matching is exact-only. Pattern matching (glob/regex) can be added later as a separate `objectNameMatch` parameter without breaking this design.
- `properties` values compared with `QJsonValue` equality — `properties: {"visible": true}` works; `properties: {"text": null}` does not mean "no text set."

**Property-name discoverability.** Agents learn available property names by calling `qt_objects_inspect(objectId, parts=["properties"])` on an instance. The `qt_objects_search` docstring points to this explicitly. Two lookups the first time, zero after.

### `qt_objects_inspect`

Replaces `qt_objects_info`, `qt_properties_list`, `qt_methods_list`, `qt_signals_list`, `qt_qml_inspect`, `qt_models_info`.

**Params:**

| param | type | default | purpose |
|---|---|---|---|
| `objectId` | string | required | The object to inspect |
| `parts` | string \| string[] | `["info"]` | Which sections to include |

**Valid `parts` values:**

| value | contents | applicability |
|---|---|---|
| `"info"` | `{className, objectName, superClasses: [str], visible?, enabled?}` — matches today's `MetaInspector::objectInfo`; `visible`/`enabled` only present on `QWidget` subclasses | Always applicable |
| `"properties"` | `[{name, type, value, readable, writable}]` | Always applicable |
| `"methods"` | `[{name, signature, returnType, access, methodType}]` | Always applicable |
| `"signals"` | `[{name, signature, parameters}]` | Always applicable |
| `"qml"` | `{isQmlItem, qmlId?, qmlFile?, qmlTypeName}` when applicable; `null` when not a QML item *or* when the probe was built without `QTPILOT_HAS_QML` | See "Null vs throw" below |
| `"geometry"` | `{x, y, width, height, visible}` when object is a `QWidget`; `null` otherwise | See "Null vs throw" below |
| `"model"` | `{rowCount, columnCount, roleNames, hasChildren, headers?}` when object inherits `QAbstractItemModel` (or has one via `ModelNavigator::resolveModel`); `null` otherwise | See "Null vs throw" below |
| `"all"` | Alias for `["info","properties","methods","signals","qml","geometry","model"]` | — |

Unknown value → `kInvalidField` with the valid list. This error code is introduced by this spec (not previously present); the paging spec reuses it.

**Response envelope — flat, key-per-part:**

```jsonc
{
  "objectId": "MainWindow/myTreeView",
  "info": { "className": "QTreeView", ... },
  "properties": [ ... ],
  "geometry": { "x": 10, "y": 40, "width": 400, "height": 300, "visible": true },
  "model": null
}
```

- Only requested parts appear as keys.
- When a part was requested but isn't applicable to this object type (`model` on a non-model object, `geometry` on a non-widget), the key is present with value `null`. The agent can distinguish "not requested" from "requested but N/A" by key presence.
- Default `parts=["info"]` matches today's `qt_objects_info` lightweight behaviour, so the most common call is unchanged in shape.

**Heterogeneous response schema.** The return shape varies by requested parts and object type, but is *deterministic*: `parts` determines keys; object type determines whether applicable-only parts are `null`. Agents can reason about it with no surprises.

**Null vs throw — deliberate behaviour change.** Today's `qt_qml_inspect` throws `kQmlNotAvailable` when the probe is built without QML support, and today's `qt_models_info` throws `kNotAModel` when the object isn't a model (see `src/probe/api/native_mode_api.cpp:1002` and `:1042`). Under this spec those become `null` values in the response when accessed via `qt_objects_inspect(parts=[...])`. Rationale: the inspect tool's whole point is "tell me what's relevant about this object." Forcing callers to catch exceptions for `parts="all"` against an arbitrary object would be terrible UX. The explicit behaviour change:

- `qt_objects_inspect(parts=["qml"])` on a non-QML object → `{ "qml": null }`, no error.
- `qt_objects_inspect(parts=["qml"])` on any object when built without `QTPILOT_HAS_QML` → `{ "qml": null }`, no error.
- `qt_objects_inspect(parts=["model"])` on a non-model object → `{ "model": null }`, no error.

The dedicated tools are being removed, so their throwing behaviour disappears with them. No caller loses an error case they could meaningfully act on; they gain the ability to request "give me everything" without risk.

**Internal refactor required.** Today's `qt.objects.inspect` unconditionally returns all four sections (see `src/probe/api/native_mode_api.cpp:263`) with no `parts` support. This is a behavioural change, not just composition: the existing handler must be rewritten to parse `parts`, dispatch to the appropriate helpers per part, and omit unrequested sections. `MetaInspector::objectInfo`, `listProperties`, `listMethods`, `listSignals` stay as-is and are called conditionally.

### `qtpilot_status`

Replaces `qt_modes`, `qtpilot_get_mode`, `qtpilot_list_probes`, `qtPilot_probe_status`.

**Params:** none.

**Response:**

```jsonc
{
  "mode": "native",
  "available_modes": ["native", "cu", "chrome", "all"],
  "connection": {
    "connected": true,
    "ws_url": "ws://127.0.0.1:9443/qtpilot",
    "probe_version": "0.3.0",
    "protocol_version": 1
  },
  "discovery": {
    "active": true,
    "probes": [
      { "ws_url": "ws://127.0.0.1:9443/qtpilot",
        "app_name": "qtPilot-test-app",
        "pid": 12408,
        "last_seen_ms": 740 }
    ]
  }
}
```

- `mode` and `available_modes` at the top (most frequently checked).
- `connection` — attachment state (what `qtPilot_probe_status` returned).
- `discovery` — UDP broadcast state and discovered probes (what `qtpilot_list_probes` returned).
- When not connected: `connection.connected=false`, `ws_url`/`probe_version`/`protocol_version` absent.
- When discovery is disabled: `discovery.active=false`, `probes` is an empty array.

`qtpilot_set_mode`, `qtpilot_connect_probe`, `qtpilot_disconnect_probe` remain separate tools — they are actions, not queries.

### `qtpilot_log_status` with `tail`

Folds `qtpilot_log_tail` into `qtpilot_log_status`.

**Params:**

| param | type | default | purpose |
|---|---|---|---|
| `tail` | int | `0` | When `>0`, include that many most-recent entries in the response |

**Response:**

```jsonc
{
  "active": true,
  "path": "/tmp/qtpilot-session.jsonl",
  "level": 2,
  "buffer_size": 1000,
  "entry_count": 427,
  "duration_ms": 93410,
  "entries": [ /* last N entries if tail>0, omitted otherwise */ ]
}
```

- `tail=0` → same cheap status response as today's `qtpilot_log_status`; no `entries` key.
- `tail=N` → last `min(N, entry_count)` entries included; `entries` key present.
- `tail<0` → `kInvalidArgument`.

## Naming & convention rules (project-wide)

These rules drive the consolidation and apply to every existing tool plus any tool added in the future.

**Rule 1 — Prefix split is by trust boundary.**
- `qt_*` = methods the probe handles (target application introspection/manipulation).
- `qtpilot_*` = methods the MCP server / Python layer handles (session, logging, recording, discovery, mode).

Today's violator `qtPilot_probe_status` is killed in Section 3.

**Rule 2 — All snake_case, all lowercase.**
Tool names and sub-namespaces follow `word_word_word`. No camelCase anywhere in tool identifiers. Offenders renamed: `qtPilot_probe_status`, `qt_events_startCapture`, `qt_events_stopCapture`.

**Rule 3 — Params are camelCase.**
Matches the established majority (`objectId`, `maxDepth`, `className`, `objectName`, `roleNames`). Internal JSON-RPC param keys are camelCase; the Python wrapper uses `snake_case` on the Python side and maps — this is already the established pattern.

**Rule 4 — Lifecycle triads are noun-first: `<noun>_start`, `<noun>_stop`, `<noun>_status`.**
Applies to `qtpilot_log_*`, `qtpilot_recording_*`, `qt_events_*`. Violators renamed.

**Rule 5 — Query verb vocabulary is closed:**

| verb | meaning |
|---|---|
| `search` | Multi-filter discovery returning a list (`qt_objects_search`, `qt_models_search`) |
| `inspect` | Detailed read-only introspection of one thing (`qt_objects_inspect`) |
| `get` / `set` | Single-value read/write (`qt_properties_get/set`) |
| `list` | Enumerate well-known instances of a type (`qt_models_list`, `qt_names_list`) |
| `status` | Runtime state snapshot (`qtpilot_status`, `qtpilot_log_status`, `qtpilot_recording_status`) |

`find` is banned — ambiguous between "single exact match" and "fuzzy search." `qt_models_find` is renamed to `qt_models_search`.

**Rule 6 — Filter tools return a uniform envelope:**

```jsonc
{ "<collection>": [...], "count": N, "truncated": bool }
```

- `<collection>` is the plural noun (`objects`, `matches`, `rows`).
- `count` is the length of the returned array.
- `truncated: true` when more results existed beyond `limit`.

Applies to `qt_objects_search`, `qt_models_search`, `qt_models_data`. No per-tool one-off shapes.

**Rule 7 — Actions return `{ ok: true, ... }` at minimum.**
Replaces today's mix of `{success:true}`, `{changed:true}`, `{disconnected:true}`, `{connected:true}`. Additional result data is keyed alongside `ok`. Errors are JSON-RPC errors, never `{ok:false}` payloads.

### Action-response migration (part of this spec, not a follow-up)

Concrete list of current response shapes and the target shape. Every touched call site in `src/probe/api/native_mode_api.cpp` and the Python session tools needs updating as part of this work.

| Tool (current) | Current response | Migrated response |
|---|---|---|
| `qt.properties.set` (`native_mode_api.cpp:399`) | `{success: bool}` | On success: `{ok: true}`. On failure: throw `kPropertyTypeMismatch` / `kPropertyReadOnly` etc. — remove the `success:false` path. |
| `qt.methods.invoke` (`:520`) | `{success: true, result: ...}` | `{ok: true, result: ...}` |
| `qt.signals.subscribe` (`:589`) | `{success: true, subscriptionId: ...}` | `{ok: true, subscriptionId: ...}` |
| `qt.signals.unsubscribe` (`:610`) | `{success: true}` | `{ok: true}` |
| `qt.events.start` (was `startCapture`, `:912`) | `{success: true}` | `{ok: true}` |
| `qt.events.stop` (was `stopCapture`, `:930`) | `{success: true}` | `{ok: true}` |
| `qt.names.register` (`:968`) | `{success: true}` | `{ok: true}` |
| `qt.names.unregister` (`:986`) | `{success: bool}` | On success: `{ok: true}`. On unknown name: throw `kNameNotFound`. |
| `qtpilot_connect_probe` | `{connected: bool, ws_url, error?}` | On success: `{ok: true, ws_url}`. On failure: JSON-RPC error with the failure reason as detail. |
| `qtpilot_disconnect_probe` | `{disconnected: bool, ws_url?, reason?}` | On success: `{ok: true, ws_url?}`. On no-active-connection: `{ok: true}` (idempotent, not an error). |
| `qtpilot_set_mode` | `{changed: bool, error?}` | On success: `{ok: true, mode}`. On invalid mode: JSON-RPC error. |
| `qt.properties.set` uses of `success:false` → throw | — | Same pattern: any `success:false` branch becomes an appropriate error code throw. |

**Principle:** `ok` is always `true` when the method returns normally; failures use JSON-RPC error responses with typed codes and detail objects. No more dual-channel signalling (success:true/false *and* exceptions).

This removes a real pain: today, callers writing defensive code must handle both `{success:false}` payloads *and* exceptions from the same tool. Under Rule 7, only exceptions signal failure.

## Final tool surface

The 39 tools after consolidation, grouped:

**Probe — introspection (`qt_objects_*`, `qt_properties_*`, `qt_methods_*`, `qt_signals_*`):**

- `qt_objects_search`, `qt_objects_inspect`, `qt_objects_tree`, `qt_objects_children` (the last from the paging spec)
- `qt_properties_get`, `qt_properties_set`
- `qt_methods_invoke`
- `qt_signals_subscribe`, `qt_signals_unsubscribe`, `qt_signals_setLifecycle`

**Probe — models (`qt_models_*`):**
- `qt_models_list`, `qt_models_data`, `qt_models_search`

**Probe — UI interaction (`qt_ui_*`):**
- `qt_ui_click`, `qt_ui_clickItem`, `qt_ui_sendKeys`, `qt_ui_screenshot`, `qt_ui_geometry`, `qt_ui_hitTest`

**Probe — events (`qt_events_*`):**
- `qt_events_start`, `qt_events_stop`

**Probe — persistent names (`qt_names_*`):**
- `qt_names_register`, `qt_names_unregister`, `qt_names_list`, `qt_names_validate`, `qt_names_load`, `qt_names_save`

**Probe — diagnostics:**
- `qt_ping`, `qt_version`

**MCP server — session (`qtpilot_*`):**
- `qtpilot_status`, `qtpilot_connect_probe`, `qtpilot_disconnect_probe`, `qtpilot_set_mode`

**MCP server — logging / recording:**
- `qtpilot_log_start`, `qtpilot_log_stop`, `qtpilot_log_status`
- `qtpilot_recording_start`, `qtpilot_recording_stop`, `qtpilot_recording_status`

Total: 10 + 3 + 6 + 2 + 6 + 2 + 4 + 6 = **39 tools**.

## Migration notes

- Pre-1.0 hard break. No aliasing old names to new ones — that defeats the consolidation's purpose.
- Bump the qtPilot protocol version and note the change in the changelog under "v0.4.0 tool consolidation."
- The Python wrapper exposes *only* the new tool set. Old names removed.
- Docs: update `mcp-diagram.html` tool list. Update `CLAUDE.md` usage examples at lines 224–232 — four of the referenced tools are affected (`qt_objects_findByClass`, `qt_objects_query`, `qt_properties_list`, plus general examples using removed tools).

## Testing

Test cases added to `tests/test_native_mode_api.cpp` and `python/tests/test_tools.py`:

**`qt_objects_search`:**
- `testSearchByObjectName`, `testSearchByClassName`, `testSearchByProperties`, `testSearchAllFiltersCombined`
- `testSearchNoFiltersIsError` (expect `kInvalidArgument` with useful detail)
- `testSearchRootScoping`
- `testSearchLimitTruncation` (construct >limit matches; verify `truncated:true` + `count == limit`)

**`qt_objects_inspect`:**
- `testInspectDefaultInfoOnly`
- `testInspectEachPartIndividually` (one test per valid part)
- `testInspectAllAlias`
- `testInspectUnknownPartError` (expect `kInvalidField`)
- `testInspectNonApplicablePartsReturnNull` (request `model` on a non-model object; request `geometry` on a non-widget)
- `testInspectModelPartOnActualModel` (populated structure)
- `testInspectGeometryPartOnWidget` (populated structure)

**`qtpilot_status`:**
- `testStatusStructurePresent` (all three subgroups always present, even when empty)
- `testStatusWhenDisconnected` (`connection.connected==false`, sensible defaults)
- `testStatusWithDiscoveryActive` (`discovery.probes` populated)

**`qtpilot_log_status`:**
- `testLogStatusDefaultNoEntries` (`tail=0`, no `entries` key)
- `testLogStatusWithTail` (`tail=N`, min(N, count) entries)
- `testLogStatusNegativeTailError`

**Renames — all test files referencing old names must be updated:**

- `python/tests/test_tools.py` — manifest assertion; add new names, remove old.
- `python/tests/test_mode_switching.py` — references `qtpilot_get_mode`, `qtpilot_list_probes`; migrate to `qtpilot_status`.
- `python/tests/test_logging_tools.py` — references `qtpilot_log_tail`; migrate to `qtpilot_log_status(tail=N)`.
- `python/tests/test_recording_tools.py` — references `qtpilot_start_recording` / `qtpilot_stop_recording`; rename.
- `tests/test_native_mode_api.cpp` — references several killed / renamed JSON-RPC methods (`qt.objects.find`, `qt.objects.findByClass`, `qt.objects.query`, `qt.properties.list`, `qt.methods.list`, `qt.signals.list`, `qt.qml.inspect`, `qt.models.info`, `qt.modes`, `qt.events.startCapture`, `qt.events.stopCapture`, `qt.models.find`); rewrite or delete each affected test.
- `tests/test_model_navigator.cpp` — references `qt.models.find`; rename to `qt.models.search`.

The Python manifest assertion in `test_tools.py` stays the canary — if any caller site is missed, it fails fast.

**Action-response migration tests** (Rule 7 normalization, see below): update tests that assert on old-shape responses (`{success:true}`, `{changed:true}`, `{connected:true}`, `{disconnected:true}`) to assert on `{ok:true}`.

## Implementation touchpoints

**Probe side:**
- `src/probe/api/native_mode_api.cpp` — the bulk of the work. Remove each `RegisterMethod(...)` call for a killed tool; register each new canonical tool. The existing `qt.objects.inspect` registration at `:263` needs rewriting to parse `parts`, dispatch conditionally, and return null for non-applicable parts (removes the current `kQmlNotAvailable` / `kNotAModel` throws when folded in).
- `src/probe/introspection/meta_inspector.{h,cpp}` — unchanged internals; `inspect` composes existing helpers (`objectInfo`, `listProperties`, `listMethods`, `listSignals`) and calls `inspectQmlItem` / `ModelNavigator::resolveModel` conditionally.
- `src/probe/api/error_codes.h` — add `kInvalidField` for unknown `parts` values. (The paging spec reuses this code.)

**Python wrapper:**
- `python/src/qtpilot/tools/native.py` — remove killed tools; add consolidated tools.
- `python/src/qtpilot/tools/session.py` (or equivalent for `qtpilot_*`) — same pattern for session-layer tools.

**Tests:**
- `tests/test_native_mode_api.cpp` — delete tests for killed tools; add cases listed above.
- `python/tests/test_tools.py` — update manifest assertion.

**Docs:**
- `mcp-diagram.html` — update tool list.
- `CLAUDE.md` — update usage examples.

## Interaction with the paging spec

The paging design (`2026-04-17-objects-tree-paging-design.md`) lives inside this consolidation's surface. `qt_objects_tree` and `qt_objects_children` are both survivors; the paging-specific parameters (`maxChildren`, `maxNodes`, `fields`) are unaffected by the consolidation.

**Order of implementation: consolidation first, paging second.** Both touch the probe-side serialization code (`serializeObjectInfo` / `serializeObjectTree` in `src/probe/introspection/object_id.cpp`). Consolidation's `qt_objects_inspect` needs the same split-by-field refactor of `serializeObjectInfo` that paging needs. Doing consolidation first means the refactor happens once, against a clean post-consolidation surface.

## Risk

Low. All changes are additive-then-subtractive at the tool boundary; internal helpers are stable. The tool-name manifest assertion in `python/tests/test_tools.py` is the canary: any missed caller site fails fast.

## Summary

| Problem | Fix |
|---|---|
| Overlapping `find`/`findByClass`/`query` → choice paralysis | One `qt_objects_search` with all filters |
| `properties_list`/`methods_list`/`signals_list` vs `inspect` → bundle-vs-granular confusion | One `qt_objects_inspect` with `parts` |
| Three mode/status tools with overlapping info | One `qtpilot_status` with three subgroups |
| `log_status` + `log_tail` split | `log_status(tail=N)` |
| Naming leaks: `qtPilot_probe_status`, `qt_events_startCapture` | Snake-case rule, noun-first triads |
| Return-shape drift between tools | Uniform envelopes for filters and actions |

Unifying principle: **remove every decision the agent shouldn't have to make.** Fewer tools, each with one clear job, predictable names, predictable shapes.
