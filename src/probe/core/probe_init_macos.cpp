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
// Runs when QCoreApplication starts. Shared body; see probe_deferred_init.h.
static void qtpilotAutoInit() {
  qtPilot::detail::startupHook();
}

// Register the startup function with Qt
Q_COREAPP_STARTUP_FUNCTION(qtpilotAutoInit)

// Library constructor - called when loaded via DYLD_INSERT_LIBRARIES or dlopen
// (macOS), or at image load when the probe is linked into the app (iOS).
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
  fprintf(stderr, "[qtPilot] Library loaded (macOS), waiting for Qt startup\n");
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

#endif  // Q_OS_MACOS
