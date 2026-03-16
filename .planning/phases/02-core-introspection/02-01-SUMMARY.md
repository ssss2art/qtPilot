# Phase 02 Plan 01: Object Registry Summary

**One-liner:** Object Registry with qtHookData hooks for tracking all QObjects with thread-safe singleton and re-entry protection

## Metadata

```yaml
phase: 02-core-introspection
plan: 01
subsystem: core/introspection
tags: [qtHookData, object-tracking, singleton, thread-safety]

dependency-graph:
  requires: [01-foundation]
  provides: [object-registry, hook-installation, object-lookup]
  affects: [02-02, 02-03, 02-04]

tech-stack:
  added: [Qt6::CorePrivate]
  patterns: [Q_GLOBAL_STATIC singleton, qtHookData hooks, daisy-chaining]

key-files:
  created:
    - src/probe/core/object_registry.h
    - src/probe/core/object_registry.cpp
    - tests/test_object_registry.cpp
  modified:
    - CMakeLists.txt
    - src/probe/CMakeLists.txt
    - src/probe/core/probe.cpp
    - src/probe/core/probe_init_windows.cpp
    - tests/CMakeLists.txt

decisions:
  - id: atomic-reentry-guard
    choice: "Use std::atomic<bool> for re-entry guard"
    rationale: "thread_local causes TLS issues in injected DLLs (same as std::call_once)"
  - id: destructor-hook-cleanup
    choice: "Uninstall hooks in ObjectRegistry destructor"
    rationale: "Prevents crash from hooks calling into partially-destroyed registry"
  - id: qtpilot-enabled-check
    choice: "Add QTPILOT_ENABLED=0 check to Windows probe_init"
    rationale: "Allows isolated testing of ObjectRegistry without full probe"

metrics:
  tasks: 3
  duration: 32 min
  completed: 2026-01-30
```

## Summary

Created the ObjectRegistry singleton that tracks all QObjects in target applications using Qt's private qtHookData hooks. This is the foundation for all introspection features - every object lookup, property access, and signal subscription will use this registry.

### Key Implementation Details

1. **Hook Installation via qtHookData**
   - Installs AddQObject and RemoveQObject callbacks
   - Preserves existing hooks via daisy-chaining (GammaRay compatibility)
   - Verifies qtHookData version >= 1 before installing

2. **Thread-Safe Singleton**
   - Uses Q_GLOBAL_STATIC for lazy initialization
   - QRecursiveMutex protects shared data
   - Atomic flag prevents re-entry during singleton creation

3. **Object Lifecycle Integration**
   - Hooks installed in Probe::initialize()
   - Existing objects scanned via scanExistingObjects()
   - Hooks uninstalled in both Probe::shutdown() and ObjectRegistry destructor

4. **Lookup Methods**
   - findByObjectName(name, root) - find by Qt objectName
   - findAllByClassName(className, root) - find all of a class
   - allObjects() / objectCount() / contains()

## Commits

| Hash | Type | Description |
|------|------|-------------|
| 3c2deaa | feat | Add ObjectRegistry with qtHookData hooks |
| e32f919 | feat | Integrate ObjectRegistry with Probe lifecycle |
| 14cdf09 | test | Add unit tests for ObjectRegistry |

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Re-entry during singleton initialization**
- **Found during:** Task 3 (testing)
- **Issue:** When hooks were installed and qDebug() was called, it created temporary QObjects which triggered the hook, which called ObjectRegistry::instance(), which created the singleton, which is a QObject, which triggered the hook again - infinite recursion
- **Fix:** Added std::atomic<bool> g_singletonCreating flag to guard against re-entry in hook callbacks during singleton initialization
- **Files modified:** src/probe/core/object_registry.cpp

**2. [Rule 1 - Bug] Crash on process exit**
- **Found during:** Task 3 (testing)
- **Issue:** When ObjectRegistry Q_GLOBAL_STATIC was destroyed at DLL unload, objects being destroyed would trigger hooks that called into the partially-destroyed registry, causing crash in QSet::remove
- **Fix:** Uninstall hooks in ObjectRegistry destructor before any cleanup
- **Files modified:** src/probe/core/object_registry.cpp

**3. [Rule 2 - Missing Critical] QTPILOT_ENABLED check on Windows**
- **Found during:** Task 3 (testing)
- **Issue:** Tests couldn't disable probe auto-initialization on Windows, causing full probe startup during unit tests
- **Fix:** Added QTPILOT_ENABLED=0 check to qtpilotAutoInit() in probe_init_windows.cpp
- **Files modified:** src/probe/core/probe_init_windows.cpp

## Test Results

All 6 test cases pass:
- testSingleton: Singleton returns same instance
- testObjectTracking: Objects tracked when created
- testFindByObjectName: Lookup by name works globally and in subtrees
- testFindAllByClassName: Class-based lookup returns all matches
- testObjectRemoval: Objects removed from registry on destruction
- testThreadSafety: 200 objects created across 4 threads without crash

## Next Phase Readiness

ObjectRegistry is ready for use by Plan 02-02 (Object ID system). The registry provides:
- Thread-safe object storage
- Hook-based lifecycle tracking
- Basic lookup methods

Plan 02-02 will add:
- Hierarchical ID generation (objectName > text > className#N pattern)
- ID-to-object mapping (currently just object-to-object set)
- findById() lookup method
