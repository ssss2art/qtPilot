// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "transport/websocket_server.h"

#include "transport/bind_policy.h"
#include "transport/jsonrpc_handler.h"
#include "transport/notification_queue.h"

#include <QDebug>
#include <QWebSocket>
#include <QWebSocketServer>

namespace qtPilot {

WebSocketServer::WebSocketServer(quint16 port, QObject* parent)
    : QObject(parent),
      m_server(
          new QWebSocketServer(QStringLiteral("qtPilot"), QWebSocketServer::NonSecureMode, this)),
      m_activeClient(nullptr),
      m_rpcHandler(new JsonRpcHandler(this)),
      m_port(port) {
  connect(m_server, &QWebSocketServer::newConnection, this, &WebSocketServer::onNewConnection);
}

WebSocketServer::~WebSocketServer() {
  stop();
}

bool WebSocketServer::start() {
  if (m_server->isListening()) {
    qWarning() << "[qtPilot] WebSocket server already listening on port" << m_port;
    return true;
  }

  // All interfaces by default -- reaching instrumented apps on other hosts is a
  // requirement, and discovery is broadcast-based, so a loopback default would
  // be an outage rather than a hardening. QTPILOT_BIND_ADDRESS=loopback narrows
  // it for single-machine work. See bind_policy.h for why the exposure is not
  // the mitigation here; authentication is (R7, not yet implemented).
  const QHostAddress bindAddress = listenAddress();
  if (!m_server->listen(bindAddress, m_port)) {
    QString error = m_server->errorString();
    qCritical() << "[qtPilot] Failed to start WebSocket server on" << bindAddress.toString() << ":"
                << m_port << ":" << error;
    emit errorOccurred(error);
    return false;
  }

  // When port 0 was requested, read back the OS-assigned ephemeral port
  if (m_port == 0) {
    m_port = m_server->serverPort();
  }

  // Print startup message to stderr as specified in CONTEXT.md. The host is the
  // address actually bound, not a hardcoded one -- the previous message claimed
  // 0.0.0.0 unconditionally, which is exactly the detail an operator needs to be
  // told truthfully.
  fprintf(stderr, "qtPilot listening on ws://%s:%u\n", bindAddress.toString().toUtf8().constData(),
          static_cast<unsigned>(m_port));
  // Stated once at startup rather than shouted. Reaching instrumented apps on
  // other hosts is the normal case, so this is not a misconfiguration to warn
  // about -- but the probe invokes arbitrary slots and authenticates nobody, and
  // an operator deciding where to run it should not have to read the source to
  // learn that. Fact plus remedy, one line.
  if (configuredExposure() == NetworkExposure::Lan) {
    fprintf(stderr,
            "[qtPilot] Reachable from any host on this network, with no "
            "authentication; any of them can invoke methods in this process. "
            "Set QTPILOT_BIND_ADDRESS=loopback to restrict to this machine.\n");
  }
  fflush(stderr);

  return true;
}

void WebSocketServer::stop() {
  // Close active client connection if any
  if (m_activeClient) {
    delete m_notificationQueue;
    m_notificationQueue = nullptr;
    m_activeClient->close();
    m_activeClient->deleteLater();
    m_activeClient = nullptr;
  }

  // Close the server
  if (m_server->isListening()) {
    m_server->close();
  }
}

bool WebSocketServer::isListening() const {
  return m_server && m_server->isListening();
}

quint16 WebSocketServer::port() const {
  return m_port;
}

QHostAddress WebSocketServer::serverAddress() const {
  return m_server ? m_server->serverAddress() : QHostAddress();
}

bool WebSocketServer::hasActiveClient() const {
  return m_activeClient != nullptr;
}

JsonRpcHandler* WebSocketServer::rpcHandler() const {
  return m_rpcHandler;
}

bool WebSocketServer::sendMessage(const QString& message) {
  if (!m_activeClient) {
    return false;
  }
  qint64 sent = m_activeClient->sendTextMessage(message);
  if (sent == 0) {
    qWarning() << "[qtPilot] sendTextMessage returned 0, message may not have been sent";
  }
  return sent > 0;
}

void WebSocketServer::sendNotification(const QString& message) {
  if (m_notificationQueue) {
    m_notificationQueue->enqueue(message);
  }
}

NotificationQueue* WebSocketServer::notificationQueue() const {
  return m_notificationQueue;
}

void WebSocketServer::onNewConnection() {
  QWebSocket* socket = m_server->nextPendingConnection();
  if (!socket) {
    return;
  }

  // Single-client semantics: reject if we already have a client
  if (m_activeClient) {
    qWarning() << "[qtPilot] Rejecting connection from" << socket->peerAddress().toString()
               << "- another client is already connected";
    socket->close(QWebSocketProtocol::CloseCodePolicyViolated,
                  QStringLiteral("Another client is already connected"));
    socket->deleteLater();
    return;
  }

  // Accept this client
  m_activeClient = socket;

  // Create notification queue for this client
  m_notificationQueue = new NotificationQueue(socket, 10000, 50, this);

  qInfo() << "[qtPilot] Client connected from" << socket->peerAddress().toString() << ":"
          << socket->peerPort();

  connect(socket, &QWebSocket::textMessageReceived, this, &WebSocketServer::onTextMessage);
  connect(socket, &QWebSocket::disconnected, this, &WebSocketServer::onClientDisconnected);

  emit clientConnected();
}

void WebSocketServer::onTextMessage(const QString& message) {
  if (!m_activeClient) {
    return;
  }

  qDebug() << "[qtPilot] Received:" << message;
  emit messageReceived(message);

  // Process message through JSON-RPC handler
  QString response = m_rpcHandler->HandleMessage(message);

  // Send response if not a notification (notifications return empty response)
  if (!response.isEmpty()) {
    qDebug() << "[qtPilot] Sending:" << response;
    m_activeClient->sendTextMessage(response);
  }
}

void WebSocketServer::onClientDisconnected() {
  if (m_activeClient) {
    qInfo() << "[qtPilot] Client disconnected";
    delete m_notificationQueue;
    m_notificationQueue = nullptr;
    m_activeClient->deleteLater();
    m_activeClient = nullptr;
    emit clientDisconnected();
  }
  // Server keeps listening for new connections - do NOT stop!
}

}  // namespace qtPilot
