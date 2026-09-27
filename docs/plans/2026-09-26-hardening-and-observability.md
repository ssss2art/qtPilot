# Hardening and observability implementation plan

Status: three stacked implementation branches, published as PRs #67, #68 and #69.
Nothing is merged or released.
Original review baseline: local `main` at `640773a` on 2026-09-26. The findings
below describe that baseline; implementation evidence is recorded at the end.

## Direction and review findings

Preserve qtPilot as a Qt-aware observability and control plane for processes on
one machine or across a network. Replay should consume this infrastructure;
it should not own discovery, health, identity, or the event stream.

Scope clarification: the recently introduced replay scripts, CLI options, scenario
formats, and comparison defaults are not compatibility commitments. They may be
revised together with their callers, tests, and documentation. Do not add adapters
or preserve weak semantics just to keep these experimental scripts working.
Original desktop/mobile delivery, LAN control/discovery, established API modes,
and host-safety contracts remain requirements. Unsupported replay artifacts should
receive a clear diagnosis and re-recording guidance, not a guessed interpretation.

The user-supplied discussion proposes safer operating profiles, authenticated
identity, capabilities, health, topology, coordinated snapshots, causal traces,
and stronger replay contracts. These are useful directions, with these corrections
from current source:

- LAN discovery and driving are explicit product requirements. Unconfigured
  all-interface binding is pinned by executable tests. Changing that default
  in an incidental hardening PR would break today's contract.
- Multiple probes on one host and multiple discovery listeners already exist.
  Discovery keys are address + PID + port; the probe still accepts one active
  WebSocket client. Multiple nodes does not require multiple writers per probe.
- Signal waits already exist in Python, with buffering, freshness, deadlines,
  disconnection results, and fluent assertions. Property/model waits and replay
  integration remain work. Do not introduce blocking waits in probe handlers.
- WebSocket requests are already queued outside frame dispatch. This addresses
  reentrancy, but does not move Qt object access off the GUI thread or make a
  blocking application handler interruptible.
- Replay has baseline observations in step zero, format versioning, watch lists,
  normalization, and unordered notification comparison. It does not fail a bad
  baseline before driving subsequent actions: comparison happens after execution.
- Replay still defaults to a 0.1-second settle interval. Every `qt.sync` exception
  is swallowed; unavailable synchronization and broken synchronization are conflated.
- Parsing already normalizes observations and notifications. A strict comparison
  option cannot recover identifiers/timing that parsing has discarded.
- Notification loss counters exist internally; the status resource omits them.
  Recursive recording visits immediate children, not the whole subtree. Recorder
  timestamps are receive times, not source emission times.
- Python CI does not supply the binaries used by the existing real-probe E2E
  fixtures. Their expected path is `build/bin`, while preset builds use
  `build/<preset>`. Merely adding another pytest command will not close this gap.

The existing [gap register](../OBSERVABILITY-GAPS.md) is useful but partially
stale (notably waits and dispatch). Reconcile its entries as each PR lands;
do not replace it with an independent, competing roadmap.

## Contracts to preserve

| Contract | Evidence / regression gate |
| --- | --- |
| Cross-machine LAN control and broadcast discovery remain available | `src/probe/transport/bind_policy.h`, `tests/test_bind_policy.cpp`, `tests/test_websocket_server_bind.cpp`; add a real two-host test |
| Unconfigured legacy behavior remains LAN; explicit restrictions never widen | Existing bind tests stay intact until a separately versioned migration |
| Several probes/listeners coexist without wrong-process connections | `python/tests/test_discovery_concurrency.py`, port collision tests, `qt.ping` process identity |
| Non-browser drivers work; browser Origin checks remain enforced | `websocket_server.cpp`; add real handshake tests for absent, trusted, and untrusted Origins |
| Desktop injection needs no target source changes | Launcher + real Widgets/QML test apps on supported desktop platforms |
| Mobile probe is development-only and static startup survives linking | `docs/MOBILE.md`, installed static-consumer CI; authentication is never permission to ship a probe in release apps |
| Native, computer-use, and Chrome modes retain method/argument meaning | `test_probe_method_contract.py`, mode/tool suites and replay classification tests |
| Revised replay scenarios have explicit, trustworthy semantics | Current-schema validation, round-trip tests, CLI/MCP suites; reject unsupported artifacts clearly; no requirement to preserve experimental formats/defaults |
| Request parameters are never wildcard-normalized before driving | Existing replay tests; literal input and object identifier regressions |
| Qt thread ownership and host lifecycle remain safe | Registry/inspector tests, worker-owned fixtures, dispatch/teardown suites, ASan/UBSan and appropriate TSan checks |
| Protocol skew remains inspectable in legacy sessions | `test_connection_handshake.py`; new capabilities degrade explicitly, secure profiles fail closed |
| Diagnostics are bounded and cannot silently certify missing evidence | Queue tests, new loss/completeness assertions, disabled-instrumentation tests |

## Red/green and commit discipline

For each behavioral slice:

1. Express the requirement in a scenario and domain matcher. Extend
   `tests/common/qt_matchers.h` and `python/src/qtpilot/fluent.py` only when a
   recurring concept merits vocabulary. Test matcher failures and their diagnostics.
2. Run the new test against the previous implementation. Save the command,
   failing assertion, exit code, and revision. Import/build/setup failures alone
   are not proof of a behavioral red.
3. Implement the smallest change; rerun the same test to green, then its affected
   regression suites. Use typed frozen records, Python `Result`, and C++23
   `std::expected` where operations can fail.
4. Commit the test and implementation together as one small, green behavioral
   unit. Record red/green evidence in the PR. Refactor in a separate green unit
   when useful; do not leave deliberately failing commits on a shared branch.
5. For already-correct contract characterization, prove the test catches an
   isolated, deliberate regression and restore it before committing. Report this
   as a negative control, not a newly discovered defect.

Unit tests should use controllable clocks, notifications, and barriers rather
than arbitrary sleeps. Real-probe tests use bounded deadlines and assert the
event/state that completed the operation. Keep direct scalar, race, fuzz, and
platform checks where DSL wrapping would obscure the failure.

Collect commands, output, revisions, and exit codes in separate red/green run
directories under `logs/`, following [the evidence convention](../../logs/README.md).
Generated runs remain ignored; track only explicitly selected, reviewed evidence
when durable retention helps explain a defect or demonstrate a result. Include
redaction/provenance notes and never overwrite a retained artifact with a later run.

Example intended vocabulary (proposals, not existing APIs):

```python
expect_replay(result).to_fail_precondition("ready").to_have_driven_actions(0)
expect_replay(result).to_complete_checkpoint("saved").to_have_no_evidence_loss()
expect_replay(result).to_have_comparison_evidence(exact=18, normalized=3, wildcard=2)
expect_fleet(snapshot).to_report_unavailable("node-b").to_bound_collection_time()
```

```cpp
QEXPECT_THAT(attempt, DeniedBeforeDispatch());
QEXPECT_THAT(profile, AllOf(ListensOnLan(), RequiresAuthentication()));
QEXPECT_THAT(trace, HasObservedSequence({"action", "signal", "checkpoint"}));
```

### Fluent pytest specifications are a separate requirement

PySide supplies the application under test; it does not supply the assertion DSL.
Use pytest fixtures for lifecycle/cleanup, parametrization for behavior matrices,
and async tests for wire operations. Build domain fluency with our own typed
scenario builders and expectation objects; no additional fluent-test or Gherkin
framework is required for this plan.

Current `qtpilot.fluent` already provides `expect_replay`, `expect_signal_wait`,
and `expect_wire_methods`. Extend that vocabulary in small red/green units rather
than scattering repeated dictionary lookups and boolean assertions through each
new feature. Keep test setup/builders in test helpers, independent of PySide;
expectation objects should inspect observed typed results without driving actions,
sleeping, retrying, or computing their expected answers with production code.

| Specification concern | Intended domain vocabulary |
| --- | --- |
| Authentication and exposure | Denied before dispatch; authenticated LAN session; restricted local profile |
| Discovery and identity | Distinct sessions; expected process; stale node; verified versus advertised identity |
| Replay readiness and execution | Failed precondition; zero actions driven; completed checkpoint; aborted action versus divergence |
| Observation confidence | Exact/normalized/wildcard evidence; loss count; unknown coverage; incomplete result |
| Signals, properties, models | Expected signal arguments; property reaches value; model reset/update observed; destroyed target |
| Runtime and fleet | Unresponsive node; bounded collection; partial snapshot; observed order versus inferred causality |

Introduce a builder only when it makes the scenario's initial state, action,
completion evidence, or expected outcome clearer. Keep one behavior per test and
use meaningful pytest parameter IDs such as `immediate-completion`, `stale-signal`,
and `destroyed-target`. The test should read as setup, action, then domain
expectation; wire plumbing and backend-specific construction belong in fixtures.

Reuse the same relevant scenario semantics and expectations across controlled
unit doubles, native C++ test applications, and qualified PySide applications.
Use adapters only for setup/execution differences; do not force native ownership
or platform-specific cases through an artificial common abstraction. E2E tests
must observe effects through the real probe/controller, not inspect fixture state
directly and call that a passing wire contract.

**Acceptance gate for every behavioral slice:** show the fluent specification's
behavioral red and green, plus tests that its matcher rejects the relevant wrong
result with a useful explanation. Diagnostics should name the scenario/step,
expected versus observed evidence, and the failure category. Use pytest assertion
rewriting for test helpers where appropriate or explicit rich mismatch messages;
a chainable boolean wrapper alone is not sufficient. A smoke launch or PySide
compatibility pass does not satisfy this specification gate. Keep direct scalar,
serialization, fuzz, and race assertions where they express the contract best.

## Consolidated integration PRs

Use three initial integration PRs, each containing small, independently explained
green commits. The numbered entries below are implementation slices and working
labels, not a commitment to one PR per entry.

| PR / actual branch | Included slices | Integration acceptance |
| --- | --- | --- |
| 1. `test/validation-foundations` | Documentation/log convention, 0 contract specifications + fluent DSL, 1 required real-probe E2E + CI selection, 1a PySide exploration | Tested CI selection and failure propagation; native E2E executes; explicit PySide fixture decision |
| 2. `feat/authenticated-operating-profiles` | 2 authentication, 3 effective operating profiles | Authenticated LAN/local behavior, denied calls cannot dispatch, Origin defense, actual platform sockets and static startup |
| 3. `feat/replay-contracts-and-checkpoints` | 5 loss accounting, 6 evidence integrity, 7 preconditions, 8 signal checkpoints | Initial-state failures drive zero actions, completion uses evidence, incomplete runs cannot receive a strict pass |

Broader native console capture from slice 5, identity/capabilities (4), property/
model watches (9–10), runtime health/settling (11–12), causal timelines (13), and
fleet snapshots (14) remain follow-up work. Decide their PR boundaries after the
first milestone. Each follow-up must retain the DSL and contract gates below.

Develop and prove behavioral red/green locally, collecting `logs/` evidence.
Push batches of already-green commits at useful review checkpoints; a commit
does not need its own hosted matrix run. Keep PRs draft during iteration, then
request broader validation on the final reviewable revision. Every subsequent
code change invalidates affected validation and requires fresh checks; batching
does not permit accepting stale evidence. Split an integration PR if its diff
becomes hard to review; runner consumption is primarily controlled by CI selection
and push cadence, not by combining unrelated changes into a giant PR.

## CI selection and runner consumption

PR 1 implements this policy in `scripts/ci_policy.py`, with tested scope
selection and a final `Validation gate`. Hosted execution and branch-protection
wiring remain separate validation gates. The original full workflow expands
to 27 jobs: 13 desktop builds, 3 static-probe builds, 2 mobile builds, 2 sanitizers,
5 Python stacks, lint, and benchmarks. Six desktop jobs use Windows; three jobs
use macOS across desktop/static/iOS. Cancellation of superseded runs already
exists and should be retained.

| Change / stage | Required evidence | Hosted runner scope |
| --- | --- | --- |
| Docs, policy, ignore rules, or retained evidence only | Links/format, ignore-policy behavior where changed, stable CI gate | Lightweight Linux only; no Qt builds |
| Ordinary replay/controller/DSL Python changes | Focused local red/green, full Linux Python suite, floor/latest coverage; real-probe Linux E2E for behavior crossing the wire | Linux; extra MCP stacks when SDK/registration/dependencies are affected |
| PySide fixture changes | Pinned-runtime qualification with real C++ probe; native fixture remains covered | One Linux combination initially; expand platform support only when needed |
| Shared C++/transport changes during draft iteration | Relevant local Qt tests, Linux Qt 5 floor + representative Qt 6, real-probe E2E, sanitizers as affected | Linux fast lane; supported-platform proof required before merge |
| Platform-specific changes | Linux gate plus native tests/build on the affected platform | Relevant macOS/Windows legs on ready revisions; local macOS proof may support diagnosis but is recorded separately |
| Shared probe/authentication/lifetime/linking changes ready for merge | Full supported desktop matrix and affected static/mobile/sanitizer gates on the final revision | Broader hosted validation once the candidate is ready, then again only when new changes require it |
| Release | Full supported build/artifact set, package/download consistency and release validation | Existing supported platforms/architectures retained |

Change selection must account for transitive impact, not just file extension:
Python native/tool/connection changes need Linux wire tests; CMake/common headers
can affect every desktop and mobile build. WebSocket teardown/dispatch changes
must retain checks on Qt 6.8/6.9, where historical regressions occurred, alongside
floor/current versions. Static linking/startup changes require installed consumers
and mobile compile gates. Python metadata/lockfile/MCP compatibility changes need
the complete affected SDK/interpreter matrix. Unknown paths, classifier errors,
workflow changes, and ambiguous diffs select broad validation conservatively.
Classify the complete PR diff, including deleted/renamed files, not just its last
commit. Capture the selected checks and the reason in a summary.

Implementation requirements for CI selection:

- Keep an always-triggered lightweight classifier and final required gate on PRs.
  The gate verifies every selected check succeeded, treats failed/cancelled or
  unexpectedly skipped checks as failure, and rejects an unvalidated ready code
  change. Workflow-level path skipping can leave required checks pending; see
  [GitHub's required-check guidance](https://docs.github.com/en/pull-requests/how-tos/merge-and-close-pull-requests/troubleshooting-required-status-checks).
- Use normal PR events, including ready-for-review and maintainer-requested
  escalation, with explicit draft/ready behavior. Gate requests against the
  current revision. A manual dispatch is useful for diagnosis but must not be
  assumed to satisfy PR branch rules. Do not use `pull_request_target` to run
  untrusted PR code with elevated permissions.
- Test the selector and gate with fluent cases: docs-only schedules no Qt;
  Python wire changes schedule E2E; shared headers select all relevant platforms;
  unknown paths widen; missing/failed/cancelled selected jobs block acceptance;
  release mode produces every downloadable target. Prove these reds before
  implementing job conditions.
- Reuse the representative Linux build for native E2E and compatible fixture
  runs at the same revision rather than building the same kit in a separate job.
  Qualify PySide runtime compatibility first; do not reuse an incompatible binary.
  Keep the complete release target list canonical: reduced PR matrices must not
  shrink `download.py`'s supported targets or break the matrix-consistency test.
  Update that test to compare the full release matrix if matrix generation moves.
- Delay expensive jobs until cheap gates pass. Keep dependency/Qt caches and
  SHA-pinned actions; add compiler caching only with toolchain/configuration-safe
  keys. Caches improve execution time but never establish test correctness.
  Routine PRs need test reports, not every platform's bundled release artifacts.
- Run benchmarks when relevant performance paths change or deliberately requested;
  do not run a benchmark build for documentation or ordinary Python-only changes.
  Avoid a new nightly full matrix by default. Cancel obsolete PR runs, bound job
  deadlines, and rerun failed jobs selectively instead of restarting all green legs.
- Avoid duplicating the full ready-PR matrix immediately on every merge to `main`.
  Run appropriate fast integration checks there; run broad checks again when the
  merged tree differs materially or at release. Never treat PR-head evidence as
  proof of a different merged tree. All Python and QML source/test paths must be
  included in change selection; current main-push filtering misses some of them.
- Keep red/green output in local `logs/`; upload only necessary CI reports/failure
  evidence with bounded retention. Retaining an Actions artifact and committing
  evidence to Git are separate decisions. Measure job-seconds by runner/platform
  and changed scope before/after; no percentage or dollar savings are established
  by this plan.

The repository is currently public (verified through the GitHub API on 2026-09-26).
GitHub's [billing documentation](https://docs.github.com/en/billing/concepts/product-billing/github-actions)
states standard hosted runner usage in public repositories is free; larger runners
and storage have separate billing rules. No account billing settings were inspected.
Resource-conscious selection remains appropriate regardless of who pays, and the
same policy reduces billable usage if the project moves to a private repository.

## Implementation slices

Within each integration PR, implement the included slices in dependency order.
Branch each subsequent integration PR from updated `main` after its prerequisite
lands; avoid a long-lived omnibus branch or a mandatory 16-PR stack.

### 0. `test/project-contract-specifications`

**Purpose:** establish executable compatibility gates before changing semantics.

Establish the first reusable Python scenario helpers and domain expectations
alongside the contracts that use them; test their failure messages as well as
their success paths. Grow the DSL with subsequent feature branches, not in a
large speculative infrastructure PR.

Build on existing matchers and fixtures. Add discovery parsing/key tests for
two hosts, two processes on one host, stale expiry, goodbye, and malformed
datagrams. Add real Origin-handshake regressions and one-active-client tests.
Retain bind defaults, literal replay parameters, established mode
classification, and installed static startup contracts.

**Red:** deliberate isolated changes to exposure, identity key, Origin rejection,
or replay input preservation must fail the corresponding domain assertion.
**Green:** original implementation restored, all contract tests pass. This is
contract characterization, not feature implementation.

### 1. `ci/required-real-probe-e2e` (depends on 0)

Provide an explicit test binary directory rather than hardcoded `build/bin`.
Run Python E2E in a job with a matching launcher, probe, test app, and Qt runtime
(initially a representative Linux build). In that job, missing artifacts,
unexpected skips, and zero collected E2E tests fail. Ordinary Python-only
development may still skip unavailable binaries. Add a Widgets path and a QML
path, plus process-group cleanup on failure. Retain locked dependencies,
SHA-pinned actions, and the existing platform/static/mobile matrices.

**Red:** point required-E2E mode at missing binaries; observe failure, not skip.
Then break a real wire method in an isolated negative control and show the
cross-layer test fails. **Green:** built artifacts drive the application and the
job reports executed tests. Making the job required in hosting branch protection
is a separate repository-setting action, not accomplished by YAML alone.

### 1a. `test/pyside-probe-compatibility` (depends on 0, 1)

Local qualification is recorded in [PYSIDE-QUALIFICATION.md](../PYSIDE-QUALIFICATION.md).
Widgets/property access passed, but a typed Python slot failed. Retain native
acceptance fixtures; binding qualification remains incomplete.

**Purpose:** qualify Qt for Python as a source of expressive real-Qt test fixtures
before checkpoint and watch work relies on it. This is a prerequisite exploration,
not a claim that qtPilot already supports injection into Python Qt applications.
Authentication and profile work can proceed independently.

Use the shared fluent pytest vocabulary above for qualification assertions;
PySide-specific helpers construct the target, while domain expectations remain
usable with native fixtures and controlled unit doubles.

Use PySide6 for the initial experiment. Qt's Python bindings expose signals,
slots, and properties through the Qt meta-object system, which is the boundary
the C++ probe inspects. Declare fixture methods with typed `@Slot` decorators
and observable state with Qt `Property` and NOTIFY signals; ordinary Python
attributes are not automatically Qt properties. See the official
[signals/slots documentation](https://doc.qt.io/qtforpython-6/tutorials/basictutorial/signals_and_slots.html)
and [property documentation](https://doc.qt.io/qtforpython-6/PySide6/QtCore/Property.html).

Keep the responsibilities explicit:

| Layer | Implementation and role |
| --- | --- |
| Probe | C++ injection, Qt hooks, object tracking, transport, host safety |
| Controller and specifications | Existing Python WebSocket client, replay, pytest, fluent assertions; no Qt binding needed |
| Synthetic application fixtures | Optional PySide6 Widgets/QML objects, signals, properties, models, controlled asynchronous behavior |
| Native safety and compatibility tests | Existing C++/Qt Test/GMock, sanitizers, Qt 5/6 and static/mobile coverage |

The controller and target remain separate processes. Do not embed Python in the
probe, generate bindings for probe internals, or bypass WebSocket with direct
Python object access in tests that claim end-to-end coverage. PyQt qualification
can be a later independent slice; PySide success does not establish PyQt support.

PySide wheels include Qt binaries, as described in
[Qt's installation guide](https://doc.qt.io/qtforpython-6/gettingstarted.html).
The injected probe and Python target must use a compatible runtime. Record the
Python, binding, Qt, architecture, probe build, loaded-library paths, and plugin
paths for each tested combination. Start with one pinned Linux combination;
verify actual library loading, Qt private-hook compatibility, import/startup order,
and shutdown instead of assuming equal version strings are sufficient. Expand
to macOS/Windows only with executable evidence. Distinguish loader incompatibility
from a feature's behavioral red.

Keep PySide in a locked, optional test environment/extra. The published controller
must not acquire a Qt dependency, and existing Python-only tests must still run
without PySide. Missing bindings/binaries fail the designated compatibility job;
they must not turn that required job into a green skip.

Qualification slices, each with a small green commit:

1. Launch a minimal Python Widgets application through the real launcher/probe;
   verify ping/process identity and object discovery. Prove the fixture expectation
   fails when the probe is absent or the wrong process is connected.
2. Read/write a Python-defined Qt property and invoke a typed Python slot through
   the wire; assert the application effect. A deliberate broken accessor/slot
   negative control must fail the fluent assertion.
3. Subscribe to a Python-defined signal and verify typed arguments for immediate
   and queued emissions using explicit barriers. Wrong arguments, missing emissions,
   and stale emissions must fail distinctly. Preserve Qt thread affinity; exercise
   callbacks into Python with bounded deadlines to catch hangs.
4. Destroy/recreate watched objects and verify notification, handle invalidation,
   disconnect, and process cleanup. Retain references/Qt parent ownership deliberately;
   distinguish Python wrapper collection from actual QObject destruction.
5. Qualify a small model update/reset and a QML fixture through the same probe.
   Do not claim either capability based only on the initial Widgets smoke test.

**Exit decision:** document tested combinations, red/green commands, actual wire
results, limitations, and cleanup evidence. If compatible, use these fixtures for
immediate completion, delayed updates, destruction, and overflow scenarios in
branches 8–10. If not, retain native C++ fixtures and record the loader/binding
limitation plus follow-up work; this exploration must not indefinitely block
checkpoint improvements or weaken the original native contracts. No compatibility
result has been established yet.

### 2. `feat/probe-session-authentication` (depends on 0, 1)

Write a short protocol/security design in this branch before implementation:
credential provisioning without target source edits; authenticated transport;
client and server identity; admission states; deadlines; old/new compatibility;
redaction; cleanup. Prefer established TLS/tunnel and authentication mechanisms
over inventing cryptography. Bearer credentials over plaintext LAN WebSockets
are not a secure remote mode. Validate Qt 5/6 and mobile support before selecting
the implementation.

Implement probe and Python client together. When authentication is configured,
unauthenticated requests cannot dispatch methods, subscribe, inspect, or reserve
the sole active-client slot indefinitely. Bound pending handshakes. Authentication
failure must never fall back to an unauthenticated connection. Do not put secrets
in URLs, UDP discovery, recordings, status output, fixtures, or logs. Preserve
legacy unauthenticated behavior only in the explicitly documented compatibility
path; report its exposure honestly. Origin checks remain an independent defense.

**Red:** wrong/missing credentials dispatch a sentinel method; stalled admission
blocks a valid client; secrets appear in logs; secure connection downgrades.
**Green:** denied calls have zero application effects, valid local/LAN clients
work, failed attempts clean up, legacy mode still works as specified. Include
encrypted transport/server-identity tests before claiming remote security.

### 3. `feat/explicit-operating-profiles` (depends on 2)

Add explicit `local`, `trusted-network`, and `remote` profiles with one effective
configuration shared by launcher, injected/static probe, Python server, and
discovery. Proposed policy: local = loopback/no UDP announcements; trusted-network
= LAN discovery plus authentication; remote = verified encrypted access and
authentication, no automatic public exposure. A remote profile without its
required transport fails closed. Network location alone does not establish trust.

Specify profile/environment/CLI precedence. Reject conflicting explicit settings
instead of silently widening access. Invalid profiles cannot become legacy LAN.
Unset profiles retain existing bind and announce behavior, including loopback
announcements under the existing bind setting. New explicit local behavior is
distinct from that legacy setting.

**Red:** conflicting options widen a local profile; remote starts without its
requirements; profile changes make a LAN peer undiscoverable unintentionally.
**Green:** policy table matches sockets and discovery in integration tests.

Changing the unconfigured default is deliberately outside this PR: propose it
only with a versioned migration, release notes, and tested rig configuration.
Safer opt-in profiles do not themselves fix the legacy unauthenticated default.

### 4. `feat/probe-identity-and-capabilities` (depends on 2, 3)

Add per-process-session identity to discovery and handshake/ping, optional
operator-provided persistent node identity, and negotiated capabilities/auth
requirements. Keep existing discovery fields and legacy address/PID/port fallback.
Do not automatically persist identity into arbitrary target application storage.
Treat announcements as untrusted hints; verify identity on the authenticated
connection before using it for selection or trust. Bound/validate payloads and
handle duplicate IDs and reordered stale goodbye packets.

**Red:** restart or PID reuse inherits the prior session; spoofed discovery
establishes trust; one node replaces another. **Green:** sessions stay distinct,
restarts are explicit, legacy peers remain discoverable. Separate "supported",
"enabled", and "unavailable" capabilities; build flags alone do not prove runtime
availability. Multi-client writing is not part of this branch.

### 5. `feat/native-diagnostics-and-loss-accounting` (depends on 0, 1)

Expose native Qt warning/critical capture using the existing capture facility;
separate probe logging by category/source rather than blanket text suppression.
Preserve the host's existing message handler and fatal behavior. Surface probe
queue, Python queue, signal-buffer, and capture-buffer loss/limits through
status and typed evidence records. Distinguish zero loss from unknown counters.
Keep buffers and payloads bounded; instrumentation is optional.

**Red:** application warnings disappear with probe chatter, previous handler is
not called, an overflow looks like complete evidence. **Green:** warnings remain
observable, host logging behavior survives, overflow is reported. Fatal-message
tests run in subprocesses; no claim of reliable post-crash in-process reporting.

### 6. `fix/replay-evidence-integrity` (depends on 5)

Retain raw observation/notification data at ingestion and normalize only at
comparison. Define comparison policies and round-trip semantics for the revised
schema without retaining experimental defaults. Report exact, normalized,
wildcard, unsupported, and unavailable
evidence with defined counting units and field paths. Define a strict policy
that rejects ambiguous/wildcard or incomplete evidence, and make it the default
for acceptance runs. Explicit exploratory policies can relax comparison rules
while reporting their weaker evidence. Previously normalized artifacts cannot
recover original evidence; reject them when needed and request re-recording,
rather than relabeling them as exact or building a compatibility adapter.

Distinguish a method-not-found `qt.sync` response from timeout, disconnect,
handler failure, and other errors. Continue without an unavailable synchronization
feature only when the scenario explicitly permits it; required checkpoints fail
if their synchronization feature is unavailable.
Thread evidence through CLI, MCP results, baseline output, and fluent matchers.

**Red:** wildcard/normalized pass looks exact; losses permit a strict pass;
sync timeout is silently ignored. **Green:** revised policies and schema round-trip
correctly, acceptance runs refuse missing evidence, genuine sync failures stop clearly.

### 7. `feat/replay-preconditions` (depends on 6)

Add a versioned scenario contract (or explicitly versioned companion manifest)
with environment metadata, fixture reference, read-only preconditions, and
capability requirements. Validate initial observations and explicit preconditions
before any action by default. Distinguish fixture/setup failure, precondition failure,
unsupported capability, action failure, and application divergence.

Fixture preparation belongs to a bounded caller/harness hook; do not execute
arbitrary shell commands from an imported recording. Do not claim automatic
reset of arbitrary injected applications. Reject unknown required semantics and
obsolete formats clearly; update scripts/callers to the new contract directly.

**Red:** wrong initial state still sends a click; invalid fixture gets called an
application divergence. **Green:** zero mutations on failed prerequisites, clear
failure diagnosis, current-format round-trip and clear unsupported-format errors.

### 8. `feat/replay-signal-checkpoints` (depends on 6, 7, and the 1a fixture decision)

Integrate existing `SignalWaiter` into action checkpoints. Arm subscription and
an emission cursor/barrier before driving; bind the wait to the correct session
and expected signal/argument predicate. Do not call `fresh=True` only after the
action: that can discard an immediate completion signal. Map freshly returned
subscription IDs rather than trusting IDs from a previous recording.

Bound wait, cancel, disconnect, destroyed target, and dropped evidence behavior.
Remove subscriptions on every path. Replace fixed settle delays as the acceptance
synchronization mechanism: checkpoints wait for declared evidence, and missing
conditions do not silently fall back to elapsed time. Existing `--settle` options
and callers may be removed or revised; an explicit pacing delay, if retained,
must not count as completion evidence.

**Red:** immediate completion is lost, stale emission satisfies a new action,
or timeout drives the next step. **Green:** these races are proven using barriers,
cleanup is verified, and a real queued application action reaches its checkpoint.

### 9. `feat/property-notify-watch` (depends on 5, 8)

Add read/subscribe/recheck handling for NOTIFY properties, with an explicit
predicate and deadline. No-NOTIFY properties return unsupported or use explicitly
requested bounded polling. Report sampling time/sequence; old/new values are
observed samples, not necessarily every intermediate application value.

**Red:** a property changes between read and subscribe and the wait times out;
target destruction or disconnect looks successful. **Green:** races are covered,
failure categories and cleanup are correct, replay uses property checkpoints.

### 10. `feat/model-change-watch` (depends on 9)

Add structured rows-inserted/removed, reset, layout, and data-change events using
the shipped signal support. Preserve role/range semantics and model identity;
handle reset/destruction without using invalid indices. Provide model predicates
for checkpoints. Keep proxy mapping, header access, editing, and selection APIs
as separate small follow-up PRs rather than expanding this synchronization slice.

**Red:** a reset leaves a stale model/index checkpoint looking complete or a
background load finishes after the runner has already asserted. **Green:** actual
model completion is observed and lifecycle failures remain explicit.

### 11. `feat/runtime-health` (depends on 4, 5)

Start with bounded, optional event-loop responsiveness/stall sampling, thread
identity/affinity metadata, and portable process resource samples. Name the
sampling window, clock, source, and unavailable fields. A heartbeat scheduled
on a blocked GUI thread cannot report its stall live; the controller must report
stale/unresponsive independently, and the probe can report the delay on recovery.

Do not read worker-owned QObjects from the GUI thread. Timer inventories and
thread-pool queues are follow-up capability work: expose only what can actually
be observed, with coverage limits. Benchmark enabled/disabled overhead on a
synthetic large app before choosing defaults; no unmeasured performance claims.

**Red:** a deliberately stalled loop stays "healthy", unsupported metrics become
zero, or instrumentation materially perturbs the scenario beyond an agreed budget.
**Green:** stale health is explicit, sampled measurements have bounds, instrumentation
is bounded and disabling it preserves ordinary automation.

### 12. `feat/observable-settling` (depends on 8, 9, 10, 11)

Define an explicit observed-stability contract: selected property/model predicates
hold, selected streams are quiet for a bounded window, and the loop is responsive.
Return which conditions were observed, timeout, and incomplete evidence. Future
timers, queued worker work, network replies, and arbitrary external systems can
invalidate later state; dispatcher idle alone is not global quiescence.

**Red:** a dispatcher-idle moment before a delayed model change is called globally
settled. **Green:** declared conditions control the checkpoint; timeout/unknown
coverage are honest. Retain explicit signal/property checkpoints as stronger
domain evidence when available. No blanket deterministic-execution guarantee.

### 13. `feat/causal-timeline` (depends on 4, 5, 8)

Introduce a versioned, bounded event envelope with session identity, sequence,
source/receive times, action/operation IDs where observed, and loss markers.
Correlate deferred admission with completion/failure where instrumentation can
see it. Keep stable event identity separate from mutable object lookup paths.
Fix recursive recording in a small commit: full bounded traversal, deduplication,
dynamic additions/removals, and subscription limits/partial-coverage reporting.

Add explicit ordered and unordered groups with documented revised defaults.
Label events within an action window as temporal association; do not imply the
action caused every concurrent event. True cross-thread/cross-node causality
requires propagated context or an optional application integration.

**Red:** independent events acquire fabricated parentage; out-of-order delivery
changes a declared sequence unnoticed; overflow looks complete; deep descendants
are missed. **Green:** sequence policies work, association strength is visible,
current-schema transcripts round-trip, unsupported artifacts fail clearly, and
event loss bounds the conclusions.

### 14. `feat/fleet-snapshots` (depends on 4, 11, 13)

Add controller-owned sessions per probe without changing each probe's one-client
contract. Separate this from MCP mode selection and preserve existing single-probe
APIs. Collect concurrently with per-node deadlines and a total time bound; report
collection start/end, session, clock uncertainty, busy/disconnected nodes, and
partial completeness. A coordinated snapshot is not an atomic distributed state.

Optional operator/app topology metadata must be explicitly sourced, bounded, and
validated. Unknown edges stay unknown. Application metadata hooks are additive;
desktop injection and basic fleet inspection still need no application edits.

**Red:** one stuck/busy node hangs the fleet, a restarted node inherits an old
snapshot, or partial data produces a fleet-wide pass. **Green:** nodes are isolated,
partial failures are explicit, and a real two-host network run succeeds.

## Dependency lanes and recommended first milestone

```text
contracts -> required real-probe E2E
                    |-> PySide compatibility decision -> checkpoint fixtures
                    |-> authentication -> profiles -> identity/capabilities --+
                    |-> diagnostics/loss -> replay integrity -> preconditions |
                                                         -> checkpoints      |
                                                            -> properties    |
                                                               -> models     |
identity + diagnostics -> runtime health -> observed settling                 |
identity + diagnostics + checkpoints -> timeline -> fleet snapshots <---------+
```

Start with integration PR 1 (slices 0, 1, 1a), including CI consumption controls,
then PR 2 (authentication/profiles) and PR 3 (replay trustworthiness/checkpoints).
Grow the DSL through small green commits and run hosted checks by affected scope
at review checkpoints. Qualify PySide before relying on it for checkpoint/watch
development; retain native fixtures if qualification fails. The first milestone
is authenticated LAN driving plus a scenario that refuses a bad initial state,
waits for real completion, and cannot issue a strict pass with missing evidence.
Identity, richer runtime metrics, settling, topology, and fleet aggregation follow
on proven foundations rather than delaying that milestone.

Release/publication is a separate step after merge, not another implementation
epic. Probe and Python artifacts must advertise compatible versions/capabilities;
prove registry/downloaded artifact behavior independently of build success.

## Validation and remaining decisions

- Every implementation PR: saved behavioral red, same test green, relevant contract
  gates selected by the CI policy above, matcher failure diagnostics, zero Python/
  compiler warnings, and updated docs. Documentation-only changes use lightweight
  verification rather than an unnecessary native build.
- Wire changes: real probe/CLI/MCP test, serialized schema assertions, legacy/new
  client-probe combinations and explicit capability fallback/denial.
- Replay changes: update scripts, CLI/MCP callers, scenario fixtures, and docs
  together. Verify the revised schema and failure semantics; obsolete experimental
  script/format compatibility is not a merge gate. Retain tests of genuine safety
  properties such as verbatim action inputs, even when surrounding APIs change.
- PySide fixtures: branch 1a's compatibility decision precedes their use as
  checkpoint/watch acceptance evidence. Keep at least one native C++ real-probe
  scenario alongside Python fixtures; binding behavior is additional coverage,
  not proof of native lifetime/thread safety or Qt 5/mobile compatibility.
- C++ changes: local/fast checks during iteration, then existing Qt 5.15 Linux/
  Windows and supported Qt 6 desktop matrix on the ready integration revision;
  Widgets/QML/no-QML builds as affected. Preserve Android/iOS cross-build and
  installed static-consumer startup checks. Run sanitizers for lifetime/thread
  work; local Python success is not C++ or device proof.
- LAN changes: two probes on one host plus a real separate-host test for discovery,
  authenticated connection, process selection, restart, and disconnect. Loopback
  and mocked datagrams alone do not establish the original network requirement.
  Begin with a repeatable two-host script/runbook if a second CI host is unavailable;
  explicitly report that gate as external until automation exists.
- Do not introduce multi-writer probe control, automatic retries of mutations,
  application reset, global deterministic scheduling, or crash-handler interception
  in these branches. Those need independent ownership/safety contracts.
- Decide transport/provisioning in branch 2's design; decide exact new scenario
  schema in branch 7. The transport decision needs compatibility fixtures; the
  replay decision needs current-schema fixtures and unsupported-format errors,
  without experimental backward compatibility. A default exposure migration needs
  a separate product/version decision; it is not implied by approval of this plan.

Baseline verification for this review: 523 focused existing Python tests passed
using the repository venv on 2026-09-26. Suites: replay, replay driver/format/modes/
watch, signal waits, fluent DSL, probe method contract, connection handshake, and
discovery concurrency. The initial sandbox run prevented UDP binding (512 passed,
11 permission failures); the same command passed outside that restriction with
no code changes. No fresh C++ build, live LAN test, device test, or full matrix was
performed for this documentation-only review. New branches' red/green evidence
remains work to execute, not evidence claimed by this plan.

## PR 1 implementation evidence

Work is on `test/validation-foundations`, in small commits. Local validation:

- Python suite: **1042 passed, 6 skipped**; the opt-in PySide suite and existing
  SDK feature-dependent tests account for optional coverage. Native E2E executed.
- Native CTest: **29/29 executables passed**, including bind, teardown, dispatch,
  introspection, input, model, QML and static initialization-related tests.
- CI selector/catalog suite: **45 passed**; selected-job failures/cancellations/
  skips cannot pass the final gate. SDK registration changes retain all stacks.
- Actionlint `1.7.12`: CI and release workflows passed syntax/context validation.
- Saved behavioral red/green: missing/empty required E2E, malformed discovery
  values, missing property evidence and SDK scope. Isolated native mutations
  changing the LAN default and bypassing Origin protection failed their contract
  assertions; original sources were restored and rebuilt.
- Benchmark deprecation: warning check failed on the old const-reference
  `DoNotOptimize` overload and passed after using mutable output storage.
- PySide: **one property/runtime pass, one typed-slot failure**. See
  [qualification evidence and decision](../PYSIDE-QUALIFICATION.md).

Run outputs remain in ignored `logs/`; no raw test/process logs are tracked.
[PR #67](https://github.com/ssss2art/qtPilot/pull/67) passed all 29 hosted jobs
at `da58691447b853f10a72cb653fd4a7a0fb34695e` (run `36281227602`). GitHub reports `main` as
unprotected with no repository rulesets: YAML supplies a stable `Validation gate`,
but requiring it for merges needs a separate repository-setting decision.
This first branch supplies validation foundations; PRs 2 and 3 below supply
authentication/profiles and replay acceptance respectively.

## PR 2 implementation evidence

`feat/authenticated-operating-profiles` stacks on PR 1. The
[authentication design and runbook](../AUTHENTICATION.md) document bearer admission,
verified TLS, bounded pending sockets, CLI/environment precedence and unchanged
unconfigured LAN behavior. Admission is enforced before client ownership/dispatch;
local/remote discovery is disabled from the same resolved native policy.

- Native suite: **31/31 executables passed** on macOS/Qt 6.11.2, including new
  configuration and admission tests plus original bind, dispatch and teardown gates.
- Python suite: **1092 passed, 6 optional/SDK-dependent skips**, including real
  native TLS, rejection, recording redaction, discovery and launcher tests.
- Saved behavioral red/green includes rejected credentials with zero effects,
  missing/invalid configuration, TLS CA/hostname failures, remote downgrade,
  local/remote discovery leakage, TLS announcement parsing, profile precedence
  and startup URL redaction. A launcher-forwarding mutation fails real TLS admission.
- C++ builds retain `-Werror` for GCC/Clang and `/WX` for MSVC. Python retains
  `filterwarnings = ["error"]`. Explicit deprecation-warning negative controls
  failed under the actual local compiler flags and pytest configuration, then
  passed after removing the deprecated use/warning. Existing narrow third-party
  import-warning exceptions have not been broadened.
- CI policy/catalog: **45 passed**; actionlint passed. The workflow accepts stacked
  PR bases and includes TLS/profile E2E in its required representative Linux job.

The Qt skill review subsequently reproduced two admission issues: an explicitly
empty profile downgraded to legacy, and Origin/second-client rejection retained
pending socket capacity. Both are fixed with red/green cases. Only an absent
profile selects legacy; the established empty **bind-address** behavior remains
unchanged. Rejected wrappers survive until disconnection, independently owned Qt
timers reclaim pending capacity, and stale timer/socket identities are guarded.

[PR #68](https://github.com/ssss2art/qtPilot/pull/68) passed **29/29 hosted jobs**
at corrected revision `219a233` in run `36287690646`, including supported desktop,
static/mobile, sanitizers and the final Validation gate.
The separate-host runbook remains an external gate. Local or
single-host UDP/socket results do not substitute for cross-machine proof.

## PR 3 implementation evidence

`feat/replay-contracts-and-checkpoints` stacks on PR 2. See the
[design decisions](replay-contract-v2.md) and [public replay contract](../REPLAY.md).

- Version-2 JSON contracts are strict by default in CLI/MCP. Exact requirements
  and preconditions gate mutations; every action declares postconditions and/or
  a Qt signal checkpoint. Raw responses and selected assertion paths are retained.
- Owned Qt subscriptions are installed before driving. A bounded asyncio queue,
  pre-action cursor, argument correlation, deadline and `AsyncExitStack` provide
  controller orchestration. Qt owns delivery/lifetimes and a 64-permit semaphore
  bounds exclusive connections. Ordinary observers retain their own subscriptions.
- Probe/controller loss is session scoped; checkpoint loss includes cleanup.
  Unknown/reset/dropped evidence cannot produce a strict pass. MCP status also
  exposes diagnostic signal-buffer loss/eviction and bounded capture counters.
- Experimental transcripts/macros require explicit exploration. Their reports
  disclose normalized/wildcard/unavailable/unsupported evidence and never claim
  exact or loss-verified acceptance. Existing unsafe replay defaults are not a
  compatibility commitment; literal request semantics remain covered.
- Behavioral red/green covers schema rejection, readiness, required sync failure,
  raw fidelity, loss/reconnect, subscription ownership/capacity, early/queued
  completion, stale/wrong correlation, destruction, timeout, cancellation and
  cleanup overflow. Reordering native subscriptions after action delivery fails
  both real Qt completion cases; restoring the order passes them.
- Final local code revision `d5514f9`: **1,183 Python passed, 6 optional/SDK skips**;
  **31/31 native suites passed**; warning-as-error C++ build passed on macOS/Qt
  6.11.2. This includes the original bind/discovery/Origin/client-ownership gates.
  Raw outputs remain ignored under `logs/20260927T0221*`.
- The `qt-agent-skills` review (skill `qt-cpp-review`, revision `71d6c10`) ran
  deterministic lint and all six focused review missions. Its three confirmed
  deep findings have reproduced fixes: the two PR 2 admission cases above and
  unbounded exclusive subscriptions in PR 3. No proprietary application data
  is included in fixtures, documentation or tracked evidence.

[PR #69](https://github.com/ssss2art/qtPilot/pull/69) requests the full supported
matrix for review. Its first hosted run caught a missing direct `QJsonDocument`
include in the transport diagnostics handler on older Qt versions; Qt 6.11
transitively supplied it locally. The superseded run was cancelled to save runner
time, the direct include was added, and hosted validation must prove that fix.
Cross-host LAN acceptance,
physical-device runtime behavior and a required GitHub merge gate remain separate
external gates. Broader console capture, recursive recording, property/model
watches, health/topology/fleet semantics and automatic mutation retry are deferred.
