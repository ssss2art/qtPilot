# Phase 1: Foundation - Research

**Researched:** 2026-01-29
**Domain:** DLL/LD_PRELOAD injection and WebSocket transport for Qt applications
**Confidence:** HIGH

## Summary

Phase 1 establishes the injection and transport foundation for qtPilot. The probe must load into any Qt application on Windows (via launcher with DLL injection) and Linux (via LD_PRELOAD), then expose a WebSocket server for JSON-RPC 2.0 communication.

The primary technical challenges are Windows-specific: CRT mismatch, TLS corruption in injected DLLs, and loader lock deadlocks from DllMain. These are well-documented pitfalls with established solutions. Linux injection via LD_PRELOAD is straightforward in comparison.

The WebSocket transport uses Qt's native QWebSocketServer module, which integrates naturally with Qt's event loop. JSON-RPC 2.0 handling can be implemented using QJsonDocument (built into Qt Core) without external dependencies.

**Primary recommendation:** Implement a minimal DllMain/constructor that only sets a flag, then defer all real initialization to the first client connection or explicit `Initialize()` call triggered after Qt's event loop starts.

## Standard Stack

The established libraries/tools for this domain:

### Core

| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| Qt WebSockets | 5.15.x / 6.x | WebSocket server in probe | Native Qt integration, signal/slot based, no external deps |
| QJsonDocument | 5.15.x / 6.x | JSON-RPC message parsing | Part of Qt Core, native QString/QVariant integration |
| Windows API | Win32 | DLL injection, process management | CreateRemoteThread + LoadLibrary is the standard technique |
| LD_PRELOAD | POSIX | Linux library injection | Standard POSIX mechanism, used by GammaRay, Qt-Inspector |

### Supporting

| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| InitOnceBeginInitialize | Vista+ | Thread-safe one-time init | Replace std::call_once in injected Windows DLLs |
| FlsAlloc/FlsGetValue | Vista+ | Fiber Local Storage | If thread-local storage is needed (avoid TLS entirely if possible) |

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| QJsonDocument | nlohmann/json | Better C++ ergonomics but adds vcpkg dependency |
| Qt WebSockets | jcon-cpp | Full JSON-RPC framework but more complex, MIT licensed |
| Custom injection | kubo/injector | Cross-platform library but archived (Sept 2025), LGPL |
| CreateRemoteThread | SetWindowsHookEx | Only works for GUI apps, less general |

**Installation:**

Qt WebSockets is part of the Qt installation:
```cmake
find_package(Qt6 REQUIRED COMPONENTS Core Network WebSockets)
# or for Qt 5:
find_package(Qt5 5.15 REQUIRED COMPONENTS Core Network WebSockets)
```

No vcpkg dependencies are required for Phase 1.

## Architecture Patterns

### Recommended Project Structure

```
src/
├── probe/
│   ├── CMakeLists.txt
│   ├── core/
│   │   ├── probe.h           # Singleton probe class
│   │   ├── probe.cpp
│   │   ├── probe_init.cpp    # Platform entry points (DllMain, constructor)
│   │   └── deferred_init.h   # Safe deferred initialization helpers
│   └── transport/
│       ├── websocket_server.h
│       ├── websocket_server.cpp
│       ├── jsonrpc_handler.h
│       └── jsonrpc_handler.cpp
├── launcher/
│   ├── CMakeLists.txt
│   ├── main.cpp              # qtpilot-launch CLI
│   └── process_launcher.cpp  # Platform-specific process launch + injection
```

### Pattern 1: Deferred Initialization

**What:** DllMain/constructor only sets a flag; real initialization happens later.

**When to use:** Always on Windows; recommended on Linux for consistency.

**Why:** Windows loader lock prevents calling Qt functions, LoadLibrary, thread creation, or most Win32 APIs from DllMain. Even on Linux, deferring initialization until after Qt's event loop starts is safer.

**Example:**

```cpp
// probe_init_windows.cpp
#include <Windows.h>
#include <synchapi.h>

namespace {
    INIT_ONCE g_initOnce = INIT_ONCE_STATIC_INIT;
    bool g_dllLoaded = false;
}

// SAFE: Only sets a flag
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        g_dllLoaded = true;
        // DO NOT: call Qt functions, create threads, load libraries
    }
    return TRUE;
}

// Called later, after Qt event loop is running
BOOL CALLBACK InitOnceCallback(PINIT_ONCE, PVOID, PVOID*) {
    // SAFE: Now we can do real work
    qtpilot::Probe::instance()->initialize();
    return TRUE;
}

void qtpilot::ensureInitialized() {
    if (!g_dllLoaded) return;
    InitOnceExecuteOnce(&g_initOnce, InitOnceCallback, nullptr, nullptr);
}
```

### Pattern 2: Qt Event Loop Integration

**What:** Hook into Qt's event loop to trigger probe initialization.

**When to use:** After DLL is loaded, need to detect when Qt is ready.

**Why:** Qt must be fully initialized before we can use QWebSocketServer.

**Example:**

```cpp
// probe_init_linux.cpp
#include <QtCore/QCoreApplication>
#include <QtCore/QTimer>

namespace {
    bool g_initialized = false;
}

__attribute__((constructor))
static void onLibraryLoad() {
    // Queue initialization for after Qt starts
    // This works because QCoreApplication::instance() may be null here
    if (QCoreApplication::instance()) {
        QTimer::singleShot(0, []() {
            qtpilot::Probe::instance()->initialize();
        });
    } else {
        // Qt not started yet - will need to hook startup
        // Option 1: Use qtHookData[QHooks::Startup] (private API)
        // Option 2: Wait for first WebSocket connection attempt
    }
}
```

### Pattern 3: Single-Client WebSocket Server

**What:** Accept only one WebSocket connection at a time.

**When to use:** Per CONTEXT.md decision - single client only.

**Why:** Simplifies state management; probe serves one client (Claude) at a time.

**Example:**

```cpp
// websocket_server.cpp
class WebSocketServer : public QObject {
    Q_OBJECT
public:
    explicit WebSocketServer(quint16 port, QObject* parent = nullptr);

private slots:
    void onNewConnection() {
        auto* socket = server_->nextPendingConnection();

        if (activeClient_) {
            // Reject - already have a client
            socket->close(QWebSocketProtocol::CloseCodePolicyViolated,
                         "Another client is already connected");
            socket->deleteLater();
            return;
        }

        activeClient_ = socket;
        connect(socket, &QWebSocket::textMessageReceived,
                this, &WebSocketServer::onTextMessage);
        connect(socket, &QWebSocket::disconnected,
                this, &WebSocketServer::onClientDisconnected);
    }

    void onClientDisconnected() {
        activeClient_->deleteLater();
        activeClient_ = nullptr;
        // Server keeps listening for new connections
    }

private:
    QWebSocketServer* server_ = nullptr;
    QWebSocket* activeClient_ = nullptr;
};
```

### Pattern 4: Windows Process Launch + Injection

**What:** Launch target process suspended, inject DLL, then resume.

**When to use:** The `qtpilot-launch.exe` launcher on Windows.

**Why:** Clean injection before target's main() runs; no admin rights needed.

**Example:**

```cpp
// process_launcher.cpp (Windows)
bool launchWithProbe(const QString& targetExe, const QStringList& args,
                     const QString& probeDllPath) {
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};

    QString cmdLine = QString("\"%1\" %2").arg(targetExe, args.join(" "));

    // 1. Create process suspended
    if (!CreateProcessW(
            nullptr,
            cmdLine.toStdWString().data(),
            nullptr, nullptr, FALSE,
            CREATE_SUSPENDED,
            nullptr, nullptr, &si, &pi)) {
        return false;
    }

    // 2. Allocate memory in target for DLL path
    void* remoteMem = VirtualAllocEx(pi.hProcess, nullptr,
                                      (probeDllPath.size() + 1) * sizeof(wchar_t),
                                      MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteMem) {
        TerminateProcess(pi.hProcess, 1);
        return false;
    }

    // 3. Write DLL path to target
    WriteProcessMemory(pi.hProcess, remoteMem,
                       probeDllPath.toStdWString().c_str(),
                       (probeDllPath.size() + 1) * sizeof(wchar_t),
                       nullptr);

    // 4. Get LoadLibraryW address (same in all processes due to ASLR base)
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    auto loadLibraryW = GetProcAddress(kernel32, "LoadLibraryW");

    // 5. Create remote thread to load our DLL
    HANDLE hThread = CreateRemoteThread(
        pi.hProcess, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(loadLibraryW),
        remoteMem, 0, nullptr);

    if (hThread) {
        WaitForSingleObject(hThread, INFINITE);
        CloseHandle(hThread);
    }

    // 6. Resume main thread
    ResumeThread(pi.hThread);

    // Cleanup
    VirtualFreeEx(pi.hProcess, remoteMem, 0, MEM_RELEASE);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    return true;
}
```

### Anti-Patterns to Avoid

- **Calling Qt functions from DllMain:** Qt may call LoadLibrary, create threads, or use synchronization internally. Always defer.
- **Using `thread_local` in the probe DLL:** TLS slots are not allocated for dynamically loaded DLLs. Use FlsAlloc or avoid altogether.
- **Using `std::call_once` in injected DLL:** It uses TLS internally on MSVC. Use Windows InitOnce API instead.
- **Passing STL containers across DLL boundary:** CRT mismatch causes heap corruption. Use C-style interfaces or Qt types.
- **Synchronous WebSocket operations:** Will block Qt event loop. Always use signals/slots.

## Don't Hand-Roll

Problems that look simple but have existing solutions:

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| WebSocket server | Raw TCP + framing | QWebSocketServer | RFC 6455 compliance, Qt event loop integration |
| JSON parsing | Manual parser | QJsonDocument | Performance, correctness, QString integration |
| One-time init | Static bool + mutex | InitOnceExecuteOnce | Lock-free fast path, handles exceptions |
| CLI argument parsing | Manual argv parsing | QCommandLineParser | Handles quoting, escaping, help generation |
| Thread-safe singleton | Double-checked lock | Q_GLOBAL_STATIC | Platform-safe, lazy initialization |

**Key insight:** Every "simple" component in DLL injection has edge cases that took years to discover. Use battle-tested code.

## Common Pitfalls

### Pitfall 1: CRT Mismatch Heap Corruption

**What goes wrong:** Probe DLL compiled with different MSVC version than target Qt app. Memory allocated by one CRT, freed by another, causes heap corruption.

**Why it happens:** Each MSVC version has its own C runtime with separate heap. STL containers, std::string crossing DLL boundary use the wrong allocator.

**How to avoid:**
1. Build probe with `/MD` (dynamic CRT) to match most Qt apps
2. Never pass STL types across DLL boundary - use Qt types or raw pointers
3. Memory allocated in DLL must be freed in DLL
4. Document MSVC version requirements clearly

**Warning signs:** Crashes in `free()`, "heap corruption detected", works in Debug but crashes in Release.

### Pitfall 2: TLS Corruption in Injected DLLs

**What goes wrong:** DLL using `thread_local` or `__declspec(thread)` corrupts other modules' TLS when loaded via LoadLibrary.

**Why it happens:** Windows loader only allocates TLS slots for DLLs present at process start. Dynamic DLLs get index 0, trampling the main executable's TLS.

**How to avoid:**
1. Never use `thread_local` or `__declspec(thread)` in probe
2. Avoid `std::call_once` (uses TLS on MSVC)
3. Use explicit FLS API if per-thread storage needed: `FlsAlloc()`, `FlsGetValue()`, `FlsSetValue()`
4. Audit dependencies for implicit TLS usage

**Warning signs:** Strange global state changes, variables resetting, works sometimes but fails in production.

### Pitfall 3: Loader Lock Deadlock

**What goes wrong:** DllMain calls forbidden function, process hangs forever with no error.

**Why it happens:** Windows loader holds a global lock during DllMain. Functions like LoadLibrary, CreateThread, User32/Gdi32 functions try to acquire same lock.

**How to avoid:**
1. DllMain only sets flags - no real work
2. Use `DisableThreadLibraryCalls()` to reduce callback overhead
3. Defer initialization to after DLL load completes
4. Never call: LoadLibrary, CreateThread, CoInitialize, registry functions, Qt functions

**Warning signs:** Process freezes on startup, no crash dump, debugger shows loader lock held.

### Pitfall 4: Qt Not Initialized When Constructor Runs

**What goes wrong:** LD_PRELOAD constructor tries to use QCoreApplication before it exists.

**Why it happens:** Library constructors run before main(), but Qt apps create QApplication in main().

**How to avoid:**
1. Check `QCoreApplication::instance()` before using Qt
2. If null, register for later initialization via qtHookData or timer
3. Consider lazy initialization on first WebSocket connection

**Warning signs:** Segfault in Qt functions, "QApplication::instance() called without QApplication".

### Pitfall 5: WebSocket Thread Affinity

**What goes wrong:** QWebSocket accessed from wrong thread, undefined behavior or crashes.

**Why it happens:** Qt objects have thread affinity. QWebSocket must be used from the thread that created it.

**How to avoid:**
1. Create QWebSocketServer on main thread
2. Never call QWebSocket methods from worker threads
3. Use `QMetaObject::invokeMethod` with `Qt::QueuedConnection` for cross-thread calls
4. Process all messages via Qt's event loop

**Warning signs:** "Cannot send events to objects owned by a different thread", random crashes on message send.

## Code Examples

Verified patterns from official sources and established projects:

### JSON-RPC 2.0 Request Handler

```cpp
// Source: JSON-RPC 2.0 specification + Qt best practices
// jsonrpc_handler.cpp

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

class JsonRpcHandler : public QObject {
    Q_OBJECT
public:
    QString handleRequest(const QString& message) {
        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &error);

        if (error.error != QJsonParseError::NoError) {
            return formatError(-32700, "Parse error", QJsonValue());
        }

        QJsonObject request = doc.object();

        // Validate JSON-RPC 2.0
        if (request["jsonrpc"].toString() != "2.0") {
            return formatError(-32600, "Invalid Request", request["id"]);
        }

        QString method = request["method"].toString();
        QJsonValue params = request["params"];
        QJsonValue id = request["id"];

        if (method.isEmpty()) {
            return formatError(-32600, "Invalid Request", id);
        }

        // Notification (no id) - no response required
        if (id.isNull() || id.isUndefined()) {
            emit notificationReceived(method, params);
            return QString();  // No response for notifications
        }

        // Method call
        QJsonValue result = dispatchMethod(method, params);
        return formatResult(result, id);
    }

private:
    QString formatResult(const QJsonValue& result, const QJsonValue& id) {
        QJsonObject response;
        response["jsonrpc"] = "2.0";
        response["result"] = result;
        response["id"] = id;
        return QJsonDocument(response).toJson(QJsonDocument::Compact);
    }

    QString formatError(int code, const QString& message, const QJsonValue& id) {
        QJsonObject error;
        error["code"] = code;
        error["message"] = message;

        QJsonObject response;
        response["jsonrpc"] = "2.0";
        response["error"] = error;
        response["id"] = id.isNull() ? QJsonValue() : id;
        return QJsonDocument(response).toJson(QJsonDocument::Compact);
    }

    QJsonValue dispatchMethod(const QString& method, const QJsonValue& params);

signals:
    void notificationReceived(const QString& method, const QJsonValue& params);
};
```

### Linux LD_PRELOAD Launcher

```cpp
// Source: Qt-Inspector pattern (https://github.com/robertknight/Qt-Inspector)
// launcher_linux.cpp

#include <QProcess>
#include <QProcessEnvironment>
#include <QFileInfo>

bool launchWithProbe(const QString& targetExe, const QStringList& args,
                     const QString& probeLibPath, quint16 port) {
    QProcess process;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

    // Get absolute path to probe library
    QString absProbe = QFileInfo(probeLibPath).absoluteFilePath();

    // Prepend to LD_PRELOAD (preserve existing preloads)
    QString existingPreload = env.value("LD_PRELOAD");
    if (existingPreload.isEmpty()) {
        env.insert("LD_PRELOAD", absProbe);
    } else {
        env.insert("LD_PRELOAD", absProbe + " " + existingPreload);
    }

    // Set configuration
    env.insert("QTPILOT_PORT", QString::number(port));

    process.setProcessEnvironment(env);
    process.setProgram(targetExe);
    process.setArguments(args);

    // Start and optionally wait
    process.start();
    return process.waitForStarted();
}
```

### Minimal DllMain Template

```cpp
// Source: Microsoft DLL Best Practices
// dllmain.cpp

#include <Windows.h>

namespace qtpilot {
    void ensureInitialized();  // Defined elsewhere, uses InitOnce
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        // Optimization: Don't need thread attach/detach notifications
        DisableThreadLibraryCalls(hModule);
        // DO NOT initialize here - loader lock is held
        // Real initialization happens in ensureInitialized()
        break;

    case DLL_PROCESS_DETACH:
        // Only cleanup if process is not terminating
        if (reserved == nullptr) {
            // Normal unload - can do cleanup
        }
        // If reserved != nullptr, process is terminating
        // Don't access global data, it may be destroyed
        break;
    }
    return TRUE;
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| TlsAlloc for per-thread state | FlsAlloc (FLS API) | Windows Vista (2006) | FLS works with fibers and is DLL-injection safe |
| std::call_once everywhere | InitOnceExecuteOnce on Windows | MSVC std::call_once uses TLS | Avoid TLS in injected DLLs |
| Manual mutex for one-time init | Q_GLOBAL_STATIC | Qt 5.1 | Thread-safe, lazy, platform-safe |
| CreateRemoteThread only | kubo/injector library | 2015-2025 | Cross-platform but now archived |

**Deprecated/outdated:**
- kubo/injector: Repository archived September 2025, not maintained. Roll own injection or use as reference only.
- Qt 4.x patterns: Qt 4 is unsupported; don't reference old code.
- Implicit TLS (`__declspec(thread)`): Broken for dynamically loaded DLLs on Windows.

## Open Questions

Things that couldn't be fully resolved:

1. **Qt version detection at runtime**
   - What we know: Can call `qVersion()` to get string like "5.15.2"
   - What's unclear: Best way to verify ABI compatibility before using Qt symbols
   - Recommendation: Call `qVersion()` early, log version, fail gracefully on mismatch

2. **Qt initialization timing on Linux**
   - What we know: LD_PRELOAD constructor runs before main(), QCoreApplication may not exist
   - What's unclear: Cleanest hook point for "Qt is ready" without using private APIs
   - Recommendation: Check QCoreApplication::instance() in constructor; if null, use QTimer::singleShot when it becomes available, or lazy-init on first connection

3. **32-bit process injection from 64-bit launcher**
   - What we know: CreateRemoteThread works, but GetProcAddress returns wrong address
   - What's unclear: Whether we need to support this scenario (32-bit Qt apps from 64-bit launcher)
   - Recommendation: Defer - build matching bitness launcher and probe for now

## Sources

### Primary (HIGH confidence)

- [Microsoft Learn: Dynamic-Link Library Best Practices](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-best-practices) - DllMain restrictions, loader lock
- [Microsoft Learn: One-Time Initialization](https://learn.microsoft.com/en-us/windows/win32/sync/one-time-initialization) - InitOnceExecuteOnce pattern
- [Microsoft Learn: Using TLS in a DLL](https://learn.microsoft.com/en-us/windows/win32/dlls/using-thread-local-storage-in-a-dynamic-link-library) - TLS pitfalls
- [Qt Documentation: QWebSocketServer](https://doc.qt.io/qt-6/qwebsocketserver.html) - WebSocket server API
- [GammaRay Source Code](https://github.com/KDAB/GammaRay) - Injection patterns, qtHookData usage

### Secondary (MEDIUM confidence)

- [Apriorit: Using GCC Constructor Attribute with LD_PRELOAD](https://www.apriorit.com/dev-blog/537-using-constructor-attribute-with-ld-preload) - Linux injection patterns
- [Qt-Inspector Source](https://github.com/robertknight/Qt-Inspector) - LD_PRELOAD launcher implementation
- [jcon-cpp](https://github.com/joncol/jcon-cpp) - Qt JSON-RPC over WebSocket reference (MIT license)
- [Nynaeve: TLS Design Problems](http://www.nynaeve.net/?p=187) - Deep dive on TLS corruption

### Tertiary (LOW confidence)

- [kubo/injector](https://github.com/kubo/injector) - Cross-platform injection library (archived, LGPL, reference only)
- Various security blogs on DLL injection - Good for understanding but security-focused, not development

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH - Qt WebSockets and QJsonDocument are documented, stable APIs
- Architecture: HIGH - Patterns from GammaRay/Qt-Inspector have years of production use
- Pitfalls: HIGH - Microsoft documentation is authoritative for Windows issues
- Windows injection: MEDIUM - CreateRemoteThread is well-known but edge cases exist
- Linux injection: HIGH - LD_PRELOAD is standard POSIX behavior

**Research date:** 2026-01-29
**Valid until:** 2026-03-01 (60 days - stable domain, Windows APIs don't change)
