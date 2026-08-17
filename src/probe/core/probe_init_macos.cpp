// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

// macOS library constructor for qtPilot probe.
//
// When loaded via DYLD_INSERT_LIBRARIES, the constructor runs before main().
// At that point, QCoreApplication may not exist yet, so we must defer
// initialization until Qt is ready.

#include <QtGlobal>  // for Q_OS_MACOS / Q_OS_IOS — must precede the guard
// iOS is included deliberately. The DYLD_INSERT_LIBRARIES path below is
// macOS-only -- iOS does not permit inserting libraries into a third-party
// app -- but the OTHER delivery mode, build-time LINKING, is exactly how a
// probe reaches a device app. With this file compiled out on iOS the
// Q_COREAPP_STARTUP_FUNCTION below never registered, so a linked probe sat
// inert with no way to start.
#if defined(Q_OS_MACOS) || defined(Q_OS_IOS)

#include "probe.h"

#include <QCoreApplication>

namespace {

// Flag indicating we need to initialize when Qt becomes ready.
// Set by constructor if QCoreApplication doesn't exist yet.
bool g_needsDeferredInit = false;

// Flag to prevent multiple initialization attempts.
bool g_initAttempted = false;

/// @brief Hand initialization to the event loop rather than running it here.
///
/// The probe opens a listening socket, and a socket cannot be registered until
/// the platform event dispatcher is wired up. Both entry points below can run
/// EARLIER than that, so neither may initialize inline:
///
///   - the library constructor runs before main(), so before Qt exists at all;
///   - Q_COREAPP_STARTUP_FUNCTION runs from qt_call_pre_routines(), which is
///     called from INSIDE QCoreApplicationPrivate::init() -- the application
///     object is not finished constructing yet.
///
/// Initializing inline from the startup function segfaults on iOS:
/// QTcpServer::listen -> QCFSocketNotifier::registerSocketNotifier ->
/// QObject::thread() on a host dispatcher that is still null. Injection never
/// hit this because it attaches to an already-running app; build-time LINKING
/// is the first delivery mode that reaches this code path this early.
///
/// A queued invocation is used rather than QTimer, because starting a timer
/// itself needs an event dispatcher. Posting an event only needs the thread's
/// event queue, which exists, and delivery happens once the loop runs -- by
/// which point the application object is fully constructed.
void scheduleInitialize() {
  QMetaObject::invokeMethod(
      QCoreApplication::instance(), []() { qtPilot::Probe::instance()->initialize(); },
      Qt::QueuedConnection);
}

/// @brief Attempt to initialize the probe if Qt is ready.
/// @return true if initialization was performed or already done.
bool tryInitialize() {
  if (g_initAttempted) {
    return true;
  }

  // Check if Qt application exists
  if (QCoreApplication::instance() == nullptr) {
    return false;
  }

  g_initAttempted = true;
  scheduleInitialize();

  return true;
}

}  // namespace

namespace qtPilot {

/// @brief Ensure the probe is initialized.
///
/// Call this from any code path that needs the probe to be ready.
/// On macOS, this triggers deferred initialization if Qt is now ready.
void ensureInitialized() {
  if (g_needsDeferredInit && !g_initAttempted) {
    tryInitialize();
  }
}

}  // namespace qtPilot

// Automatic initialization hook using Q_COREAPP_STARTUP_FUNCTION.
// This function runs automatically when QCoreApplication starts.
// It's the safe place to trigger probe initialization after Qt is ready.
static void qtpilotAutoInit() {
  // Check if probe is disabled via environment
  QByteArray enabled = qgetenv("QTPILOT_ENABLED");
  if (enabled == "0") {
    return;  // Probe disabled
  }
  // QCoreApplication::self is assigned before pre-routines run, so instance()
  // is non-null here -- but the object it points at is still inside its own
  // constructor. Defer; see scheduleInitialize().
  g_initAttempted = true;
  scheduleInitialize();
}

// Register the startup function with Qt
Q_COREAPP_STARTUP_FUNCTION(qtpilotAutoInit)

// Library constructor - called when loaded via DYLD_INSERT_LIBRARIES or dlopen
// (macOS), or at image load when the probe is linked into the app (iOS).
// Runs BEFORE main(), so QCoreApplication may not exist.
__attribute__((constructor)) static void onLibraryLoad() {
  // Check if probe is disabled via environment
  const char* enabled = getenv("QTPILOT_ENABLED");
  if (enabled != nullptr && enabled[0] == '0' && enabled[1] == '\0') {
    // Probe disabled - don't initialize
    g_initAttempted = true;  // Prevent future attempts
    return;
  }

  // Note: We no longer try to initialize here because Q_COREAPP_STARTUP_FUNCTION
  // will trigger automatically when QCoreApplication is created. This is more
  // reliable than polling or manual triggering.

  // Log to stderr since Qt logging may not be available yet
  fprintf(stderr, "[qtPilot] Library loaded (macOS), waiting for Qt startup\n");
}

// Library destructor - called on library unload.
__attribute__((destructor)) static void onLibraryUnload() {
  if (!g_initAttempted) {
    return;
  }
  // Q_GLOBAL_STATIC returns nullptr after destruction - check before use
  auto* probe = qtPilot::Probe::instance();
  if (probe && probe->isInitialized()) {
    probe->shutdown();
  }
}

#endif  // Q_OS_MACOS
