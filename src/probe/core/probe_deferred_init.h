// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "probe.h"

#include <cstdio>

#include <QCoreApplication>
#include <QMetaObject>

namespace qtPilot {
namespace detail {

/// @brief The one-shot latch guarding probe startup.
///
/// A function-local static so the platform init files share exactly one instance
/// without each declaring its own copy -- the drift between those copies is what
/// this header exists to prevent.
inline bool& initAttempted() {
  static bool attempted = false;
  return attempted;
}

/// @brief Whether the probe was switched off via QTPILOT_ENABLED=0.
///
/// qgetenv, not getenv: MSVC deprecates getenv, and with -Werror that turned
/// C4996 into a hard error on every Windows leg the moment this helper moved into
/// a header Windows compiles. (It was previously inline in the POSIX-only init
/// files, which MSVC never saw.)
///
/// Safe from the pre-main library constructor as well as from the Qt startup
/// hook: qgetenv needs QtCore loaded, not a QCoreApplication, and the probe links
/// QtCore -- so the loader has already run QtCore's initializers before it runs
/// the probe's. The test suite leans on this, since it disables the probe through
/// exactly this variable.
inline bool disabledByEnvironment() {
  return qgetenv("QTPILOT_ENABLED") == "0";
}

/// @brief Hand probe initialization to the event loop instead of running it now.
///
/// The probe opens a listening socket, and a socket cannot be registered until
/// the platform event dispatcher is wired up. Every delivery mode can reach
/// initialization EARLIER than that:
///
///   - the library constructor runs before main(), so before Qt exists at all;
///   - Q_COREAPP_STARTUP_FUNCTION runs from qt_call_pre_routines(), which Qt
///     calls from INSIDE QCoreApplicationPrivate::init() -- the application
///     object is not finished constructing yet.
///
/// Initializing inline from the startup function segfaults on iOS:
/// QTcpServer::listen -> QCFSocketNotifier::registerSocketNotifier ->
/// QObject::thread() on a host dispatcher that is still null. Injection never
/// hit it because it attaches to an app that is already running; build-time
/// LINKING is the first delivery mode that arrives this early.
///
/// A queued invocation is used rather than QTimer because starting a timer
/// itself needs an event dispatcher. Posting an event needs only the thread's
/// event queue, which exists by this point, and delivery happens once the loop
/// runs -- by which time the application object is fully constructed.
///
/// Passing the application as the context object also fixes the thread affinity
/// the old QTimer::singleShot got wrong: a context-less singleShot created its
/// timer on the CALLING thread, so a probe delivered by dlopen from a worker
/// thread would have built its WebSocket server with that thread's affinity.
/// A queued invocation on qApp always runs on the application thread.
///
/// @return true if initialization was successfully queued. A false return means
///         the probe will NOT start, and says so on stderr -- silence here was
///         the hardest possible failure to attribute.
inline bool scheduleInitialize() {
  QCoreApplication* app = QCoreApplication::instance();
  if (app == nullptr) {
    fprintf(stderr,
            "[qtPilot] Cannot start the probe: no QCoreApplication to schedule "
            "initialization onto.\n");
    return false;
  }

  const bool queued = QMetaObject::invokeMethod(
      app, []() { Probe::instance()->initialize(); }, Qt::QueuedConnection);
  if (!queued) {
    fprintf(stderr,
            "[qtPilot] Failed to queue probe initialization on the application "
            "thread; the probe will not start.\n");
  }
  return queued;
}

/// @brief Body of the Q_COREAPP_STARTUP_FUNCTION hook, shared by every platform
/// that has one.
///
/// This lives in one place because the platform init files are near-duplicates
/// and that is exactly how the original bug arose: the macOS file deferred on
/// its injected path but not on its linked one, the Linux file did not defer on
/// its linked path either, and Windows still initialized inline from the
/// InitOnce callback while a comment here claimed it had no startup hook at all.
/// A single definition means the next platform cannot quietly drift back to
/// initializing inline.
///
/// The latch is set only on success, so a failed schedule can still be retried
/// through ensureInitialized().
inline void startupHook() {
  if (disabledByEnvironment()) {
    return;
  }
  if (initAttempted()) {
    return;
  }
  if (scheduleInitialize()) {
    initAttempted() = true;
  }
}

/// @brief Body of the public qtPilot::ensureInitialized() entry point.
///
/// The recovery path for a delivery mode where the startup hook never fired --
/// notably a statically linked probe whose archive member was pulled in by an
/// explicit reference rather than by whole-archive linking. Safe to call from any
/// thread and any number of times: scheduleInitialize() posts to the application
/// thread, and Probe::initialize() is itself idempotent.
inline void ensureInitializedImpl() {
  if (disabledByEnvironment()) {
    return;
  }
  if (initAttempted()) {
    return;
  }
  if (QCoreApplication::instance() == nullptr) {
    // Nothing to post to yet. Deliberately does NOT latch, so the startup hook
    // (or a later call) can still start the probe.
    return;
  }
  if (scheduleInitialize()) {
    initAttempted() = true;
  }
}

}  // namespace detail
}  // namespace qtPilot
