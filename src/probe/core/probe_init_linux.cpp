// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

// Linux library constructor for qtPilot probe.
//
// When loaded via LD_PRELOAD, the constructor runs before main().
// At that point, QCoreApplication may not exist yet, so we must defer
// initialization until Qt is ready.

#if defined(__linux__) || (defined(__unix__) && !defined(__APPLE__))

#include "probe.h"

#include <QCoreApplication>
#include <QTimer>

namespace {

// Flag indicating we need to initialize when Qt becomes ready.
// Set by constructor if QCoreApplication doesn't exist yet.
bool g_needsDeferredInit = false;

// Flag to prevent multiple initialization attempts.
bool g_initAttempted = false;

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

  // Use QTimer::singleShot to defer to the event loop.
  // This ensures Qt is fully initialized and the event loop is running.
  QTimer::singleShot(0, []() { qtPilot::Probe::instance()->initialize(); });

  return true;
}

}  // namespace

namespace qtPilot {

/// @brief Ensure the probe is initialized.
///
/// Call this from any code path that needs the probe to be ready.
/// On Linux, this triggers deferred initialization if Qt is now ready.
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
  // QCoreApplication now exists, safe to initialize the probe
  g_initAttempted = true;
  qtPilot::Probe::instance()->initialize();
}

// Register the startup function with Qt
Q_COREAPP_STARTUP_FUNCTION(qtpilotAutoInit)

// Library constructor - called when loaded via LD_PRELOAD or dlopen.
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
  fprintf(stderr, "[qtPilot] Library loaded, waiting for Qt startup\n");
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

#endif  // __linux__ || (__unix__ && !__APPLE__)
