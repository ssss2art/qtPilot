// Copyright (c) 2024-2026 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "interaction/modifier_parser.h"

#include "api/error_codes.h"
#include "transport/jsonrpc_handler.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>

namespace qtPilot {

namespace {

/// The accepted spellings, mapped onto Qt's enum.
///
/// `cmd` and `ctrl` deliberately land on the same flag: Qt reports the macOS
/// Command key as Qt::ControlModifier, so an app's `Qt::ControlModifier` check
/// is what both names need to satisfy. `meta` reaches the physical Control key
/// on macOS and the Windows/Super key elsewhere, which is the same enum value
/// on both.
const QHash<QString, Qt::KeyboardModifier>& modifierNames() {
  static const QHash<QString, Qt::KeyboardModifier> kNames = {
      {QStringLiteral("ctrl"), Qt::ControlModifier},
      {QStringLiteral("control"), Qt::ControlModifier},
      {QStringLiteral("cmd"), Qt::ControlModifier},
      {QStringLiteral("command"), Qt::ControlModifier},
      {QStringLiteral("shift"), Qt::ShiftModifier},
      {QStringLiteral("alt"), Qt::AltModifier},
      {QStringLiteral("option"), Qt::AltModifier},
      {QStringLiteral("meta"), Qt::MetaModifier},
      {QStringLiteral("super"), Qt::MetaModifier},
      {QStringLiteral("win"), Qt::MetaModifier},
      {QStringLiteral("keypad"), Qt::KeypadModifier},
  };
  return kNames;
}

JsonRpcException makeModifierError(const QJsonValue& offending, const QString& methodName,
                                   const QString& reason) {
  QJsonArray accepted;
  const QStringList names = ModifierParser::acceptedNames();
  for (const QString& name : names) {
    accepted.append(name);
  }
  return JsonRpcException(JsonRpcError::kInvalidParams, reason,
                          QJsonObject{{QStringLiteral("method"), methodName},
                                      {QStringLiteral("modifiers"), offending},
                                      {QStringLiteral("accepted"), accepted}});
}

/// Fold one name onto the accumulating flags, returning an unexpected JsonRpcException if
/// unrecognised.
std::expected<void, JsonRpcException> applyNameExpected(const QString& rawName,
                                                        Qt::KeyboardModifiers& flags,
                                                        const QJsonValue& offending,
                                                        const QString& methodName) {
  const QString name = rawName.trimmed().toLower();
  if (name.isEmpty()) {
    return std::unexpected(makeModifierError(
        offending, methodName, QStringLiteral("Parameter 'modifiers' has an empty modifier name")));
  }
  const auto it = modifierNames().constFind(name);
  if (it == modifierNames().constEnd()) {
    return std::unexpected(makeModifierError(
        offending, methodName,
        QStringLiteral("Parameter 'modifiers' names an unknown modifier: '%1'").arg(rawName)));
  }
  flags |= it.value();
  return {};
}

}  // namespace

std::expected<Qt::KeyboardModifiers, JsonRpcException> ModifierParser::parseExpected(
    const QJsonValue& value, const QString& methodName) {
  if (value.isUndefined() || value.isNull()) {
    return Qt::NoModifier;
  }

  Qt::KeyboardModifiers flags = Qt::NoModifier;

  if (value.isString()) {
    const QString joined = value.toString().trimmed();
    if (joined.isEmpty()) {
      return Qt::NoModifier;
    }
    const QStringList parts = joined.split(QLatin1Char('+'), Qt::KeepEmptyParts);
    for (const QString& part : parts) {
      auto res = applyNameExpected(part, flags, value, methodName);
      if (!res) {
        return std::unexpected(res.error());
      }
    }
    return flags;
  }

  if (value.isArray()) {
    const QJsonArray names = value.toArray();
    for (const auto& entry : names) {
      if (!entry.isString()) {
        return std::unexpected(makeModifierError(
            value, methodName,
            QStringLiteral("Parameter 'modifiers' array must hold only strings")));
      }
      auto res = applyNameExpected(entry.toString(), flags, value, methodName);
      if (!res) {
        return std::unexpected(res.error());
      }
    }
    return flags;
  }

  return std::unexpected(makeModifierError(
      value, methodName,
      QStringLiteral("Parameter 'modifiers' must be a string or an array of strings")));
}

Qt::KeyboardModifiers ModifierParser::parse(const QJsonValue& value, const QString& methodName) {
  auto res = parseExpected(value, methodName);
  if (!res) {
    throw res.error();
  }
  return *res;
}

QStringList ModifierParser::acceptedNames() {
  QStringList names = modifierNames().keys();
  names.sort();
  return names;
}

}  // namespace qtPilot
