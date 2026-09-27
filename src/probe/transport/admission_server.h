// Copyright (c) 2026 qtPilot Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "transport/transport_credentials.h"

#include <QHash>
#include <QTcpServer>

class QTcpSocket;
class QTimer;
class QWebSocketServer;

namespace qtPilot {
/// Owns bounded TCP/TLS admissions and delegates HTTP upgrade parsing to Qt.
class AdmissionServer : public QTcpServer {
 public:
  AdmissionServer(QWebSocketServer* websocket, const TransportCredentials& credentials,
                  QObject* parent);
  ~AdmissionServer() override;
  void shutdown();
  int pendingCount() const;
  void finish(const QHostAddress& peer, quint16 port);

 protected:
  void incomingConnection(qintptr descriptor) override;

 private:
  QWebSocketServer* m_websocket;
  TransportCredentials m_credentials;
  QHash<QTcpSocket*, QTimer*> m_pending;
};
}  // namespace qtPilot
