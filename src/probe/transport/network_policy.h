// Copyright (c) 2026 qtPilot Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "transport/bind_policy.h"

#include <expected>

#include <QProcessEnvironment>
#include <QString>

namespace qtPilot {

enum class OperatingProfile { Legacy, Local, TrustedNetwork, Remote };

struct QTPILOT_EXPORT NetworkPolicy {
  OperatingProfile profile = OperatingProfile::Legacy;
  NetworkExposure exposure = NetworkExposure::Lan;
  bool discoveryEnabled = true;
  QString tokenFile;
  QString certificateFile;
  QString privateKeyFile;

  bool authenticationRequired() const { return !tokenFile.isEmpty(); }
  bool tlsEnabled() const { return !certificateFile.isEmpty(); }
};

QTPILOT_EXPORT std::expected<NetworkPolicy, QString> resolveNetworkPolicy(
    const QProcessEnvironment& environment);

}  // namespace qtPilot
