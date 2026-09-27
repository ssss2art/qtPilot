// Copyright (c) 2026 qtPilot Contributors
// SPDX-License-Identifier: MIT
#include "transport/admission_server.h"

#include <chrono>

#include <QTcpSocket>
#include <QTimer>
#include <QWebSocketServer>
#ifndef QT_NO_SSL
#include <QSslSocket>
#endif

namespace qtPilot {
namespace {
constexpr int kMaxPendingAdmissions = 16;
constexpr auto kAdmissionTimeout = std::chrono::seconds(5);
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
  const auto pending = m_pending;
  for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
    discard(it.key(), it->deadline);
  }
}

void AdmissionServer::discard(QTcpSocket* socket, QTimer* deadline) {
  // QWebSocket may disconnect callbacks on its underlying TCP socket during
  // destruction. The independent deadline and QPointer remain reliable.
  const auto found = m_pending.constFind(socket);
  if (found == m_pending.cend() || found->deadline != deadline) {
    return;  // A stale callback must not discard a new socket at a reused address.
  }
  const auto pending = m_pending.take(socket);
  if (pending.deadline) {
    pending.deadline->stop();
    pending.deadline->deleteLater();
  }
  if (pending.socket) {
    pending.socket->abort();
    pending.socket->deleteLater();
  }
}

int AdmissionServer::pendingCount() const {
  return static_cast<int>(m_pending.size());
}

void AdmissionServer::finish(const QHostAddress& peer, quint16 port) {
  for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
    if (it->socket && it->socket->peerAddress().isEqual(peer, QHostAddress::TolerantConversion) &&
        it->socket->peerPort() == port) {
      it->deadline->stop();
      it->deadline->deleteLater();
      it->socket->setReadBufferSize(0);
      m_pending.erase(it);
      return;
    }
  }
}

void AdmissionServer::releaseRejection(const QHostAddress& peer, quint16 port) {
  for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
    if (it->socket && it->socket->peerAddress().isEqual(peer, QHostAddress::TolerantConversion) &&
        it->socket->peerPort() == port) {
      QTcpSocket* socket = it->socket;
      QTimer* deadline = it->deadline;
      m_pending.erase(it);
      if (deadline) {
        deadline->stop();
        deadline->disconnect(this);
        connect(deadline, &QTimer::timeout, this, [socket, deadline] {
          if (socket) {
            socket->abort();
            socket->deleteLater();
          }
          deadline->deleteLater();
        });
        deadline->start(std::chrono::milliseconds(200));
      }
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
  auto* deadline = new QTimer(this);
  deadline->setSingleShot(true);
  m_pending.insert(socket, PendingAdmission{socket, deadline});
  connect(deadline, &QTimer::timeout, this, [this, socket, deadline] {
    discard(socket, deadline);
    deadline->deleteLater();
  });
  connect(socket, &QTcpSocket::disconnected, this,
          [this, socket, deadline] { discard(socket, deadline); });
  deadline->start(kAdmissionTimeout);
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
