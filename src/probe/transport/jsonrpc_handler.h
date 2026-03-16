// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>
#include <stdexcept>
#include <unordered_map>

#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QString>

// Export macro for Windows DLL
#if defined(QTPILOT_PROBE_LIBRARY)
#if defined(_WIN32)
#define QTPILOT_EXPORT __declspec(dllexport)
#else
#define QTPILOT_EXPORT __attribute__((visibility("default")))
#endif
#else
#if defined(_WIN32)
#define QTPILOT_EXPORT __declspec(dllimport)
#else
#define QTPILOT_EXPORT
#endif
#endif

namespace qtPilot {

/// @brief JSON-RPC 2.0 message handler.
///
/// This class parses incoming JSON-RPC requests, dispatches them to
/// registered method handlers, and formats the responses.
class QTPILOT_EXPORT JsonRpcHandler : public QObject {
  Q_OBJECT

 public:
  /// @brief Method handler function type.
  /// Takes JSON params, returns JSON result or throws for errors.
  using MethodHandler = std::function<QString(const QString& params)>;

  /// @brief Construct a JSON-RPC handler.
  /// @param parent Parent QObject.
  explicit JsonRpcHandler(QObject* parent = nullptr);

  /// @brief Destructor.
  ~JsonRpcHandler() override = default;

  /// @brief Handle an incoming JSON-RPC message.
  /// @param message The raw JSON message string.
  /// @return The JSON response string, or empty for notifications.
  QString HandleMessage(const QString& message);

  /// @brief Register a method handler.
  /// @param method Method name.
  /// @param handler Handler function.
  void RegisterMethod(const QString& method, MethodHandler handler);

  /// @brief Unregister a method handler.
  /// @param method Method name.
  void UnregisterMethod(const QString& method);

 signals:
  /// @brief Emitted when a notification should be sent to clients.
  /// @param notification The JSON notification string.
  void NotificationReady(const QString& notification);

  /// @brief Emitted when a notification (request without id) is received.
  /// @param method The method name.
  /// @param params The params value.
  void NotificationReceived(const QString& method, const QJsonValue& params);

 private:
  /// @brief Create a JSON-RPC success response.
  /// @param id Request ID.
  /// @param result Result value as JSON string.
  /// @return Formatted JSON-RPC response.
  QString CreateSuccessResponse(const QString& id, const QString& result);

  /// @brief Create a JSON-RPC error response.
  /// @param id Request ID (can be null for parse errors).
  /// @param code Error code.
  /// @param message Error message.
  /// @return Formatted JSON-RPC error response.
  QString CreateErrorResponse(const QString& id, int code, const QString& message);

  /// @brief Create a JSON-RPC error response with data field.
  /// @param id Request ID.
  /// @param code Error code.
  /// @param message Error message.
  /// @param data Additional structured error data.
  /// @return Formatted JSON-RPC error response with data.
  QString CreateErrorResponse(const QString& id, int code, const QString& message,
                              const QJsonObject& data);

  /// @brief Register built-in methods (ping, getVersion, etc.).
  void RegisterBuiltinMethods();

  std::unordered_map<QString, MethodHandler> methods_;
};

// JSON-RPC 2.0 error codes
namespace JsonRpcError {
constexpr int kParseError = -32700;
constexpr int kInvalidRequest = -32600;
constexpr int kMethodNotFound = -32601;
constexpr int kInvalidParams = -32602;
constexpr int kInternalError = -32603;
// Server errors: -32000 to -32099
constexpr int kServerError = -32000;
}  // namespace JsonRpcError

/// @brief Structured JSON-RPC exception with error code and optional data.
///
/// Thrown by method handlers (e.g., NativeModeApi lambdas) to produce
/// structured error responses with specific error codes and data fields.
/// JsonRpcHandler::HandleMessage catches this before std::exception.
class JsonRpcException : public std::runtime_error {
 public:
  JsonRpcException(int code, const QString& message, const QJsonObject& data = QJsonObject())
      : std::runtime_error(message.toStdString()), m_code(code), m_message(message), m_data(data) {}

  int code() const { return m_code; }
  QString errorMessage() const { return m_message; }
  QJsonObject data() const { return m_data; }

 private:
  int m_code;
  QString m_message;
  QJsonObject m_data;
};

}  // namespace qtPilot
