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
| `BM_GenerateOneId_WithNSiblings` | **O(N)** | One `generateObjectId()` with N siblings present — the per-call cost |
| `BM_GenerateAllIds_ForGroup` | **O(N²)** | IDs for every sibling in the group — what a full tree walk costs |
| `BM_SerializeTree_NDelegates` | **O(N²)** | `serializeObjectTree()`, i.e. `qt.objects.tree` |
| `BM_ResolveId_LastOfNSiblings` | **O(N²)** | `findByObjectId()` resolving a single ID |

N is the number of eagerly instantiated sibling delegates under one container.

## Why these are quadratic, and what would fix it

Both quadratics come from the same place. `generateIdSegment()` has to know whether
an object's segment is unique among its siblings, and `getSiblingIndex()` answers
that by scanning the sibling list — O(N) per object. Do that for every object in the
group and the walk is O(N²).

Resolution inherits it for a second reason: `matchesSegment()` compares against a
freshly generated segment, so resolving one path scans N candidates and pays the
O(N) sibling scan at each — hence `BM_ResolveId_LastOfNSiblings` being quadratic
rather than linear.

The known fix for both is to stop asking the question per object: compute every
sibling's segment **once per parent**, bucket them, and assign suffixes in one
sweep. That makes the group O(N) and the tree walk linear in node count. It has not
been done because correctness came first — an earlier attempt to shortcut the
sibling scan produced duplicate IDs, which is the one thing an ID may not do.

If you take that on, these benchmarks are how you show it worked:
`BM_GenerateAllIds_ForGroup` and `BM_SerializeTree_NDelegates` should report `N`
rather than `N^2`.

## Practical impact

A recycling `ListView` only ever instantiates its visible delegates, so N is small
(tens) and the quadratic is invisible. The shape that bites is a `Repeater` over
thousands of eager rows, where the cost lands on the host application's main thread
during `ObjectRegistry::scanExistingObjects()` at probe startup — and again on every
`qt.objects.tree`.

## Adding a case

Give it a `SetComplexityN(n)` and a `->Complexity()`, build the fixture **outside**
the timed loop, and pick a range wide enough for the fit to mean something (at least
~5 points spanning 2 orders of magnitude). If a new benchmark reports a worse
exponent than the table above, that is the finding — record it rather than tuning
the constant.
