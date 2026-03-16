// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "transport/jsonrpc_handler.h"  // For QTPILOT_EXPORT

#include <QDateTime>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

namespace qtPilot {

/// @brief Uniform response envelope wrapper for Native Mode API methods.
///
/// All qt.* methods return their results wrapped in a standard envelope:
/// { "result": ..., "meta": { "timestamp": epoch_ms, ... } }
///
/// This ensures consistent response formatting across all API methods.
class QTPILOT_EXPORT ResponseEnvelope {
 public:
  /// @brief Wrap a result value in the standard envelope.
  /// @param result The result value (any JSON type).
  /// @param objectId Optional object ID to include in meta.
  /// @return JSON object with {result, meta{timestamp, objectId?}}.
  static QJsonObject wrap(const QJsonValue& result, const QString& objectId = QString());

  /// @brief Wrap a result value with extra metadata fields.
  /// @param result The result value (any JSON type).
  /// @param extraMeta Additional fields to merge into the meta object.
  /// @return JSON object with {result, meta{timestamp, ...extraMeta}}.
  static QJsonObject wrap(const QJsonValue& result, const QJsonObject& extraMeta);

  /// @brief Create a JSON-RPC error object string.
  /// @param code Error code (use ErrorCode constants or JsonRpcError codes).
  /// @param message Human-readable error message.
  /// @param data Optional additional error data.
  /// @return JSON string: {"code":N,"message":"...","data":{...}}.
  static QString createError(int code, const QString& message,
                             const QJsonObject& data = QJsonObject());

  /// @brief Create a validation error for missing/invalid parameters.
  /// @param method The method name that was called.
  /// @param missingParam Name of the missing or invalid parameter.
  /// @param expectedSchema JSON object describing the expected parameter schema.
  /// @return JSON string for a -32602 (Invalid params) error.
  static QString createValidationError(const QString& method, const QString& missingParam,
                                       const QJsonObject& expectedSchema);
};

}  // namespace qtPilot
