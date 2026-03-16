// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "api/response_envelope.h"

#include <QJsonDocument>

namespace qtPilot {

QJsonObject ResponseEnvelope::wrap(const QJsonValue& result, const QString& objectId) {
  QJsonObject meta;
  meta[QStringLiteral("timestamp")] = QDateTime::currentMSecsSinceEpoch();

  if (!objectId.isEmpty()) {
    meta[QStringLiteral("objectId")] = objectId;
  }

  QJsonObject envelope;
  envelope[QStringLiteral("result")] = result;
  envelope[QStringLiteral("meta")] = meta;
  return envelope;
}

QJsonObject ResponseEnvelope::wrap(const QJsonValue& result, const QJsonObject& extraMeta) {
  QJsonObject meta;
  meta[QStringLiteral("timestamp")] = QDateTime::currentMSecsSinceEpoch();

  // Merge extra meta fields
  for (auto it = extraMeta.constBegin(); it != extraMeta.constEnd(); ++it) {
    meta[it.key()] = it.value();
  }

  QJsonObject envelope;
  envelope[QStringLiteral("result")] = result;
  envelope[QStringLiteral("meta")] = meta;
  return envelope;
}

QString ResponseEnvelope::createError(int code, const QString& message, const QJsonObject& data) {
  QJsonObject error;
  error[QStringLiteral("code")] = code;
  error[QStringLiteral("message")] = message;

  if (!data.isEmpty()) {
    error[QStringLiteral("data")] = data;
  }

  return QString::fromUtf8(QJsonDocument(error).toJson(QJsonDocument::Compact));
}

QString ResponseEnvelope::createValidationError(const QString& method, const QString& missingParam,
                                                const QJsonObject& expectedSchema) {
  QJsonObject data;
  data[QStringLiteral("method")] = method;
  data[QStringLiteral("missingParam")] = missingParam;
  if (!expectedSchema.isEmpty()) {
    data[QStringLiteral("expectedSchema")] = expectedSchema;
  }

  return createError(JsonRpcError::kInvalidParams,
                     QStringLiteral("Missing required parameter: %1").arg(missingParam), data);
}

}  // namespace qtPilot
