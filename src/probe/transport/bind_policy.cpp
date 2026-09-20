// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "transport/bind_policy.h"

#include <algorithm>
#include <cstdio>
#include <ranges>
#include <string>
#include <string_view>

#include <QByteArray>
#include <QString>

namespace qtPilot {

namespace {

/// Warn once rather than on every announce tick: the broadcaster calls into the
/// policy every 5 seconds, and a repeating warning would bury the app's own
/// stderr output.
bool g_warnedInvalid = false;

constexpr std::string_view kLoopbackValues[] = {"loopback", "localhost", "127.0.0.1", "::1"};

constexpr std::string_view kLanValues[] = {"any", "all", "lan", "0.0.0.0", "*"};

}  // namespace

std::expected<NetworkExposure, QString> parseExposure(const QString& rawValue) {
  const QString trimmed = rawValue.trimmed();
  if (trimmed.isEmpty()) {
    return std::unexpected(QStringLiteral("Empty exposure string"));
  }
  const std::string lower = trimmed.toLower().toStdString();

  if (std::ranges::find(kLoopbackValues, lower) != std::end(kLoopbackValues)) {
    return NetworkExposure::Loopback;
  }
  if (std::ranges::find(kLanValues, lower) != std::end(kLanValues)) {
    return NetworkExposure::Lan;
  }

  return std::unexpected(
      QStringLiteral("Unrecognised exposure value: '%1'. Use 'any' or 'loopback'.").arg(rawValue));
}

NetworkExposure configuredExposure() {
  const QByteArray raw = qgetenv("QTPILOT_BIND_ADDRESS");
  if (raw.isEmpty()) {
    return NetworkExposure::Lan;
  }

  return parseExposure(QString::fromUtf8(raw))
      .or_else([&](const QString&) -> std::expected<NetworkExposure, QString> {
        if (!g_warnedInvalid) {
          g_warnedInvalid = true;
          fprintf(stderr,
                  "[qtPilot] QTPILOT_BIND_ADDRESS=\"%s\" is not a recognised value; "
                  "restricting to loopback. Use \"any\" (default) or \"loopback\".\n",
                  raw.constData());
          fflush(stderr);
        }
        return NetworkExposure::Loopback;
      })
      .value();
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
