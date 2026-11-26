// Copyright (c) 2026 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <array>

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

namespace qtPilot {

/// Encode one argv argument for the Windows C runtime (not a cmd.exe command).
/// Quotes require 2*n+1 preceding backslashes; the closing delimiter requires 2*n.
inline QString quoteWindowsArgument(const QString& argument) {
  QString encoded = QStringLiteral("\"");
  decltype(argument.size()) backslashes = 0;
  for (decltype(argument.size()) index = 0; index < argument.size(); ++index) {
    const char16_t character = argument.at(index).unicode();
    if (character == u'\\') {
      ++backslashes;
      continue;
    }
    const bool isQuote = character == u'"';
    encoded += QString(backslashes * (isQuote ? 2 : 1), QLatin1Char('\\'));
    if (isQuote)
      encoded += QLatin1Char('\\');
    encoded += QChar(character);
    backslashes = 0;
  }
  encoded += QString(backslashes * 2, QLatin1Char('\\'));
  encoded += QLatin1Char('"');
  return encoded;
}

/// Environment variable names forwarded across UAC elevation.
inline constexpr std::array<const char*, 13> kElevatedEnvVarsToForward = {
    "PATH",
    "QT_PLUGIN_PATH",
    "QT_QPA_PLATFORM_PLUGIN_PATH",
    "QTPILOT_PORT",
    "QTPILOT_MODE",
    "QTPILOT_INJECT_CHILDREN",
    "QTPILOT_ENABLED",
    "QTPILOT_DISCOVERY_PORT",
    "QTPILOT_PROFILE",
    "QTPILOT_AUTH_TOKEN_FILE",
    "QTPILOT_TLS_CERT",
    "QTPILOT_TLS_KEY",
    "QTPILOT_TLS_CA",
};

/// Build the `/c ...` parameter string for an elevated `cmd.exe` launch.
/// Replaces `--run-as-admin` with `--elevated` in @p args, quotes @p executable
/// and every argument with quoteWindowsArgument(), and emits `set "VAR=val"`
/// prefixes for non-empty entries in @p envVars.
inline QString buildElevatedCmdParameters(const QString& executable, const QStringList& args,
                                          const QList<QPair<QString, QString>>& envVars) {
  QStringList newArgs = args;
  newArgs.removeAll(QStringLiteral("--run-as-admin"));
  newArgs.prepend(QStringLiteral("--elevated"));

  QStringList launcherParts;
  launcherParts.reserve(1 + newArgs.size());
  launcherParts.append(quoteWindowsArgument(executable));
  for (const QString& arg : newArgs) {
    launcherParts.append(quoteWindowsArgument(arg));
  }
  const QString launcherCmd = launcherParts.join(QLatin1Char(' '));

  QStringList envSetCmds;
  for (const auto& entry : envVars) {
    if (entry.first.isEmpty() || entry.second.isEmpty())
      continue;
    // Strip embedded double quotes so `set "VAR=val"` cannot break out of quotes.
    QString safeValue = entry.second;
    safeValue.remove(QLatin1Char('"'));
    envSetCmds.append(QStringLiteral("set \"%1=%2\"").arg(entry.first, safeValue));
  }

  if (envSetCmds.isEmpty()) {
    return QStringLiteral("/c \"%1\"").arg(launcherCmd);
  }
  return QStringLiteral("/c \"%1 && %2\"")
      .arg(envSetCmds.join(QStringLiteral(" && ")), launcherCmd);
}

}  // namespace qtPilot
