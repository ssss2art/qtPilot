// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <QHostAddress>
#include <QObject>
#include <QString>

class QWebSocketServer;
class QWebSocket;

#include "core/qtpilot_export.h"

namespace qtPilot {

class JsonRpcHandler;
class NotificationQueue;

/// @brief WebSocket server for JSON-RPC communication.
///
/// This server accepts exactly one client at a time (single-client semantics).
/// When a client is already connected, new connection attempts are rejected
/// with a policy violation close code. The server continues listening after
/// a client disconnects, ready for reconnection.
///
/// Messages received from the client are routed to a JsonRpcHandler for
/// processing, and responses are sent back to the client.
class QTPILOT_EXPORT WebSocketServer : public QObject {
  Q_OBJECT

 public:
  /// @brief Construct a WebSocket server.
  /// @param port Port to listen on.
  /// @param parent Parent QObject.
  explicit WebSocketServer(quint16 port, QObject* parent = nullptr);

  /// @brief Destructor.
  ~WebSocketServer() override;

  /// @brief Start listening for connections.
  /// @return true if server started successfully.
  bool start();

  /// @brief Stop listening and disconnect any active client.
  void stop();

  /// @brief Check if the server is listening.
  /// @return true if listening for connections.
  bool isListening() const;

  /// @brief Get the configured port.
  /// @return The port number.
  quint16 port() const;

  /// @brief The address the server is actually listening on.
  ///
  /// Exposed so the exposure policy is observable rather than inferred: the
  /// difference between a loopback bind and an all-interfaces bind is a security
  /// boundary, and a test that cannot read it back can only assert that some
  /// code path was taken.
  /// @return The bound address, or QHostAddress::Null when not listening.
  QHostAddress serverAddress() const;

  /// @brief Check if a client is currently connected.
  /// @return true if a client is connected.
  bool hasActiveClient() const;

  /// @brief Get the JSON-RPC handler.
  /// @return Pointer to the handler.
  JsonRpcHandler* rpcHandler() const;

  /// @brief Send a message to the connected client.
  /// @param message The message to send.
  /// @return true if sent successfully, false if no client connected.
  bool sendMessage(const QString& message);

  /// @brief Send a notification through the bounded queue.
  /// @param message The JSON notification string.
  void sendNotification(const QString& message);

  /// @brief Get the notification queue (for diagnostics).
  /// @return Pointer to the notification queue, or nullptr if no client.
  NotificationQueue* notificationQueue() const;

 signals:
  /// @brief Emitted when a client connects.
  void clientConnected();

  /// @brief Emitted when a client disconnects.
  void clientDisconnected();

  /// @brief Emitted when a message is received from the client.
  /// @param message The raw message string.
  void messageReceived(const QString& message);

  /// @brief Emitted when an error occurs.
  /// @param error Error description.
  void errorOccurred(const QString& error);

 private slots:
  /// @brief Handle new connection attempts.
  void onNewConnection();

  /// @brief Handle incoming text messages.
  /// @param message The message text.
  void onTextMessage(const QString& message);

  /// @brief Handle client disconnection.
  void onClientDisconnected();

 private:
  QWebSocketServer* m_server = nullptr;
  QWebSocket* m_activeClient = nullptr;
  JsonRpcHandler* m_rpcHandler = nullptr;
  quint16 m_port;
  NotificationQueue* m_notificationQueue = nullptr;
};

}  // namespace qtPilot
