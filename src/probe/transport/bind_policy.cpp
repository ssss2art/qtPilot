// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "transport/bind_policy.h"

#include <cstdio>

#include <QByteArray>
#include <QString>

namespace qtPilot {

namespace {

/// Warn once rather than on every announce tick: the broadcaster calls into the
/// policy every 5 seconds, and a repeating warning would bury the app's own
/// stderr output.
bool g_warnedInvalid = false;

}  // namespace

NetworkExposure configuredExposure() {
  const QByteArray raw = qgetenv("QTPILOT_BIND_ADDRESS");
  if (raw.isEmpty()) {
    return NetworkExposure::Lan;
  }

  const QString value = QString::fromUtf8(raw).trimmed().toLower();

  if (value == QLatin1String("loopback") || value == QLatin1String("localhost") ||
      value == QLatin1String("127.0.0.1") || value == QLatin1String("::1")) {
    return NetworkExposure::Loopback;
  }

  if (value == QLatin1String("any") || value == QLatin1String("all") ||
      value == QLatin1String("lan") || value == QLatin1String("0.0.0.0") ||
      value == QLatin1String("*")) {
    return NetworkExposure::Lan;
  }

  // Loopback, not the Lan default: the only reason to set this variable is to
  // restrict the probe, so a typo must not be resolved as "wide open". This
  // fails visibly -- connections are refused and the reason is on stderr.
  if (!g_warnedInvalid) {
    g_warnedInvalid = true;
    fprintf(stderr,
            "[qtPilot] QTPILOT_BIND_ADDRESS=\"%s\" is not a recognised value; "
            "restricting to loopback. Use \"any\" (default) or \"loopback\".\n",
            raw.constData());
    fflush(stderr);
  }
  return NetworkExposure::Loopback;
}

QHostAddress listenAddress() {
  return configuredExposure() == NetworkExposure::Lan ? QHostAddress(QHostAddress::Any)
                                                      : QHostAddress(QHostAddress::LocalHost);
}

QHostAddress announceAddress() {
  return configuredExposure() == NetworkExposure::Lan ? QHostAddress(QHostAddress::Broadcast)
                                                      : QHostAddress(QHostAddress::LocalHost);
}

}  // namespace qtPilot
