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

[[noreturn]] void rejectModifiers(const QJsonValue& offending, const QString& methodName,
                                  const QString& reason) {
  QJsonArray accepted;
  const QStringList names = ModifierParser::acceptedNames();
  for (const QString& name : names) {
    accepted.append(name);
  }
  throw JsonRpcException(JsonRpcError::kInvalidParams, reason,
                         QJsonObject{{QStringLiteral("method"), methodName},
                                     {QStringLiteral("modifiers"), offending},
                                     {QStringLiteral("accepted"), accepted}});
}

/// Fold one name onto the accumulating flags, rejecting anything unrecognised.
void applyName(const QString& rawName, Qt::KeyboardModifiers& flags, const QJsonValue& offending,
               const QString& methodName) {
  const QString name = rawName.trimmed().toLower();
  if (name.isEmpty()) {
    rejectModifiers(offending, methodName,
                    QStringLiteral("Parameter 'modifiers' has an empty modifier name"));
  }
  const auto it = modifierNames().constFind(name);
  if (it == modifierNames().constEnd()) {
    rejectModifiers(
        offending, methodName,
        QStringLiteral("Parameter 'modifiers' names an unknown modifier: '%1'").arg(rawName));
  }
  flags |= it.value();
}

}  // namespace

Qt::KeyboardModifiers ModifierParser::parse(const QJsonValue& value, const QString& methodName) {
  if (value.isUndefined() || value.isNull()) {
    return Qt::NoModifier;
  }

  Qt::KeyboardModifiers flags = Qt::NoModifier;

  if (value.isString()) {
    const QString joined = value.toString().trimmed();
    if (joined.isEmpty()) {
      return Qt::NoModifier;
    }
    // Qt::SkipEmptyParts would quietly accept "ctrl++shift"; the caller almost
    // certainly meant something else, so the empty segment is reported instead.
    const QStringList parts = joined.split(QLatin1Char('+'), Qt::KeepEmptyParts);
    for (const QString& part : parts) {
      applyName(part, flags, value, methodName);
    }
    return flags;
  }

  if (value.isArray()) {
    const QJsonArray names = value.toArray();
    for (const auto& entry : names) {
      if (!entry.isString()) {
        rejectModifiers(value, methodName,
                        QStringLiteral("Parameter 'modifiers' array must hold only strings"));
      }
      applyName(entry.toString(), flags, value, methodName);
    }
    return flags;
  }

  rejectModifiers(value, methodName,
                  QStringLiteral("Parameter 'modifiers' must be a string or an array of strings"));
}

QStringList ModifierParser::acceptedNames() {
  QStringList names = modifierNames().keys();
  names.sort();
  return names;
}

}  // namespace qtPilot
