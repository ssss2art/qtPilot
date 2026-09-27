// Copyright (c) 2026 qtPilot Contributors
// SPDX-License-Identifier: MIT
#include "transport/admission_server.h"

#include <QTcpSocket>
#include <QTimer>
#include <QWebSocketServer>
#ifndef QT_NO_SSL
#include <QSslSocket>
#endif

namespace qtPilot {
namespace {
constexpr int kMaxPendingAdmissions = 16;
constexpr int kAdmissionTimeoutMs = 5000;
constexpr qint64 kUpgradeReadBuffer = 16 * 1024;
}  // namespace

AdmissionServer::AdmissionServer(QWebSocketServer* websocket,
                                 const TransportCredentials& credentials, QObject* parent)
    : QTcpServer(parent), m_websocket(websocket), m_credentials(credentials) {}

AdmissionServer::~AdmissionServer() {
  shutdown();
}

void AdmissionServer::shutdown() {
  close();
  const auto sockets = m_pending.keys();
  for (auto* socket : sockets) {
    socket->abort();
    socket->deleteLater();
  }
  m_pending.clear();
}

int AdmissionServer::pendingCount() const {
  return static_cast<int>(m_pending.size());
}

void AdmissionServer::finish(const QHostAddress& peer, quint16 port) {
  for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
    if (it.key()->peerAddress() == peer && it.key()->peerPort() == port) {
      it.value()->stop();
      it.value()->deleteLater();
      it.key()->setReadBufferSize(0);
      m_pending.erase(it);
      return;
    }
  }
}

void AdmissionServer::incomingConnection(qintptr descriptor) {
  if (m_pending.size() >= kMaxPendingAdmissions) {
    QTcpSocket rejected;
    if (rejected.setSocketDescriptor(descriptor)) {
      rejected.abort();
    }
    return;
  }
  QTcpSocket* socket = nullptr;
#ifndef QT_NO_SSL
  if (m_credentials.tls) {
    auto* secure = new QSslSocket(this);
    secure->setSslConfiguration(m_credentials.ssl);
    connect(secure, &QSslSocket::encrypted, this, [this, secure] {
      if (m_pending.contains(secure)) {
        m_websocket->handleConnection(secure);
      }
    });
    socket = secure;
  } else
#endif
  {
    socket = new QTcpSocket(this);
  }
  if (!socket->setSocketDescriptor(descriptor)) {
    socket->deleteLater();
    return;
  }
  socket->setReadBufferSize(kUpgradeReadBuffer);
  auto* deadline = new QTimer(socket);
  deadline->setSingleShot(true);
  m_pending.insert(socket, deadline);
  connect(deadline, &QTimer::timeout, socket, [socket] { socket->abort(); });
  connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
    m_pending.remove(socket);
    socket->deleteLater();
  });
  connect(socket, &QObject::destroyed, this, [this, socket] { m_pending.remove(socket); });
  deadline->start(kAdmissionTimeoutMs);
#ifndef QT_NO_SSL
  if (m_credentials.tls) {
    static_cast<QSslSocket*>(socket)->startServerEncryption();
  } else
#endif
  {
    m_websocket->handleConnection(socket);
  }
}
}  // namespace qtPilot
