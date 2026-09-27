// Copyright (c) 2026 qtPilot Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "transport/network_policy.h"

#include <QByteArray>
#ifndef QT_NO_SSL
#include <QSslConfiguration>
#endif

namespace qtPilot {
struct TransportCredentials {
  QByteArray tokenDigest;
  bool tls = false;
#ifndef QT_NO_SSL
  QSslConfiguration ssl;
#endif
  bool accepts(const QByteArray& authorization) const;
};
std::expected<TransportCredentials, QString> loadTransportCredentials(const NetworkPolicy& policy);
}  // namespace qtPilot
