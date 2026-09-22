# qtPilot — Observability & Testability Gap Analysis

Status: living document. Goal: make Qt apps probed by qtPilot as **correct**,
**testable**, and **observable** as possible by closing capability gaps in the
tool itself.

This catalog came from (a) hands-on probing of a real design-system app and
(b) a code-grounded audit of the probe, transport, and Python server. Each gap
notes why it matters and a rough effort estimate (S/M/L).

> Legend — Effort: **S** ≈ hours, **M** ≈ 1–2 days, **L** ≈ multi-day / epic.

---

## Current priority order

This is the current top-down order after reconciling the catalog with `main`:

1. **R7 authentication** for the default LAN-reachable probe.
2. **R8 blocking real-probe E2E in CI**; the suites exist, but CI currently skips them.
3. **Release the post-v0.1.5 work** so PyPI and downloadable probes match the documented surface.
4. **T1 wait primitives + O5 NOTIFY watch** for deterministic automation.
5. **Native diagnostics and transport resilience** (O1/O2/O7, R2/R3/R5/R6).
6. **Remaining model and interaction depth** (M1-M4, T4/T5/T7/T9/T11/T12).

Basic QML discovery and driving are shipped and are no longer a standalone epic.

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
as-is to avoid round-trip changes); 64-bit-flag symbolic decode (the Qt ≥6.9
overload can now be exercised by the 6.9/6.10 CI legs while preserving older
compatibility); **T1** wait primitive and **O5** NOTIFY-watch.

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
3. ~~**No request timeout**~~ **DONE** — `ProbeConnection.call()` has a default
   30-second deadline. The remaining problem is **R2**: the probe RPC pipeline
   runs on the GUI thread, so a slow handler can still freeze the app and socket.

---

## Correctness — reading state accurately

| ID | Gap | Why it matters | Evidence | Effort |
|----|-----|----------------|----------|--------|
| C1 | ~~Enum/flag values were dumped as raw ints~~ **DONE** | symbolic `enumKey`/`enumKeys` are emitted alongside values | `meta_inspector.cpp` | M |
| C2 | ~~Gadget and `QObject*` values were lost~~ **DONE** | gadget sub-properties recurse and object pointers resolve to object IDs | `variant_json.cpp` | M |
| C3 | ~~Property metadata omitted notify signals and enum keys~~ **DONE** | property metadata exposes NOTIFY and enum information | `meta_inspector.cpp` | S |
| C4 | `QByteArray` base64-encoded **without a type marker** | ambiguous vs plain string; round-trips wrong | `variant_json.cpp` | S |
| C5 | Cross-thread object reads/invokes via `Qt::AutoConnection` | reading a worker-thread object's property can tear/deadlock; no thread-affinity guard | `meta_inspector.cpp` get/set/invoke | M |

## Observability — seeing state & behavior over time

| ID | Gap | Why it matters | Evidence | Effort |
|----|-----|----------------|----------|--------|
| O1 | **Console / `qWarning`/`qCritical` capture not wired into native mode** | can't assert "app emitted no warnings"; the capture class exists but only Chrome mode uses it | `console_message_capture.cpp`, `native_mode_api.cpp` | S |
| O2 | Captured console is **swamped by qtPilot's own jsonrpc logging** | app messages buried in probe `[qtPilot]` chatter | `console_message_capture.cpp` (text-regex filter only) | S |
| O3 | ~~Object tree omitted top-level windows/dialogs/popups~~ **DONE** | `getTopLevelObjects()` now includes top-level `QWindow`s and `QWidget`s; native context-menu tools cover open-menu discovery and activation | `object_id.cpp`, `native_mode_api.cpp` | M |
| O4 | objectIds are **positional/text-derived → unstable** across runs & on text change | breaks cross-run reproducibility | `object_id.cpp` sibling `#N`; text segments | M |
| O5 | No **property-change (NOTIFY) watch** | observing state evolve is mostly properties, not clicks | no API in `native_mode_api.cpp` | M |
| O6 | No **full-state snapshot** for golden/diff comparison | multi-call assembly is a torn read | no `qt.snapshot` | M |
| O7 | **Notification drops are counted but not surfaced through MCP status/results** | internal probe/Python counters exist, but a caller can still report a false pass without seeing them | `connection.py`, `notification_queue.cpp`, `status.py` | S |
| O8 | Recording: **recursive subscribe only 1 level deep**; no emission timestamps; ~~no **replay**~~ | **Replay: DONE.** A level-2+ message log is now re-runnable. A session splits into the five methods that change the application and everything else, which only observes it; re-driving the first and diffing the second is a behavioural assertion needing no hand-written test per flow. Determinism comes from stripping timing (including the probe's own `meta.timestamp`), stripping the request id at the top level only (a nested `id` names an *object*), masking generated `QObject~N` handles (the counter follows construction order -- registered names are the stable identity), treating logger-truncated values as wildcards, and comparing notifications as a multiset. `qtpilot_replay_inspect` / `qtpilot_replay_run`. **Still open:** recursive subscribe remains 1 level deep, so a recording still misses nested widgets, and there are still no emission timestamps -- signals are ordered per step but not timed within one | `replay.py`; `event_recorder.py` | S–L |
| O9 | ~~Event capture was QWidget-only~~ **DONE** | `QQuickItem` and `QQuickWindow` input events are captured and live-validated | `event_capture.cpp`, `test_qml_interaction.cpp` | M |

## Models & QML

| ID | Gap | Why it matters | Evidence | Effort |
|----|-----|----------------|----------|--------|
| M1 | No **proxy source/proxy index mapping** (`QSortFilterProxyModel`) | can't correlate filtered rows to source | `model_navigator.cpp` resolveModel | M |
| M2 | No `headerData` access | can't assert/locate columns by header | `model_navigator.cpp` | S |
| M3 | No direct `setData` / no **selection / currentIndex read** | can't set model values or assert selection | `native_mode_api.cpp` clickItem only | M |
| M4 | No model-change signal taxonomy for sync (`rowsInserted`/`dataChanged`…) | can't deterministically wait for model loads | (depends on signal args) | M |
| Q1 | **Advanced QML metadata remains shallow** — bindings, states, attached properties, and context properties are not modeled directly | basic tree/property/method/model/a11y inspection works; advanced QML semantics still require inference | `qml_inspector.cpp` | L |
| Q2 | ~~No QML visual tree~~ **DONE** | traversal combines QObject children with `QQuickItem::childItems()` and includes top-level `QWindow`s | `object_id.cpp`, `test_object_id.cpp` | M |
| Q3 | ~~Can't drive QML/Quick items~~ **DONE** | click, typing, geometry, hit testing, screenshots, computer-use input, accessibility actions, signals, and events are covered and live-validated | `native_mode_api.cpp`, `computer_use_mode_api.cpp`, `QML-A11Y-EVALUATION.md` | L |

## Testability — driving & asserting

| ID | Gap | Why it matters | Evidence | Effort |
|----|-----|----------------|----------|--------|
| T1 | **No wait-for-condition/signal/property** | racy sleep-poll is the #1 flaky-test cause | no `qt.wait*` registered | L |
| T2 | **No general server-side assertion DSL** | inline replay observations provide structured equality/divergence assertions, but ad-hoc clients still assemble assertions themselves | `replay.py` | M |
| T3 | ~~No **modifiers on mouse actions**~~ **DONE** | Every method that synthesizes a mouse event takes an optional `modifiers` (`"ctrl"`, `"ctrl+shift"`, `["ctrl","shift"]`, case-insensitive), parsed in one place by `ModifierParser` and shared by `qt.ui.click`, `qt.ui.doubleClick`, the `cu.*` mouse methods and `qt.ui.sendKeys` text input. `InputSimulator` had always accepted modifiers on every entry point -- only the JSON-RPC layer could not say so. Names map onto **Qt's enum, not the keycaps**: Qt reports macOS Command as `ControlModifier`, so `"ctrl"` is Command there and `"meta"` reaches the physical Control key, which is what an app's own `Qt::ControlModifier` checks compare against. Unknown names are refused with `kInvalidParams` rather than silently dropped. Found driving a plan-view desktop app, where a Ctrl+click selection test could not be reproduced at all because the probe had no way to hold a modifier. Key down/up and multi-chord sequences are split out as T11 and T12 | `modifier_parser.cpp`, `native_mode_api.cpp`, `computer_use_mode_api.cpp` | M |
| T4 | No **hover / tooltip / mouse-enter** | hover-reveal UI & `:hover` styling untestable | `input_simulator.cpp` mouseMove | M |
| T5 | No **multi-select** / item selection-model access | range/Ctrl selection impossible | `native_mode_api.cpp` clickItem | M |
| T6 | **Menus/popups/dialogs are only partly first-class** | context menus and modal/popup discovery work; generic dropdown/menu-bar/submenu workflows still lack one uniform API | `native_mode_api.cpp` | M |
| T7 | Screenshots still lack **golden/diff/hash/mask** operations | native `ImageContent` and `save_to` avoid huge inline text payloads, but visual regression comparison remains client-side | `screenshot_helper.py`, `screenshot.cpp` | M |
| T8 | ~~HiDPI/Retina geometry returned no DPR~~ **DONE** | widget, QWindow, QML, and graphics-view geometry report `devicePixelRatio`; computer-use input retries physical-to-logical scaling | `hit_test.cpp`, `computer_use_mode_api.cpp` | S |
| T9 | No **focus/activation** ops or focus assertion | Tab-order / focus tests can't be set up or verified | `input_simulator.cpp` (focus is a side effect) | S/M |
| T10 | ~~**`QGraphicsView` scene items are discoverable but not addressable**~~ **DONE** | `qt.ui.geometry` maps a `QGraphicsObject` through the view(s) rendering it (scene/viewport/global rects, `visible`, one entry per view), `qt.ui.hitTest` descends into the scene instead of stopping at the viewport, and `qt.objects.inspect` reports a scene-space geometry part. Found driving a widgets + QGraphicsView desktop app: an item knew it was at scene (300,300) and nothing could turn that into a screen point, so the caller had to guess the zoom scale by dragging a known distance and dividing | `hit_test.cpp`, `native_mode_api.cpp` | M |
| T11 | **Multi-chord key sequences send only the first chord** | `sendKeySequence()` parses the string with `QKeySequence` and then takes combination `0` only, so `"Ctrl+K, Ctrl+S"` silently sends `Ctrl+K` and reports success -- a caller testing a chorded shortcut gets a green result for something that never happened, which is worse than an error. Both the QWidget and QWindow overloads do this deliberately and say so in a comment, so closing it is a scope decision rather than a bug fix. **No app we drive uses chorded shortcuts yet** -- recorded so the silent half-send is known before something depends on it. Shape of the fix: loop `count()` combinations, deciding whether an inter-chord delay is needed and what a partial failure mid-sequence should report | `input_simulator.cpp` `sendKeySequence()` | S/M |
| T12 | **No key down/up; a modifier cannot be held across operations** | modifiers are per-call, so "hold Ctrl, click three items, release" is three independent Ctrl+clicks. Equivalent for selection, where each click carries the modifier, but not for apps that latch on the modifier's own press/release edges (marquee modes, temporary tool switches, spring-loaded states). The mouse side already has `cu.mouseDown`/`cu.mouseUp`; this is the keyboard counterpart | `input_simulator.cpp`, `computer_use_mode_api.cpp` | S/M |

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
| R8 | **Real-probe Python E2E exists but is skipped in CI** | `test_complicated_app_e2e.py` and `test_replay_e2e.py` launch the real probe/app when binaries exist; Python CI jobs do not receive those binaries, so the cross-layer path is not blocking | `python/tests/test_*_e2e.py`, `.github/workflows/ci.yml` | M |
| R9 | ~~**Server teardown with a live client crashes on some Qt versions**~~ **DONE** | `QWebSocket::close()` may emit `disconnected()` synchronously, re-entering `onClientDisconnected()`, which nulled `m_activeClient` -- `stop()` then dereferenced it. SEGFAULTed on Qt 6.8/6.9 (Linux and Windows) and survived on 5.15.2/6.5.3/6.10.0/6.11.1, the signature of a timing-dependent re-entrancy hole. **Fix:** both teardown paths go through `takeActiveClient()`, which clears the state before anything that can re-enter. Reproduced as a CI red on exactly those four legs before fixing, and covered by `tests/test_websocket_teardown.cpp` | `websocket_server.cpp` | M |
| R10 | ~~**`invokeMethod` segfaulted the host on a pointer argument**~~ **DONE** | a `Q_INVOKABLE` taking a pointer received whatever `jsonToVariant` coerced the JSON value into, so `describeObject(1)` handed the application the address `0x1` to dereference. A probe must never crash the app it is inspecting. Pointer parameters are now resolved rather than converted: `null` passes through, an object id is looked up **and type-checked** against the parameter's `QMetaObject` (a resolved-but-wrong-type object would otherwise reach the callee's own `qobject_cast` and come back as garbage), and anything else is `kInvalidParams`. Reproduced as a SIGSEGV in `test_meta_inspector` before fixing. Resolving ids also makes pointer-taking helpers callable for the first time | `meta_inspector.cpp` | S |

---

## Proposed branch / PR split

Ordered by value × independence. Each row is one PR off `main` unless noted.

| Branch | Contents | Status |
|--------|----------|--------|
| `fix/probe-introspection-gaps` | #1 subclass search, #2 dynamic props | ✅ merged |
| `fix/value-serialization` | C1 enums/flags, C2 gadgets/QObject*, C3 metadata | ✅ merged (C4 bytes deferred) |
| `feat/sync-and-signals` | #4 signal arg values | ✅ done. ⏳ still to add: T1 wait primitive, O5 NOTIFY-watch |
| `feat/native-diagnostics` | O1 native console capture, O2 probe-log separation, O7 drop visibility | ⏳ planned — low-effort, high observability ROI |
| `fix/probe-hardening` | R7 bind policy (default unchanged), R4 version + protocol handshake, R1 request timeout | ✅ done |
| `fix/transport-robustness` | R3 auto-reconnect, R5 structured errors, R6 multi-client, **R7 authentication (the real fix; blocks any change to the bind default)** | ⏳ planned — reliability |
| `feat/model-introspection` *(epic)* | M1 proxy mapping, M2 headerData, M3 setData/selection, M4 change taxonomy | ⏳ planned |
| `feat/qml-support` | Q2 visual tree, Q3 drive Quick items, QML a11y/events/models | ✅ done and live-validated; only advanced Q1 metadata remains |
| `feature/input-modifiers` | T3 mouse/key modifiers across `qt.ui.*` and `cu.*`, R10 `invokeMethod` pointer-argument crash | ✅ done |
| `feat/interaction-enhancements` *(epic)* | T4 hover, T5 multi-select, remaining T6 menu depth, T7 diff, T9 focus, T11 multi-chord, T12 held keys | ⏳ planned; T3 modifiers and T8 DPR are shipped |
| `ci/real-probe-python-e2e` | Run the existing complicated-app and replay E2E suites against built CI artifacts | ⏳ planned — closes R8 |
| `release/post-v0.1.5` | Publish the accumulated probe, Python, MCP, QML, replay, and mobile work | ⏳ operational next step |

Dependency / push notes:

- Authentication should precede any expansion of the default LAN surface.
- The existing real-probe E2E suites should become blocking before relying on
  them as release evidence.
- T1 wait and O5 NOTIFY-watch build on the shipped signal-argument capture.
