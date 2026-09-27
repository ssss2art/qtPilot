// Copyright (c) 2026 qtPilot Contributors
// SPDX-License-Identifier: MIT
#include "transport/network_policy.h"

namespace qtPilot {
namespace {
std::expected<OperatingProfile, QString> parseProfile(const QProcessEnvironment& environment) {
  if (!environment.contains(QStringLiteral("QTPILOT_PROFILE"))) {
    return OperatingProfile::Legacy;
  }
  const QString value = environment.value(QStringLiteral("QTPILOT_PROFILE"));
  if (value == QStringLiteral("local")) {
    return OperatingProfile::Local;
  }
  if (value == QStringLiteral("trusted-network")) {
    return OperatingProfile::TrustedNetwork;
  }
  if (value == QStringLiteral("remote")) {
    return OperatingProfile::Remote;
  }
  return std::unexpected(QStringLiteral("Invalid QTPILOT_PROFILE"));
}
}  // namespace

std::expected<NetworkPolicy, QString> resolveNetworkPolicy(const QProcessEnvironment& environment) {
  return parseProfile(environment)
      .and_then([&](OperatingProfile profile) -> std::expected<NetworkPolicy, QString> {
        NetworkPolicy policy;
        policy.profile = profile;
        policy.discoveryEnabled =
            profile == OperatingProfile::Legacy || profile == OperatingProfile::TrustedNetwork;
        policy.exposure = profile == OperatingProfile::Local || profile == OperatingProfile::Remote
                              ? NetworkExposure::Loopback
                              : NetworkExposure::Lan;
        const QString bind = environment.value(QStringLiteral("QTPILOT_BIND_ADDRESS"));
        if (!bind.isEmpty()) {
          const auto exposure = parseExposure(bind);
          if (!exposure && profile != OperatingProfile::Legacy) {
            return std::unexpected(QStringLiteral("Invalid bind setting for operating profile"));
          }
          policy.exposure = exposure.value_or(NetworkExposure::Loopback);
        }
        if ((profile == OperatingProfile::Local && policy.exposure != NetworkExposure::Loopback) ||
            (profile == OperatingProfile::TrustedNetwork &&
             policy.exposure != NetworkExposure::Lan)) {
          return std::unexpected(QStringLiteral("Explicit bind conflicts with operating profile"));
        }
        for (const auto* name :
             {"QTPILOT_AUTH_TOKEN_FILE", "QTPILOT_TLS_CERT_FILE", "QTPILOT_TLS_KEY_FILE"}) {
          const QString key = QString::fromLatin1(name);
          if (environment.contains(key) && environment.value(key).isEmpty()) {
            return std::unexpected(QStringLiteral("Configured credential file path is empty"));
          }
        }
        policy.tokenFile = environment.value(QStringLiteral("QTPILOT_AUTH_TOKEN_FILE"));
        policy.certificateFile = environment.value(QStringLiteral("QTPILOT_TLS_CERT_FILE"));
        policy.privateKeyFile = environment.value(QStringLiteral("QTPILOT_TLS_KEY_FILE"));
        if (policy.certificateFile.isEmpty() != policy.privateKeyFile.isEmpty()) {
          return std::unexpected(
              QStringLiteral("TLS requires both certificate and private key files"));
        }
        if ((profile == OperatingProfile::TrustedNetwork || profile == OperatingProfile::Remote) &&
            (!policy.authenticationRequired() || !policy.tlsEnabled())) {
          return std::unexpected(
              QStringLiteral("Operating profile requires authentication and TLS"));
        }
        if (policy.authenticationRequired() && policy.exposure == NetworkExposure::Lan &&
            !policy.tlsEnabled()) {
          return std::unexpected(QStringLiteral("Authenticated LAN access requires TLS"));
        }
        return policy;
      });
}
}  // namespace qtPilot
