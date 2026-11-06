// Copyright (c) 2026 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <QString>

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

}  // namespace qtPilot
