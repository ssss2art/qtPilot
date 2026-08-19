// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

// Linux library constructor for qtPilot probe.
//
// When loaded via LD_PRELOAD, the constructor runs before main().
// At that point, QCoreApplication may not exist yet, so we must defer
// initialization until Qt is ready.

#if defined(__linux__) || (defined(__unix__) && !defined(__APPLE__))

#include "probe.h"
#include "probe_deferred_init.h"

#include <QCoreApplication>

namespace qtPilot {

/// @brief Ensure the probe is initialized.
///
/// The recovery path for a delivery mode where Q_COREAPP_STARTUP_FUNCTION never
/// fired. It is also the one symbol in this translation unit a consuming app can
/// reference by name, which is what lets a statically linked probe be pulled out
/// of its archive without whole-archive linking -- so it has to actually work.
/// It previously could not: it was gated on a flag that was never assigned, which
/// made it an unconditional no-op and left tryInitialize() unreachable.
void ensureInitialized() {
  detail::ensureInitializedImpl();
}

}  // namespace qtPilot

// Automatic initialization hook using Q_COREAPP_STARTUP_FUNCTION.
// Runs when QCoreApplication starts. Android reaches this path whenever the probe
// is linked rather than preloaded. Shared body; see probe_deferred_init.h.
static void qtpilotAutoInit() {
  qtPilot::detail::startupHook();
}

// Register the startup function with Qt
Q_COREAPP_STARTUP_FUNCTION(qtpilotAutoInit)

// Library constructor - called when loaded via LD_PRELOAD or dlopen.
// Runs BEFORE main(), so QCoreApplication may not exist.
__attribute__((constructor)) static void onLibraryLoad() {
  if (qtPilot::detail::disabledByEnvironment()) {
    // Latch so no later hook or explicit call starts the probe.
    qtPilot::detail::initAttempted() = true;
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
  if (!qtPilot::detail::initAttempted()) {
    return;
  }
  // Q_GLOBAL_STATIC returns nullptr after destruction - check before use
  auto* probe = qtPilot::Probe::instance();
  if (probe && probe->isInitialized()) {
    probe->shutdown();
  }
}

#endif  // __linux__ || (__unix__ && !__APPLE__)
