# Sanitizers and leak checking

What has been run against the probe, what it found, and what is worth doing next.

Nothing here runs in CI yet — that is a deliberate pause, not an oversight. See
[Wiring this into CI](#wiring-this-into-ci).

## Presets

```bash
cmake --preset asan-ubsan && cmake --build --preset asan-ubsan && ctest --preset asan-ubsan
cmake --preset tsan       && cmake --build --preset tsan       && ctest --preset tsan
```

Linux and macOS only — MSVC has ASan but no UBSan. `QTPILOT_SANITIZE` takes any
`-fsanitize=` list directly if you want a combination the presets do not cover. Two
presets rather than one because ASan and TSan cannot be linked together.

The test presets carry the runtime options, so a first run is not buried in noise:
container-overflow off (Qt is not instrumented), leak detection off (see
[leaks](#leak-checking-on-macos)), UBSan set to halt on error. Compilation uses
`-fno-sanitize-recover=undefined` for the same reason — a sanitizer that only prints
is one a green test run hides.

## ASan + UBSan: clean

20/20 with no findings.

That was checked for the failure mode a clean sanitizer run usually has, which is
that the flags never made it into the build. The ASan runtime is linked into the test
binaries (`otool -L` shows `libclang_rt.asan_osx_dynamic.dylib`), the probe carries
52 undefined `__asan`/`__ubsan` symbols, and a deliberate heap-use-after-free trips
the toolchain. The result is real.

## TSan: one real race, two tool artifacts

### The real one, left unsuppressed

`uninstallObjectHooks()` writes `g_previousAddCallback`, `g_previousRemoveCallback`
and `g_hooksInstalled` — plain non-atomic globals in `object_registry.cpp` — while the
hooks that read them are invoked by Qt on whatever thread happens to construct or
destroy a `QObject`. `uninstallObjectHooks()` is reached from `Probe::shutdown()`.

It needs a `QObject` created or destroyed on another thread during install or
shutdown, so it is narrow. It is still a data race on a function pointer that is then
called. Worth noting that `g_singletonCreating`, declared three lines below those
globals, is already `std::atomic<bool>` with a comment about precisely this hazard;
the hook globals were not given the same treatment.

### The artifacts, suppressed

Two reports pointed at `m_objects` accesses that are plainly inside a `QMutexLocker`
block. That was suspicious enough to test rather than argue about. A counter
incremented 80000 times from four threads under a lock, with Qt uninstrumented:

| Primitive | TSan sees the happens-before edge? |
|---|---|
| `QMutex` | yes |
| `QThread::wait()` | yes |
| **`QRecursiveMutex`** | **no** — reports a race; counter still exactly 80000 |
| `std::recursive_mutex` | yes |

`ObjectRegistry::m_mutex` is the only `QRecursiveMutex` in the codebase, so every
registry container access looks unsynchronized to TSan. Suppressed in
[`cmake/tsan-suppressions.txt`](../cmake/tsan-suppressions.txt).

Two hypotheses were wrong before that table existed — "uninstrumented Qt breaks TSan
generally" (no: `QMutex` is fine) and "`QThread::wait()` is opaque" (no: also fine).
The experiment is what settled it, and it is cheap to repeat if the picture changes.

The suppression is blunt (`race:qtPilot::ObjectRegistry::`) and will hide a real
registry race too. It exists so `ctest --preset tsan` is usable at all.

## Leak checking on macOS

**A CMake preset does not make sense here, and this is the reasoning rather than a
shrug.** Apple's `leaks` needs no compile or link flags: it inspects a live process.
A `*-leaks` configure preset would therefore be a byte-for-byte copy of `macos-debug`
under a different name, producing a second build tree for no behavioural difference.
It is a runtime tool, not a build mode. ASan and TSan earn presets because they change
what the compiler emits; `leaks` does not.

It also must **not** run against an ASan build — ASan replaces the allocator and
`leaks` cannot see through it. Use a plain build.

Two things that are easy to get wrong:

**1. Without a debug entitlement the result is a lie.** A default run prints

```
Process 63119 is not debuggable. Due to security restrictions, leaks can only
show or save contents of readonly memory of restricted processes.
Process 63119: 0 leaks for 0 total leaked bytes.
```

That "0 leaks" means `leaks` could not inspect the process, not that the process is
clean. Ad-hoc signing with `com.apple.security.get-task-allow` removes the warning and
produces a real answer:

```bash
cat > /tmp/get-task-allow.plist <<'PL'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>com.apple.security.get-task-allow</key><true/>
</dict></plist>
PL
codesign -s - -f --entitlements /tmp/get-task-allow.plist build/plain/bin/test_object_id

QT=/path/to/Qt/6.x/macos
DYLD_FRAMEWORK_PATH=$QT/lib QT_PLUGIN_PATH=$QT/plugins QT_QPA_PLATFORM=minimal \
  QTPILOT_ENABLED=0 MallocStackLogging=1 \
  leaks --atExit -- build/plain/bin/test_object_id
```

**2. Verify the tool before trusting a zero.** A binary that deliberately drops a
`QObject` and a 4 KB buffer reports `1 leak for 5120 total leaked bytes`, so the
detector is live. With that established, `test_object_id` reporting **0 leaks** is a
genuine result.

Also worth knowing: `leaks` reports at-exit *reachability*, not lifetime correctness.
An object parked on `deleteLater` or owned by a singleton counts as leaked and is
usually fine. Larger Qt applications carry a baseline in the hundreds or thousands of
blocks from Qt's own singletons; the probe's small `QTest` binaries currently do not,
which is why an absolute count is meaningful here and would not be elsewhere. If that
baseline ever appears, the useful question stops being "how many" and becomes "does
any leak stack name code I changed".

## Wiring this into CI

Not done yet, on purpose. What each option costs:

| Option | Cost | Blocker |
|---|---|---|
| `asan-ubsan` on one Linux leg | ASan roughly triples test time | None. This is the obvious first step. |
| `tsan` | Same order of slowdown | Currently red by design — the hook-globals race must be fixed first, or the job starts life failing and gets ignored. |
| `leaks` on macOS | Cheap | Needs the entitlement step above, and a runner where ad-hoc signing works. |

The sequencing that avoids a permanently-red job: fix the hook-globals race, then add
`asan-ubsan`, then `tsan`.

## Worth doing next

1. **Fix the hook-globals race.** Make the three globals atomic, or guard
   install/uninstall with the registry mutex. This is the only finding here that is a
   product bug.
2. **Swap `ObjectRegistry::m_mutex` to `std::recursive_mutex`** and delete the
   suppressions file. One declaration plus 15 lock sites in `object_registry.cpp` —
   not purely mechanical, because some use `QMutexLocker::unlock()`/`relock()` and
   would need `std::unique_lock`. This buys real TSan coverage of the registry, which
   is currently the least-covered concurrent code in the probe.
3. **A leak-check script** rather than a preset: run a filtered `leaks` against a
   plain build, handle the entitlement, and report leak stacks naming probe symbols.
   That is the shape this tool wants.
4. **Enable LSan on Linux.** Unlike macOS, Linux ASan has a leak detector. It is off
   in the preset because it also reports uninstrumented-Qt allocations; a suppressions
   file scoped to Qt frames would make it usable and would cover leaks on the platform
   where most CI runs.
5. **An instrumented Qt** would remove the `QRecursiveMutex` blind spot entirely and
   let TSan see Qt's own internals. Expensive to build and cache, so only worth it if
   concurrency bugs keep landing.

## See also

- [`cmake/tsan-suppressions.txt`](../cmake/tsan-suppressions.txt) — what is
  suppressed, why, and what is deliberately not
- [`docs/BUILDING.md`](BUILDING.md) — build options and presets
- [`benchmarks/README.md`](../benchmarks/README.md) — the complexity benchmarks, which
  are the other "measure it rather than argue" tool in this repo
