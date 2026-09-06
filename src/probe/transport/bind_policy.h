// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "core/qtpilot_export.h"

#include <QHostAddress>

namespace qtPilot {

/// @brief How far the probe's sockets are allowed to reach.
///
/// The probe exposes qt.methods.invoke, which calls arbitrary slots on the host
/// application, and it does not authenticate its clients. The reachable surface
/// is therefore a security boundary -- but it is also a functional requirement:
/// qtPilot is used to drive instrumented applications running on OTHER machines
/// across a shared network, and discovery is how those instances are found at
/// all. A default that cannot see the network does not have a security
/// property; it has no product.
///
/// So the default is Lan, matching the behaviour before this setting existed,
/// and Loopback is the opt-in for single-machine work. The real mitigation for
/// the exposure is authentication, which does not exist yet -- see R7 in
/// docs/observability-testability-gaps.md.
enum class NetworkExposure {
  /// Loopback only. Reachable from the same machine, and from a device over
  /// `adb forward` / `iproxy`, which both terminate on the device's loopback.
  /// Opt in with QTPILOT_BIND_ADDRESS=loopback.
  Loopback,
  /// Every interface (the default). Required to reach an instrumented
  /// application on another host, and to be discoverable by one.
  Lan,
};

/// @brief The exposure requested via QTPILOT_BIND_ADDRESS.
///
/// Recognised values, case-insensitive:
///   - unset, `any`, `all`, `lan`, `0.0.0.0`, `*`      -> Lan (default)
///   - `loopback`, `localhost`, `127.0.0.1`, `::1`     -> Loopback
///
/// An unrecognised value is rejected with a warning and treated as Loopback,
/// NOT as the default. Someone who sets this variable at all is trying to
/// restrict the probe; honouring a typo as "wide open" would silently discard
/// that intent, and silent insecurity is worse than a visible outage. The
/// failure mode is a refused connection plus a message on stderr, which is
/// something an operator can see and fix.
QTPILOT_EXPORT NetworkExposure configuredExposure();

/// @brief The address the WebSocket server should listen on.
QTPILOT_EXPORT QHostAddress listenAddress();

/// @brief The destination for discovery announcements.
///
/// Follows the listen address. A LAN-bound probe broadcasts, so remote hosts can
/// find it; a loopback-bound probe announces to loopback only, because
/// advertising a process nobody outside this machine can connect to is noise
/// that also discloses the process.
QTPILOT_EXPORT QHostAddress announceAddress();

}  // namespace qtPilot
