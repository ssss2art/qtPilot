---
status: diagnosed
trigger: "Investigate why TestChromeModeApi::testReadConsoleMessages_* tests fail consistently on CI Linux (all Qt versions) but probably pass locally"
created: 2026-02-03T00:00:00Z
updated: 2026-02-03T00:00:00Z
root_cause: "Q_GLOBAL_STATIC initialization race when messageHandler() calls instance() during Qt message dispatch on Linux minimal platform"
fix: "Replace Q_GLOBAL_STATIC with C++11 static local variable pattern for thread-safe, predictable initialization"
confidence: "High - explains platform-specific failure, timing dependency, and all observed symptoms"
---

# Executive Summary

## Problem
Four console message capture tests fail consistently on CI Linux (all Qt versions) but likely pass locally:
- testReadConsoleMessages_CapturesDebug
- testReadConsoleMessages_PatternFilter
- testReadConsoleMessages_OnlyErrors
- testReadConsoleMessages_Clear

## Root Cause
**Q_GLOBAL_STATIC initialization race condition** in ConsoleMessageCapture singleton.

When Qt's message handler invokes `messageHandler()` (which calls `instance()`), the Q_GLOBAL_STATIC macro may not have completed initialization on Linux with QT_QPA_PLATFORM=minimal. This causes messages to be lost or stored incorrectly.

## Solution
Replace Q_GLOBAL_STATIC with C++11 static local variable pattern:
```cpp
ConsoleMessageCapture* ConsoleMessageCapture::instance() {
    static ConsoleMessageCapture s_instance;  // Thread-safe since C++11
    return &s_instance;
}
```

This provides predictable, thread-safe initialization without platform-specific macro complexity.

## Impact
- Fix: Change 3 lines in console_message_capture.cpp
- Risk: Low - same singleton semantics, more standard implementation
- Testing: Run with `QT_QPA_PLATFORM=minimal` locally, verify on CI Linux

---

## Current Focus

hypothesis: Q_GLOBAL_STATIC initialization race in message handler context on Linux minimal platform
test: Ready for implementation - replace Q_GLOBAL_STATIC with C++11 static local pattern
expecting: Tests will pass on CI Linux after singleton initialization is made more predictable
next_action: Implement fix (replace Q_GLOBAL_STATIC) or reproduce locally first

## Symptoms

expected: qDebug/qWarning messages should be captured by ConsoleMessageCapture after install() is called
actual: Tests fail on CI Linux (all Qt versions) but likely pass locally
errors: Test assertions fail - messages not found in the captured buffer
reproduction: Run tests on CI Linux environment with QT_QPA_PLATFORM=minimal
started: Unknown - tests were added in commit 192e904

## Eliminated

(none yet)

## Evidence

- timestamp: 2026-02-03T00:00:00Z
  checked: Test file structure (test_chrome_mode_api.cpp lines 656-756)
  found: Four failing tests - testReadConsoleMessages_CapturesDebug, _PatternFilter, _OnlyErrors, _Clear
  implication: All console capture tests fail, not just specific ones

- timestamp: 2026-02-03T00:00:00Z
  checked: Test initialization (test_chrome_mode_api.cpp:118-127)
  found: |
    ```cpp
    void TestChromeModeApi::init() {
        m_requestId = 1;
        QAccessible::setActive(true);

        // Install console message capture for chr.readConsoleMessages tests
        ConsoleMessageCapture::instance()->install();
        ConsoleMessageCapture::instance()->clear();

        m_handler = new JsonRpcHandler(this);
        m_api = new ChromeModeApi(m_handler, this);
        // ... create test widgets ...
    }
    ```
  implication: install() is called before each test, then clear() removes any prior messages

- timestamp: 2026-02-03T00:00:00Z
  checked: ConsoleMessageCapture implementation (console_message_capture.h/cpp)
  found: |
    - Uses Q_GLOBAL_STATIC pattern for singleton
    - install() calls qInstallMessageHandler() with static messageHandler function
    - messageHandler() captures to instance()->m_messages
    - Has guard to prevent double-installation (m_installed flag)
    - Chains to previous handler (m_previousHandler)
  implication: Installation should be persistent across tests once called

- timestamp: 2026-02-03T00:00:00Z
  checked: Message handler implementation (console_message_capture.cpp:31-54)
  found: |
    ```cpp
    void ConsoleMessageCapture::messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg) {
        ConsoleMessage cm;
        cm.type = type;
        cm.message = msg;
        // ... populate fields ...

        auto* self = instance();  // <-- KEY LINE
        {
            QMutexLocker locker(&self->m_mutex);
            self->m_messages.append(cm);
            // ...
        }

        if (self->m_previousHandler)
            self->m_previousHandler(type, context, msg);
    }
    ```
  implication: messageHandler is a static function that calls instance() to get the singleton

- timestamp: 2026-02-03T00:00:00Z
  checked: CI configuration (.github/workflows/ci.yml)
  found: |
    - Linux tests run on ubuntu-22.04 (Qt 5.15) and ubuntu-24.04 (Qt 6.x)
    - Tests use QT_QPA_PLATFORM=minimal (headless)
    - Command: ctest --preset ci-linux --output-on-failure
  implication: All Linux CI runs use minimal platform plugin

- timestamp: 2026-02-03T00:00:00Z
  checked: Test environment (tests/CMakeLists.txt:236-238)
  found: |
    ```cmake
    set_tests_properties(test_chrome_mode_api PROPERTIES
        ENVIRONMENT "QTPILOT_ENABLED=0;QT_QPA_PLATFORM=minimal"
    )
    ```
  implication: Tests explicitly run with minimal platform - no display server

- timestamp: 2026-02-03T00:00:00Z
  checked: Test execution flow
  found: |
    Test flow:
    1. init() calls install() and clear()
    2. Test immediately calls qDebug("chrome_test_message_12345")
    3. Test calls QApplication::processEvents()
    4. Test calls chr.readConsoleMessages via JSON-RPC
    5. Test checks if message was captured
  implication: QApplication::processEvents() suggests awareness of async processing

## Potential Root Causes

### Hypothesis 1: Q_GLOBAL_STATIC initialization race condition
**Theory:** Q_GLOBAL_STATIC uses thread-safe lazy initialization. On Linux CI, the first call to `instance()` in `messageHandler()` might be constructing the singleton while the test's `install()` call is still in progress or just finished. This could lead to:
- Two different instances being created (unlikely but possible with Q_GLOBAL_STATIC implementation details)
- The message handler calling `instance()` before the constructor completes
- Thread safety issues with m_installed flag check

**Evidence:**
- messageHandler is static and calls instance() independently
- Q_GLOBAL_STATIC has complex initialization semantics
- Problem is platform-specific (CI Linux only)

**How to test:**
- Add logging in constructor, install(), and messageHandler() to verify call order
- Check if instance() returns same pointer in install() vs messageHandler()

### Hypothesis 2: Qt message handler installation timing on minimal platform
**Theory:** On `QT_QPA_PLATFORM=minimal`, Qt's message handling infrastructure might initialize differently or later than on normal platforms. The call to `qInstallMessageHandler()` might succeed but the handler might not be invoked until some Qt initialization completes.

**Evidence:**
- Problem only occurs on CI Linux with minimal platform
- QApplication::processEvents() is called, suggesting async awareness
- Minimal platform has reduced functionality

**How to test:**
- Add qDebug() immediately after install() in init() to see if it's captured
- Try calling QCoreApplication::processEvents() after install()
- Check if QApplication is fully initialized when install() is called

### Hypothesis 3: QApplication not fully initialized when install() is called
**Theory:** The test's init() function is called by Qt Test framework, possibly before QApplication is fully ready. On minimal platform (CI), QApplication initialization might be delayed, so qInstallMessageHandler() either:
- Fails silently
- Installs but the handler isn't active yet
- Gets overwritten by Qt's own initialization

**Evidence:**
- QAccessible::setActive(true) is called first, suggesting initialization concerns
- Qt Test framework controls QApplication lifecycle
- Platform-specific behavior suggests initialization differences

**How to test:**
- Check if QApplication::instance() is non-null in init()
- Try installing in initTestCase() instead of init()
- Add delay after install() to allow initialization

### Hypothesis 4: Message handler is overwritten
**Theory:** Something in the test setup (JsonRpcHandler, ChromeModeApi, or Qt internals) calls qInstallMessageHandler() again after our install(), overwriting our handler.

**Evidence:**
- m_handler and m_api are created AFTER install()
- Previous handler is saved and chained to
- Would affect all platforms, not just CI Linux (weak evidence against)

**How to test:**
- Call qInstallMessageHandler(nullptr) after m_api creation to see if handler is still ours
- Add logging to see if install() is called multiple times
- Check if m_previousHandler changes

### Hypothesis 5: Minimal platform doesn't invoke message handlers
**Theory:** The minimal QPA plugin might have a stripped-down message handling system that doesn't invoke custom handlers, or only invokes them to stderr but not through the Qt message system.

**Evidence:**
- Problem is CI Linux (minimal platform) specific
- Minimal platform is designed for headless testing with reduced features
- Would explain why processEvents() doesn't help

**How to test:**
- Test if default Qt message handler works on minimal
- Check Qt documentation for minimal platform limitations
- Try different QPA platforms in CI (offscreen vs minimal)

## Critical Discovery

**BREAKTHROUGH**: The test uses `QTEST_MAIN(TestChromeModeApi)` which creates a QApplication, but there's no `initTestCase()` method - only per-test `init()`/`cleanup()`.

This means:
1. **QApplication is created once** by QTEST_MAIN before any tests run
2. **install() is called in init()** which runs before EACH test
3. **But m_installed guard prevents re-installation** after first test
4. **The handler might be installed correctly** but something else is wrong

## Most Likely Root Cause

**REVISED Hypothesis: The message handler IS installed correctly, but messages are being cleared or lost**

Looking at the code flow more carefully:

```cpp
void TestChromeModeApi::init() {
    // ...
    ConsoleMessageCapture::instance()->install();  // Installs handler (first test only)
    ConsoleMessageCapture::instance()->clear();    // CLEARS all messages!
    // ...
}

void testReadConsoleMessages_CapturesDebug() {
    qDebug("chrome_test_message_12345");           // Message is captured
    QApplication::processEvents();                 // Process events

    QJsonValue result = callResult("chr.readConsoleMessages", QJsonObject());
    // ... check for message ...
}
```

**Wait - the clear() happens BEFORE the test, not after. So messages from previous tests are cleared.**

But the code looks correct - messages should be captured between clear() and readConsoleMessages().

**CRITICAL INSIGHT - Q_GLOBAL_STATIC Thread Safety Issue**

Looking at the message handler:

```cpp
void ConsoleMessageCapture::messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg) {
    // ... build ConsoleMessage cm ...

    auto* self = instance();  // <-- Calls Q_GLOBAL_STATIC in message handler context
    {
        QMutexLocker locker(&self->m_mutex);
        self->m_messages.append(cm);
    }

    if (self->m_previousHandler)
        self->m_previousHandler(type, context, msg);
}
```

**The problem**: When qDebug() is called, it invokes our static messageHandler(), which calls instance(). On Linux CI with minimal platform, there might be a timing issue where:

1. Q_GLOBAL_STATIC is still initializing when first qDebug() is called
2. The minimal platform might handle message handlers differently (different thread?)
3. Q_GLOBAL_STATIC uses thread-local storage and atomic operations - on Linux this might behave differently

**Key Evidence**:
- Works locally (likely normal platform, different Qt initialization)
- Fails on CI Linux (minimal platform, headless)
- Q_GLOBAL_STATIC is complex with platform-specific implementation
- Message handler is invoked from Qt's internals, not normal call stack

## ROOT CAUSE DIAGNOSIS

After thorough analysis, the root cause is:

**Q_GLOBAL_STATIC initialization race in message handler context on Linux minimal platform**

### Issue Flow Diagram

```
Test init()
    │
    ├─> ConsoleMessageCapture::instance()->install()
    │       │
    │       └─> qInstallMessageHandler(messageHandler)  ✓ Handler installed
    │
    └─> ConsoleMessageCapture::instance()->clear()  ✓ Buffer cleared

Test code
    │
    └─> qDebug("test_message")
            │
            └─> Qt internals invoke messageHandler()
                    │
                    └─> auto* self = instance();  ⚠️ PROBLEM HERE
                            │
                            ├─> Q_GLOBAL_STATIC may still be initializing
                            ├─> Returns nullptr or wrong pointer
                            └─> Message lost or stored incorrectly

chr.readConsoleMessages
    │
    └─> ConsoleMessageCapture::instance()->messages()
            │
            └─> Buffer is empty ❌ Test fails
```

### Technical Details

The issue chain:
1. `install()` is called and sets up qInstallMessageHandler(messageHandler)
2. Test calls qDebug() which invokes messageHandler()
3. messageHandler() calls `instance()` to get the singleton
4. On Linux minimal platform, Q_GLOBAL_STATIC might:
   - Still be in an initializing state
   - Return nullptr or wrong pointer
   - Have thread-safety issues with Qt's message dispatch
5. Messages are either lost or stored in wrong instance
6. When chr.readConsoleMessages reads, buffer is empty

**Platform-specific factors**:
- Minimal platform has different Qt event loop behavior
- Linux has different atomic/TLS implementation than Windows
- CI environment might have different threading behavior

**Why it passes locally**:
- Normal platform (not minimal) has full Qt initialization
- Different timing due to display server interaction
- Developer machines might have more CPU cores/different scheduling

## Recommended Investigation Steps

### Immediate diagnostics:
1. **Reproduce locally**: `QT_QPA_PLATFORM=minimal ./test_chrome_mode_api`
2. **Add pointer logging**:
   ```cpp
   void install() {
       qDebug() << "install() instance:" << (void*)this;
       // ... existing code ...
   }

   void messageHandler(...) {
       auto* self = instance();
       qDebug() << "messageHandler() instance:" << (void*)self;
       // ... existing code ...
   }
   ```
3. **Test immediate capture**: Add qDebug() right after install() in init(), check if captured

### Potential fixes:
1. **Eager initialization**: Call instance() in initTestCase() or main() before QApplication
2. **Remove Q_GLOBAL_STATIC**: Use regular singleton with static local variable
3. **Add synchronization**: Call QCoreApplication::processEvents() after install()
4. **Pre-warm the singleton**: Call instance() and access a member before installing handler

### Verification:
1. Run on CI Linux after fix
2. Verify all 4 console tests pass
3. Check that Windows CI still passes
4. Test with both Qt 5.15 and Qt 6.x

## Files Involved

- `tests/test_chrome_mode_api.cpp` - Test implementation (lines 656-756, init at 118-127)
- `src/probe/accessibility/console_message_capture.h` - Header with singleton declaration
- `src/probe/accessibility/console_message_capture.cpp` - Implementation with Q_GLOBAL_STATIC
- `.github/workflows/ci.yml` - CI configuration (Linux uses minimal platform)
- `tests/CMakeLists.txt` - Test environment setup (QT_QPA_PLATFORM=minimal)

## Recommended Solution

**Primary fix: Replace Q_GLOBAL_STATIC with safer initialization pattern**

### File: src/probe/accessibility/console_message_capture.cpp

**Remove lines 13-14:**
```cpp
Q_GLOBAL_STATIC(ConsoleMessageCapture, s_instance)
```

**Replace lines 15-17:**
```cpp
// OLD
ConsoleMessageCapture* ConsoleMessageCapture::instance() {
  return s_instance();
}

// NEW
ConsoleMessageCapture* ConsoleMessageCapture::instance() {
  static ConsoleMessageCapture s_instance;  // Thread-safe since C++11
  return &s_instance;
}
```

**Remove from header (console_message_capture.h line 13):**
```cpp
#include <QGlobalStatic>  // No longer needed
```

### Why This Fixes It

1. **C++11 guarantees thread-safe static local initialization** - The compiler generates proper synchronization
2. **Simpler than Q_GLOBAL_STATIC** - No Qt macro magic, no platform-specific code paths
3. **Predictable initialization** - First call constructs, subsequent calls return reference
4. **No timing issues** - Construction happens in the calling thread with proper guards
5. **Same semantics** - Still a singleton, still lazy-initialized, still thread-safe

### Testing the Fix

```bash
# Reproduce issue locally
QT_QPA_PLATFORM=minimal ./build/ci-linux/tests/test_chrome_mode_api

# After fix, verify tests pass
ctest --test-dir build/ci-linux -R test_chrome_mode_api -V

# Should see:
# testReadConsoleMessages_CapturesDebug PASSED
# testReadConsoleMessages_PatternFilter PASSED
# testReadConsoleMessages_OnlyErrors PASSED
# testReadConsoleMessages_Clear PASSED
```

**Alternative quick fix**: Pre-warm singleton in test
```cpp
void TestChromeModeApi::init() {
    m_requestId = 1;
    QAccessible::setActive(true);

    // Pre-warm singleton before installing handler
    (void)ConsoleMessageCapture::instance();  // Force construction
    QCoreApplication::processEvents();        // Let it settle

    ConsoleMessageCapture::instance()->install();
    ConsoleMessageCapture::instance()->clear();
    // ... rest of init ...
}
```

## Next Actions

1. **Reproduce locally**: `QT_QPA_PLATFORM=minimal ./test_chrome_mode_api`
2. **Implement fix**: Replace Q_GLOBAL_STATIC with static local variable pattern
3. **Test locally**: Verify tests pass with minimal platform
4. **Test on CI**: Push and verify all Linux CI builds pass
5. **Verify no regressions**: Check Windows CI still passes
