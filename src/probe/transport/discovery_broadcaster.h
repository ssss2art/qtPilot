// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <QElapsedTimer>
#include <QObject>

class QTimer;
class QUdpSocket;

namespace qtPilot {

/// @brief Broadcasts UDP discovery announcements so that MCP servers
/// can find running probes without knowing ports in advance.
///
/// Sends a JSON announce datagram to 255.255.255.255 every 5 seconds
/// and a goodbye datagram on shutdown.
///
/// Configuration via environment variables:
/// - QTPILOT_DISCOVERY_PORT: UDP port to broadcast on (default: 9221)
class DiscoveryBroadcaster : public QObject {
  Q_OBJECT

 public:
  explicit DiscoveryBroadcaster(quint16 wsPort, const QString& mode, QObject* parent = nullptr);
  ~DiscoveryBroadcaster() override;

  bool start();
  void stop();
  bool isRunning() const;

 private:
  void sendAnnounce();
  void sendGoodbye();
  QByteArray buildAnnouncePayload() const;
  QByteArray buildGoodbyePayload() const;

  QUdpSocket* socket_;
  QTimer* timer_;
  QElapsedTimer uptime_;
  quint16 discoveryPort_;
  quint16 wsPort_;
  QString mode_;
  bool running_;
};

}  // namespace qtPilot
