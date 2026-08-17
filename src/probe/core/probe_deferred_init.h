// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#ifndef QTPILOT_PROBE_DEFERRED_INIT_H
#define QTPILOT_PROBE_DEFERRED_INIT_H

#include "probe.h"

#include <QCoreApplication>
#include <QMetaObject>

namespace qtPilot {
namespace detail {

/// @brief Hand probe initialization to the event loop instead of running it now.
///
/// The probe opens a listening socket, and a socket cannot be registered until
/// the platform event dispatcher is wired up. Both delivery modes can reach
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
/// This lives in one place because the platform init files are near-duplicates
/// and that is exactly how the bug arose: the macOS file deferred on its
/// injected path but not on its linked one, and the Linux file did not defer on
/// its linked path either. A single definition means the next platform cannot
/// quietly drift back to initializing inline.
///
/// A queued invocation is used rather than QTimer because starting a timer
/// itself needs an event dispatcher. Posting an event needs only the thread's
/// event queue, which exists by this point, and delivery happens once the loop
/// runs -- by which time the application object is fully constructed.
///
/// Windows does not use this: it has no startup-function hook and initializes
/// from an InitOnce callback that already runs after Qt is up.
inline void scheduleInitialize() {
  QMetaObject::invokeMethod(
      QCoreApplication::instance(), []() { Probe::instance()->initialize(); },
      Qt::QueuedConnection);
}

}  // namespace detail
}  // namespace qtPilot

#endif  // QTPILOT_PROBE_DEFERRED_INIT_H
