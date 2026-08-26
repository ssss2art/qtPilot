# Sanitizers and leak checking

What has been run against the probe, what it found, and how it is tested.

Both `asan-ubsan` and `tsan` are wired into GitHub Actions CI on Linux (see
[Continuous integration](#continuous-integration)).

## Presets

```bash
cmake --preset asan-ubsan && cmake --build --preset asan-ubsan && ctest --preset asan-ubsan
cmake --preset tsan       && cmake --build --preset tsan       && ctest --preset tsan
```

Linux and macOS only — MSVC has ASan but lacks UBSan/TSan support. `QTPILOT_SANITIZE` takes any
`-fsanitize=` list directly if you want a combination the presets do not cover. Two
presets rather than one because ASan and TSan cannot be linked together.

The test presets carry the runtime options, so a first run is not buried in noise:
container-overflow off (Qt is not instrumented), leak detection off (see
[leaks](#leak-checking-on-macos)), UBSan set to halt on error (`halt_on_error=1`), and TSan
configured with `halt_on_error=1` to fail immediately upon detecting a race. Compilation uses
`-fno-sanitize-recover=undefined` for the same reason — a sanitizer that only prints
is one a green test run hides.

## ASan + UBSan: clean

All unit tests pass with no findings.

Verified to ensure sanitizer instrumentation is active: the ASan runtime is linked
into test binaries (`otool -L` on macOS shows the clang ASan dylib), undefined
sanitizer symbols are present in the probe, and an intentional heap use-after-free
correctly trips the toolchain.

## TSan: findings and resolved fixes

Initial TSan exploration uncovered one real product race and one Qt synchronization artifact:

### 1. Hook globals data race (Resolved)

`uninstallObjectHooks()` previously wrote `g_previousAddCallback`, `g_previousRemoveCallback`,
and `g_hooksInstalled` as plain non-atomic globals, while the hook callbacks (`qtpilotAddObjectHook`)
read and invoked them from whatever thread happened to construct or destroy a `QObject`.

**Fix:** `g_previousAddCallback` and `g_previousRemoveCallback` are now `std::atomic<AddQObjectCallback>`
/ `std::atomic<RemoveQObjectCallback>`, and `g_hooksInstalled` is `std::atomic<bool>`. Hook install,
invocation, and uninstall use acquire-release semantics, preventing data races during probe teardown.

### 2. `QRecursiveMutex` synchronization artifact (Resolved)

TSan does not track happens-before edges across `QRecursiveMutex`, causing false race reports
for container accesses in `ObjectRegistry` that were properly guarded.

| Primitive | TSan sees the happens-before edge? |
|---|---|
| `QMutex` | yes |
| `QThread::wait()` | yes |
| **`QRecursiveMutex`** | **no** — reports false race |
| `std::recursive_mutex` | **yes** |

**Fix:** `ObjectRegistry::m_mutex` was migrated from `QRecursiveMutex` to `std::recursive_mutex`
(using `std::unique_lock<std::recursive_mutex>`). This eliminated the false reports completely without
needing a suppression file, giving real TSan coverage across all registry operations.

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

**2. Verify the tool before trusting a zero.** Confirm the detector is live by
testing an intentional leak in a dummy binary. With detection verified, `test_object_id`
reporting no leaks is a genuine result.

Also worth knowing: `leaks` reports at-exit *reachability*, not lifetime correctness.
An object parked on `deleteLater` or owned by a singleton counts as leaked and is
usually fine. Larger Qt applications carry a baseline in the hundreds or thousands of
blocks from Qt's own singletons; the probe's small `QTest` binaries currently do not,
which is why an absolute count is meaningful here and would not be elsewhere. If that
baseline ever appears, the useful question stops being "how many" and becomes "does
any leak stack name code I changed".

## Continuous integration

Both `asan-ubsan` and `tsan` run on Linux (Ubuntu 24.04, Qt 6.10.0) in the `sanitizers` matrix
job in `.github/workflows/ci.yml`. Tests execute with `halt_on_error=1` so any memory safety or
data race regression fails the build immediately.

## Worth doing next

1. **A leak-check script** for macOS: run a filtered `leaks` against a plain build, handle the
   entitlement, and report leak stacks naming probe symbols.
2. **Enable LSan on Linux.** Unlike macOS, Linux ASan has a leak detector. A suppressions file
   scoped to Qt runtime frames would make LSan usable on Linux CI without false positives from
   uninstrumented Qt allocations.
3. **An instrumented Qt build** would let TSan inspect Qt internals directly (e.g. event dispatcher
   and object hierarchy locking).

## See also

- [`docs/BUILDING.md`](BUILDING.md) — build options and presets
- [`benchmarks/README.md`](../benchmarks/README.md) — the complexity benchmarks
