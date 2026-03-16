// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "core/probe.h"  // For QTPILOT_EXPORT

#include <QHash>
#include <QString>
#include <Qt>

namespace qtPilot {

/// @brief Result of parsing a key combination string.
struct KeyCombo {
  Qt::Key key = Qt::Key_unknown;
  Qt::KeyboardModifiers modifiers = Qt::NoModifier;
};

/// @brief Maps Chrome/xdotool key names to Qt::Key enums.
///
/// Provides case-insensitive lookup of key names used by Anthropic's
/// Computer Use tool (Chrome-style and xdotool-style names) and translates
/// them to the corresponding Qt::Key values.
///
/// Also parses modifier combo strings like "ctrl+shift+s" into a KeyCombo
/// containing the base key and combined modifiers.
///
/// Usage:
/// @code
///   Qt::Key key = KeyNameMapper::resolve("ArrowUp");  // Qt::Key_Up
///   KeyCombo combo = KeyNameMapper::parseKeyCombo("ctrl+shift+s");
///   // combo.key == Qt::Key_S, combo.modifiers == Ctrl|Shift
/// @endcode
class QTPILOT_EXPORT KeyNameMapper {
 public:
  /// @brief Resolve a single key name to Qt::Key.
  /// @param name Chrome/xdotool key name (case-insensitive)
  /// @return Qt::Key value, or Qt::Key_unknown if not found
  static Qt::Key resolve(const QString& name);

  /// @brief Parse a key combination string into key + modifiers.
  /// @param combo Combo string like "ctrl+shift+s", "F5", "Return"
  /// @return KeyCombo with resolved key and combined modifiers
  ///
  /// Splits on "+". Last token is the key, preceding tokens are modifiers.
  /// Single character keys (a-z, 0-9) map directly to Qt::Key equivalents.
  static KeyCombo parseKeyCombo(const QString& combo);

 private:
  /// @brief Get the static key name -> Qt::Key lookup table.
  static const QHash<QString, Qt::Key>& keyMap();

  /// @brief Get the static modifier name -> Qt::KeyboardModifier lookup table.
  static const QHash<QString, Qt::KeyboardModifier>& modifierMap();
};

}  // namespace qtPilot
