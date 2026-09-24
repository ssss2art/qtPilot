// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "transport/websocket_server.h"

#include "transport/bind_policy.h"
#include "transport/jsonrpc_handler.h"
#include "transport/notification_queue.h"

#include <QDebug>
#include <QPointer>
#include <QScopeGuard>
#include <QUrl>
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
    qCritical() << "[qtPilot] Failed to start WebSocket server on"
                << QStringLiteral("%1:%2: %3").arg(bindAddress.toString()).arg(m_port).arg(error);
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
  // Detach the client state BEFORE anything that can re-enter.
  //
  // QWebSocket::close() may emit disconnected() synchronously -- whether it does
  // depends on the socket's state and on the Qt version. That re-enters
  // onClientDisconnected(), which nulls m_activeClient; the old code then
  // continued into `m_activeClient->deleteLater()` on a null pointer. It
  // SEGFAULTed on Qt 6.8/6.9 and survived on 5.15/6.5/6.10/6.11, which is
  // exactly what a timing-dependent re-entrancy hole looks like.
  //
  // Taking a local reference and clearing the members first makes the order
  // independent of when Qt chooses to emit: a re-entrant call sees no client and
  // does nothing, and this function still owns the pointer it is finishing with.
  if (QWebSocket* client = takeActiveClient()) {
    client->close();
    client->deleteLater();
  }

  // Close the server
  if (m_server->isListening()) {
    m_server->close();
  }
}

QWebSocket* WebSocketServer::takeActiveClient() {
  QWebSocket* client = m_activeClient;
  if (client == nullptr) {
    return nullptr;
  }

  // Cleared first so a re-entrant stop()/onClientDisconnected() is a no-op.
  m_activeClient = nullptr;

  // The queue holds a raw pointer to the socket and is owned by this server, so
  // it goes before the socket does.
  delete m_notificationQueue;
  m_notificationQueue = nullptr;

  // No further signals from a socket whose bookkeeping is already gone. Safe to
  // call while one of those signals is being emitted.
  client->disconnect(this);

  return client;
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

  // Defend against Cross-Site WebSocket Hijacking (CSWSH).
  // Non-browser clients (qtpilot CLI, MCP server, LAN automation, Python test runners)
  // do not send an Origin header (socket->origin() is empty).
  // Web browsers always send an Origin header on WebSocket handshakes.
  // Allow empty origin, localhost, loopback, file://, and vscode-webview.
  QString origin = socket->origin();
  if (!origin.isEmpty()) {
    QUrl originUrl(origin);
    QString host = originUrl.host();
    bool trusted =
        (host == QStringLiteral("localhost") || host == QStringLiteral("127.0.0.1") ||
         host == QStringLiteral("::1") || origin.startsWith(QStringLiteral("vscode-webview://")) ||
         origin.startsWith(QStringLiteral("file://")));
    if (!trusted) {
      qWarning() << "[qtPilot] Rejecting connection with untrusted browser origin:" << origin;
      socket->close(QWebSocketProtocol::CloseCodePolicyViolated,
                    QStringLiteral("Cross-Site WebSocket Hijacking rejected"));
      socket->deleteLater();
      return;
    }
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

bool WebSocketServer::inFrameDispatch() const {
  return m_frameDispatchDepth > 0;
}

void WebSocketServer::onTextMessage(const QString& message) {
  if (!m_activeClient) {
    return;
  }
  ++m_frameDispatchDepth;
  const auto leaveDispatch = qScopeGuard([this] { --m_frameDispatchDepth; });

  qDebug() << "[qtPilot] Received:" << message;
  emit messageReceived(message);

  // Handle the request from the event loop, not from here. This slot runs inside
  // QtWebSockets' frame processing, which delivers every buffered frame in one
  // non-reentrant call. A handler that turns the event loop -- qt.sync and
  // sendKeys do, and so does any slot that opens a modal dialog -- would let the
  // socket deliver again with that call still on the stack. Posted events are
  // delivered in order, so requests are still handled, and answered, in order.
  QPointer<QWebSocket> client(m_activeClient);
  QMetaObject::invokeMethod(
      this, [this, client, message]() { handleRequest(client, message); }, Qt::QueuedConnection);
}

void WebSocketServer::handleRequest(const QPointer<QWebSocket>& client, const QString& message) {
  QString response = m_rpcHandler->HandleMessage(message);

  // Send response if not a notification (notifications return empty response)
  if (response.isEmpty()) {
    return;
  }

  // Answer only the client that asked, and only if it is still here. It may
  // have left before the request was handled -- or while it was, since
  // HandleMessage() can turn the event loop -- and a different client may have
  // taken its place.
  if (client.isNull() || client != m_activeClient) {
    qWarning() << "[qtPilot] Client left while its request was being handled; "
                  "dropping the reply";
    return;
  }

  qDebug() << "[qtPilot] Sending:" << response;
  client->sendTextMessage(response);
}

void WebSocketServer::onClientDisconnected() {
  QWebSocket* client = takeActiveClient();
  if (client == nullptr) {
    return;
  }

  qInfo() << "[qtPilot] Client disconnected";
  client->deleteLater();

  // Emitted last, and after the state is already consistent: a handler is free
  // to call stop() or accept a new client without observing a half-torn-down
  // server.
  emit clientDisconnected();

  // Server keeps listening for new connections - do NOT stop!
}

}  // namespace qtPilot
