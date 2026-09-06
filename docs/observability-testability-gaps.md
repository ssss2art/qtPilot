# qtPilot — Observability & Testability Gap Analysis

Status: living document. Goal: make Qt apps probed by qtPilot as **correct**,
**testable**, and **observable** as possible by closing capability gaps in the
tool itself.

This catalog came from (a) hands-on probing of a real design-system app and
(b) a code-grounded audit of the probe, transport, and Python server. Each gap
notes why it matters and a rough effort estimate (S/M/L).

> Legend — Effort: **S** ≈ hours, **M** ≈ 1–2 days, **L** ≈ multi-day / epic.

---

## Already shipped / in flight

All of the below are implemented, unit-tested, **and live-validated against a
design-system gallery app**. All are merged to `main`; the branch names in the
Status column are historical and those branches no longer exist.

| # | Gap | Where | Status |
|---|-----|-------|--------|
| 1 | `className` search was exact-match, not subclass-aware | `object_registry.cpp` | ✅ `fix/probe-introspection-gaps` |
| 2 | `listProperties` omitted dynamic properties (QSS styling hooks) | `meta_inspector.cpp` | ✅ `fix/probe-introspection-gaps` |
| C1 | enum/flag values were opaque ints — now also emit symbolic `enumKey`/`enumKeys` | `meta_inspector.cpp` | ✅ `fix/value-serialization` |
| C2 | gadget sub-property recursion (incl. nested) + `QObject*` values resolve to an objectId ref | `variant_json.cpp` | ✅ `fix/value-serialization` |
| C3 | each property now reports its NOTIFY signal name | `meta_inspector.cpp` | ✅ `fix/value-serialization` |
| #4 | signal notifications now carry **argument values** (was the "Empty for MVP" stub) | `signal_monitor.cpp` | ✅ `feat/sync-and-signals` |

Deferred / still open from these themes: **C4** (QByteArray type-marker — left
as-is to avoid round-trip changes); the 64-bit-flag symbolic decode (needs a
Qt ≥6.9 overload while CI builds 5.15/6.5/6.8); **T1** wait primitive and **O5**
NOTIFY-watch (the rest of `feat/sync-and-signals`, not yet implemented).

---

## Top cross-cutting findings (multiple audits converged)

1. **No wait / synchronization primitive** — there is no server-side
   wait-for-signal / wait-for-property==value / wait-until-condition (with
   timeout). Every test is forced into racy client-side sleep-and-poll. This is
   the single biggest testability gap.
2. ~~**Signal notifications drop argument values** — `signal_monitor.cpp`
   hardcodes `arguments = QJsonArray()` ("Empty for MVP").~~ **DONE**
   (`feat/sync-and-signals`): the relay now decodes and emits argument values.
   This unblocks assertions, wait-for-value, and property-change observation.
3. **No request timeout** — `ProbeConnection.call()` awaits the response future
   with no timeout; a blocking handler hangs the MCP call forever. The whole RPC
   pipeline also runs on the GUI thread, so one slow call freezes the app and
   the socket.

---

## Correctness — reading state accurately

| ID | Gap | Why it matters | Evidence | Effort |
|----|-----|----------------|----------|--------|
| C1 | **Enum/flag values dumped as raw ints** (no symbolic names) | `132` instead of `AlignHCenter`; `prop.isEnumType()`/`enumerator()` never used | `variant_json.cpp` Int case; `meta_inspector.cpp` listProperties | M |
| C2 | **Gadget (`QFont`/`QPen`/…) & `QObject*` values lost** to null/empty | can't read font/palette; `QObject*` props serialize null instead of an objectId ref | `variant_json.cpp` generic fallback | M |
| C3 | Property metadata omits **notify-signal name & enum keys**, `isConstant`/`isResettable` | can't know what to subscribe to, or valid enum values to set | `meta_inspector.cpp` property entry | S |
| C4 | `QByteArray` base64-encoded **without a type marker** | ambiguous vs plain string; round-trips wrong | `variant_json.cpp` | S |
| C5 | Cross-thread object reads/invokes via `Qt::AutoConnection` | reading a worker-thread object's property can tear/deadlock; no thread-affinity guard | `meta_inspector.cpp` get/set/invoke | M |

## Observability — seeing state & behavior over time

| ID | Gap | Why it matters | Evidence | Effort |
|----|-----|----------------|----------|--------|
| O1 | **Console / `qWarning`/`qCritical` capture not wired into native mode** | can't assert "app emitted no warnings"; the capture class exists but only Chrome mode uses it | `console_message_capture.cpp`, `native_mode_api.cpp` | S |
| O2 | Captured console is **swamped by qtPilot's own jsonrpc logging** | app messages buried in probe `[qtPilot]` chatter | `console_message_capture.cpp` (text-regex filter only) | S |
| O3 | Object tree **omits top-level windows/dialogs/popups** & parentless QObjects | can't navigate to dialogs/menus; gen vs resolve use different roots | `object_id.cpp` getTopLevelObjects | M |
| O4 | objectIds are **positional/text-derived → unstable** across runs & on text change | breaks cross-run reproducibility | `object_id.cpp` sibling `#N`; text segments | M |
| O5 | No **property-change (NOTIFY) watch** | observing state evolve is mostly properties, not clicks | no API in `native_mode_api.cpp` | M |
| O6 | No **full-state snapshot** for golden/diff comparison | multi-call assembly is a torn read | no `qt.snapshot` | M |
| O7 | **Silent notification drops** (queue overflow never surfaced) | a "passing" test may have dropped the failing event | `connection.py`, `notification_queue.cpp` | S |
| O8 | Recording: **recursive subscribe only 1 level deep**; no emission timestamps; no **replay** | recordings miss nested widgets, aren't deterministic tests | `event_recorder.py` | S–L |
| O9 | Event capture is **QWidget-only** | QML/QQuickItem & QWindow events invisible | `event_capture.cpp` eventFilter | M |

## Models & QML

| ID | Gap | Why it matters | Evidence | Effort |
|----|-----|----------------|----------|--------|
| M1 | No **proxy source/proxy index mapping** (`QSortFilterProxyModel`) | can't correlate filtered rows to source | `model_navigator.cpp` resolveModel | M |
| M2 | No `headerData` access | can't assert/locate columns by header | `model_navigator.cpp` | S |
| M3 | No direct `setData` / no **selection / currentIndex read** | can't set model values or assert selection | `native_mode_api.cpp` clickItem only | M |
| M4 | No model-change signal taxonomy for sync (`rowsInserted`/`dataChanged`…) | can't deterministically wait for model loads | (depends on signal args) | M |
| Q1 | **QML introspection near-stub** — only id/file/typename; no bindings/states/attached/context props | QML apps largely unobservable | `qml_inspector.cpp` | L |
| Q2 | **No QML visual tree** — tree walks QObject children, not `childItems()` | tree wrong/incomplete under `QQuickWindow` | `object_id.cpp` serializeTreeRecursive | M |
| Q3 | **Can't drive QML/Quick items** — all UI actions reject non-`QWidget` | QML un-automatable (click/type/screenshot/geometry) | `native_mode_api.cpp` resolveWidgetParam | L |

## Testability — driving & asserting

| ID | Gap | Why it matters | Evidence | Effort |
|----|-----|----------------|----------|--------|
| T1 | **No wait-for-condition/signal/property** | racy sleep-poll is the #1 flaky-test cause | no `qt.wait*` registered | L |
| T2 | **No assertion primitives** (server-side expect) | clients parse free-form gets | n/a | M |
| T3 | No **modifiers on mouse actions**; `cu.key` supports modifiers for one combination, but has no **key down/up or multi-chord sequences** | can't Ctrl/Shift-click, hold keys, or send sequences such as `Ctrl+K, Ctrl+C` | `computer_use_mode_api.cpp`, `input_simulator.cpp` | M |
| T4 | No **hover / tooltip / mouse-enter** | hover-reveal UI & `:hover` styling untestable | `input_simulator.cpp` mouseMove | M |
| T5 | No **multi-select** / item selection-model access | range/Ctrl selection impossible | `native_mode_api.cpp` clickItem | M |
| T6 | **Menus / popups / dialogs** not first-class | context-menu/dropdown/modal flows unreachable | active-window anchoring | L |
| T7 | Screenshots can't support **golden/diff** (no hash/diff/mask); huge inline payload | visual regression infeasible; payload instability | `screenshot.cpp` | M |
| T8 | **HiDPI/Retina coordinate ambiguity** — physical-pixel mode returns no DPR | clicks off by 2× with no scale info | `computer_use_mode_api.cpp` | S |
| T9 | No **focus/activation** ops or focus assertion | Tab-order / focus tests can't be set up or verified | `input_simulator.cpp` (focus is a side effect) | S/M |

## Reliability / quality — trusting the tool

| ID | Gap | Why it matters | Evidence | Effort |
|----|-----|----------------|----------|--------|
| R1 | ~~**No request timeout**~~ **DONE** -- `call()` takes a deadline (default 30s, `QTPILOT_CALL_TIMEOUT`) and raises `ProbeTimeoutError` | a blocking handler hung the call forever | `connection.py` | S |
| R2 | RPC pipeline runs on the **GUI thread** | slow call freezes app + socket; cross-thread invoke can deadlock | `websocket_server.cpp` synchronous handle | M/L |
| R3 | **No auto-reconnect**; reconnect loses probe link | app restart / blip kills the session until manual re-connect | `connection.py`, `server.py` | M |
| R4 | ~~**No probe↔python version handshake**; probe version stale~~ **DONE** -- version generated from `PROJECT_VERSION`; `protocolVersion` on the wire; client warns on skew at connect | silent skew → opaque "method not found" | `core/version.h.in`, `connection.py` | S |
| R5 | Legacy `qtpilot.*` handlers throw **generic errors** (good `ErrorCode` taxonomy unused there) | clients can't branch on failure type | `jsonrpc_handler.cpp` | S/M |
| R6 | **Single-client server**; no multi-probe / parallel sessions | can't drive two apps; stale client blocks new connects | `websocket_server.cpp`, `server.py` | M |
| R7 | Probe binds **all interfaces + LAN broadcast, no auth** -> `invokeMethod` = remote code exec | **Reduced, not closed.** The bind is now a policy (`QTPILOT_BIND_ADDRESS`) that an operator can narrow to loopback, an unrecognised value restricts rather than widens, announcements follow the bind, and the probe states the exposure at startup. The **default remains all-interfaces**: reaching instrumented apps on other hosts is a product requirement and discovery is broadcast-based, so a loopback default is an outage rather than a hardening. **Authentication is the actual fix and does not exist** -- until it does, narrowing the bind is the only control available | `bind_policy.cpp`, `websocket_server.cpp` | M |
| R8 | **No headless E2E/CI harness** (tests mock the socket) | transport/lifecycle regressions uncaught | `python/tests/conftest.py` | M |
| R9 | **Server teardown with a live client crashes on some Qt versions** | `stop()` calls `deleteLater()` on the accepted socket, but `nextPendingConnection()` parents that socket to the `QWebSocketServer`, which is a child of `WebSocketServer` -- so `~WebSocketServer` can also reach it down the parent chain, and which path runs first is left to timing. Observed as a **SEGFAULT on Qt 6.8.0 and 6.9.0, Linux and Windows both**, while 5.15.2 / 6.5.3 / 6.10.0 / 6.11.1 passed. Reproducer: connect a real `QWebSocket` to the server, then destroy the server. Ordering the teardown by waiting on `QWebSocket::disconnected` does not help -- `close()` does not emit it within 5s on 5.15/6.5. **Root cause unconfirmed and the product path is unchanged**; the test that exposed it was removed rather than left flaky, so nothing in CI covers this today. A probe shutting down while an MCP client is attached has the same shape | `websocket_server.cpp` `stop()` / `onNewConnection()` | M |

---

## Proposed branch / PR split

Ordered by value × independence. Each row is one PR off `main` unless noted.

| Branch | Contents | Status |
|--------|----------|--------|
| `fix/probe-introspection-gaps` | #1 subclass search, #2 dynamic props | ✅ done — ready to push |
| `fix/value-serialization` | C1 enums/flags, C2 gadgets/QObject*, C3 metadata | ✅ done (C4 bytes deferred); stacks on the above |
| `feat/sync-and-signals` | #4 signal arg values | ✅ done. ⏳ still to add: T1 wait primitive, O5 NOTIFY-watch |
| `feat/native-diagnostics` | O1 native console capture, O2 probe-log separation, O7 drop visibility | ⏳ planned — low-effort, high observability ROI |
| `fix/probe-hardening` | R7 bind policy (default unchanged), R4 version + protocol handshake, R1 request timeout | ✅ done |
| `fix/transport-robustness` | R3 auto-reconnect, R5 structured errors, R6 multi-client, **R7 authentication (the real fix; blocks any change to the bind default)** | ⏳ planned — reliability |
| `feat/model-introspection` *(epic)* | M1 proxy mapping, M2 headerData, M3 setData/selection, M4 change taxonomy | ⏳ planned |
| `feat/qml-support` *(epic)* | Q1 introspection, Q2 visual tree, Q3 drive Quick items | ⏳ planned — large; scope separately |
| `feat/interaction-enhancements` *(epic)* | T3 mouse modifiers/held keys/multi-chord sequences, T4 hover, T5 multi-select, T6 menus, T7 diff, T8 DPR, T9 focus | ⏳ planned |

Dependency / push notes:
- `fix/value-serialization` is **stacked on** `fix/probe-introspection-gaps` —
  base its PR on that branch, or push/merge `fix/probe-introspection-gaps` first.
- `feat/sync-and-signals` and `docs/qtpilot-gaps` are **independent** off `main`.
- The remaining `feat/sync-and-signals` work (T1 wait, O5 NOTIFY-watch) builds on
  the now-shipped argument capture; land it as a follow-up commit on that branch.
