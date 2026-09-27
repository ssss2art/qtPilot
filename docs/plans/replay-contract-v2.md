# Replay contract v2 implementation design

Implemented on `feat/replay-contracts-and-checkpoints`, the third integration
branch. The public schema, CLI/MCP usage, migration and limits are in
[REPLAY.md](../REPLAY.md). This record explains the implementation decisions.

Acceptance uses an explicit version-2 JSON contract, separate from diagnostic
JSONL transcripts. Unversioned and version-1 transcripts remain inspectable and
usable only for explicitly exploratory replay; they cannot silently become a
strict acceptance baseline. This avoids relabeling previously normalized evidence
as raw or exact. Existing experimental scripts may change directly.

The contract contains a fixture reference, required environment/capabilities,
read-only preconditions and ordered actions. The fixture reference identifies a
caller-prepared state; it never executes shell commands or resets an arbitrary
application. Reject unknown required fields, unsupported versions, invalid method
categories and invalid deadlines before application mutation.

Use Python primitives for controller orchestration: frozen dataclasses for the
contract/results, `json` for exact values, `deque`/`asyncio.Queue` for bounded
buffers, `asyncio.timeout` for deadlines, and `AsyncExitStack` for cleanup. Keep
the fluent DSL focused on observable domain outcomes; pytest provides fixtures,
parametrization and assertion diagnostics. Qt owns signals, QObject lifetimes,
thread-affinity delivery, sockets and timers. Independent signal connections use
a Qt semaphore capacity limit (64); Python does not emulate Qt's event loop.

Each action declares at least one observable postcondition or signal checkpoint.
Observations contain method, verbatim parameters, a result field path and the exact
expected JSON value. Empty paths select the whole result. A selected field is
explicit assertion scope; report that scope. Preserve raw results separately,
including timestamps and handles. Comparison must not silently mask values.
Diagnostic exploratory comparison may normalize existing transcript values but
must be labeled exploratory; wildcard/truncated/unsupported evidence cannot yield
a strict pass.

Before mutation, check initial state, required capabilities and known transport
loss counters. Counters must have a stable session identity and nondecreasing
values. Sample loss around execution; dropped or unknown evidence blocks strict
success. Keep exact/normalized/wildcard/unavailable/unsupported evidence categories
explicit in typed results and CLI/MCP output, with field paths and defined units.

For signal checkpoints, install a bounded waiter and subscribe before driving the
action, use the newly returned subscription ID, establish a pre-action cursor,
then wait for the declared signal/argument predicate. Immediate and queued
completion must both work. A stale buffered emission cannot satisfy the next
action. Do not clear buffers after driving. The predicate should include an
application correlation value when causal attribution matters; observed ordering
alone does not prove causality. Timeout, disconnect, target destruction, loss and
cancellation terminate the step and clean up subscriptions/handlers.

Fixed settling delays may remain as explicitly exploratory pacing. Strict
acceptance waits for declared evidence. Baseline recording must refuse partial,
aborted or incomplete results. Native fixtures remain the acceptance backend
because PySide typed-slot qualification is incomplete.

The existing diagnostic `SignalWaiter` remains useful for interactive MCP waits.
Strict replay uses a small owned checkpoint context because it needs two exclusive
Qt connections, a pre-action cursor and cleanup evidence scoped to one action.
Both use Python's bounded queues and asyncio; there is no second event-loop
framework and no nested Qt wait. Diagnostic waiter counters remain labeled with
their own lifetime instead of being presented as session-scoped acceptance proof.

Behavioral red/green covers malformed schemas, initial-state refusal with zero
actions, literal parameters, exact raw values, unknown/lost/reconnected evidence,
immediate and queued completion, stale/wrong correlation, target destruction,
deadline/cancellation cleanup and bounded exclusive subscription permits.
CLI/MCP default to strict acceptance; exploratory reports disclose normalized,
wildcard, unavailable and unsupported evidence instead of relabeling it exact.
Native injection tests cover synchronous and deferred completion and a wrong
initial state. Moving subscription after mutation deliberately fails both native
completion tests; restoring the order passes them.

The local qt-agent-skills `qt-cpp-review` was run at skill revision
`71d6c10da78b9a764468ae11c86ab3bc4ca4921f`. Its six focused reviews found an empty
profile downgrade, rejected socket admission retention, and unlimited exclusive
subscriptions. All three have reproduced red/green fixes; the first two live in
PR 2. Raw lint, review and test artifacts remain in ignored `logs/`.

Final local code validation: 1,183 Python tests passed with six existing optional/
SDK-dependent skips; all 31 native suites passed on macOS/Qt 6.11.2. Compiler and
pytest warnings remain errors. Hosted/platform and separate-host LAN evidence
are tracked in the [integration plan](2026-09-26-hardening-and-observability.md).
