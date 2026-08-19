# Complexity benchmarks

These exist to answer one question mechanically: **did this change make a hot path
more complex than it was?**

They are not a performance gate and not a CI job. Absolute timings depend on the
machine and are not worth defending. What is worth defending is the *exponent*, and
google/benchmark measures that directly: every case declares `SetComplexityN()` and
`Complexity()`, so the harness fits the measured curve against N and prints the
inferred Big-O plus an RMS goodness-of-fit.

## Running

```bash
cmake -B build -DQTPILOT_BUILD_BENCHMARKS=ON
cmake --build build --target qtPilot_bench_object_id
./build/bin/qtPilot_bench_object_id
```

Off by default: the target fetches google/benchmark at configure time, and nothing
in the normal build or test cycle needs it.

Read the `_BigO` and `_RMS` rows, not the per-N times. **An RMS above ~10% means
the fit is poor and the label is not trustworthy** — rerun on an otherwise idle
machine before drawing a conclusion. On a quiet machine these fit to 0–1%.

## Baseline

Measured on Apple M-series, Qt 6.11.1, Release, `--benchmark_min_time=0.05s`.
Constants are meaningless across machines; the exponents are the point.

| Benchmark | Big-O | What it covers |
|---|---|---|
| `BM_GenerateOneId_WithNSiblings` | O(N) | One `generateObjectId()` with N siblings present |
| `BM_GenerateAllIds_ForGroup` | **O(N²)** | Ids for the whole group with **no** scope — the raw per-call API |
| `BM_GenerateAllIds_ForGroup_Scoped` | **O(N)** | The same loop inside an `IdGenerationScope` |
| `BM_SerializeTree_NDelegates` | O(N) | `serializeObjectTree()`, i.e. `qt.objects.tree` |
| `BM_ResolveId_LastOfNSiblings` | O(N) | `findByObjectId()` resolving a single id |

N is the number of eagerly instantiated sibling delegates under one container.

## Where the complexity comes from, and how it was removed

Generating a segment requires knowing whether it is unique among the object's
effective siblings. Answered per object, that is a scan of the sibling list — O(N)
each, so O(N²) to walk a group of N. Resolution inherited it twice over, because
`matchesSegment()` compares against a freshly generated segment, so resolving one
path scanned N candidates and paid the O(N) sibling scan inside each.

`IdGenerationScope` (see `introspection/object_id.h`) answers the question **once per
parent** instead: one pass over a parent's effective children buckets them by segment
and assigns every child its suffix, so later lookups are hash hits. The results are
identical to the unscoped path by construction — same enumeration order, same
equality, and an object the cached pass did not see falls back to the direct scan.

`serializeObjectTree()`, `findByObjectId()`, `ObjectRegistry::scanExistingObjects()`
and `refreshDescendantIds()` each open one for the duration of their walk. That took
tree serialization and id resolution from O(N²) to O(N).

The two `ForGroup` rows are kept as a pair on purpose. The unscoped one is still
quadratic and *should* be: it measures the raw per-call API. It is also the row that
would regress if a traversal ever stopped taking a scope. If you add a batch caller,
give it a scope and the linear row is what you should see.

## Practical impact

A recycling `ListView` only ever instantiates its visible delegates, so N is small
(tens) and none of this is visible. The shape that bites is a `Repeater` over
thousands of eager rows, where the cost lands on the host application's main thread
during `ObjectRegistry::scanExistingObjects()` at probe startup — and, before the
scope, again on every `qt.objects.tree` and every id-addressed operation.

## Adding a case

Give it a `SetComplexityN(n)` and a `->Complexity()`, build the fixture **outside**
the timed loop, and pick a range wide enough for the fit to mean something (at least
~5 points spanning 2 orders of magnitude). If a new benchmark reports a worse
exponent than the table above, that is the finding — record it rather than tuning
the constant.
