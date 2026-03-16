// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "key_name_mapper.h"

namespace qtPilot {

const QHash<QString, Qt::Key>& KeyNameMapper::keyMap() {
  // Static local - initialized once, thread-safe in C++11+
  static const QHash<QString, Qt::Key> map = {
      // Navigation
      {"return", Qt::Key_Return},
      {"enter", Qt::Key_Return},
      {"tab", Qt::Key_Tab},
      {"escape", Qt::Key_Escape},
      {"esc", Qt::Key_Escape},
      {"backspace", Qt::Key_Backspace},
      {"delete", Qt::Key_Delete},
      {"space", Qt::Key_Space},

      // Arrow keys (Chrome uses "ArrowUp", xdotool uses "Up")
      {"up", Qt::Key_Up},
      {"arrowup", Qt::Key_Up},
      {"down", Qt::Key_Down},
      {"arrowdown", Qt::Key_Down},
      {"left", Qt::Key_Left},
      {"arrowleft", Qt::Key_Left},
      {"right", Qt::Key_Right},
      {"arrowright", Qt::Key_Right},

      // Page navigation
      {"home", Qt::Key_Home},
      {"end", Qt::Key_End},
      {"page_up", Qt::Key_PageUp},
      {"pageup", Qt::Key_PageUp},
      {"page_down", Qt::Key_PageDown},
      {"pagedown", Qt::Key_PageDown},
      {"insert", Qt::Key_Insert},

      // Function keys
      {"f1", Qt::Key_F1},
      {"f2", Qt::Key_F2},
      {"f3", Qt::Key_F3},
      {"f4", Qt::Key_F4},
      {"f5", Qt::Key_F5},
      {"f6", Qt::Key_F6},
      {"f7", Qt::Key_F7},
      {"f8", Qt::Key_F8},
      {"f9", Qt::Key_F9},
      {"f10", Qt::Key_F10},
      {"f11", Qt::Key_F11},
      {"f12", Qt::Key_F12},

      // Modifiers (when used as standalone keys)
      {"shift_l", Qt::Key_Shift},
      {"shift", Qt::Key_Shift},
      {"control_l", Qt::Key_Control},
      {"control", Qt::Key_Control},
      {"ctrl", Qt::Key_Control},
      {"alt_l", Qt::Key_Alt},
      {"alt", Qt::Key_Alt},
      {"super_l", Qt::Key_Super_L},
      {"super", Qt::Key_Super_L},
      {"meta", Qt::Key_Meta},

      // Misc
      {"print", Qt::Key_Print},
      {"scroll_lock", Qt::Key_ScrollLock},
      {"pause", Qt::Key_Pause},
      {"caps_lock", Qt::Key_CapsLock},
      {"num_lock", Qt::Key_NumLock},
      {"menu", Qt::Key_Menu},
  };
  return map;
}

const QHash<QString, Qt::KeyboardModifier>& KeyNameMapper::modifierMap() {
  static const QHash<QString, Qt::KeyboardModifier> map = {
      {"ctrl", Qt::ControlModifier},      {"control", Qt::ControlModifier},
      {"control_l", Qt::ControlModifier}, {"shift", Qt::ShiftModifier},
      {"shift_l", Qt::ShiftModifier},     {"alt", Qt::AltModifier},
      {"alt_l", Qt::AltModifier},         {"super", Qt::MetaModifier},
      {"super_l", Qt::MetaModifier},      {"meta", Qt::MetaModifier},
  };
  return map;
}

Qt::Key KeyNameMapper::resolve(const QString& name) {
  const auto& map = keyMap();
  const QString lower = name.toLower();

  // Check explicit table first
  auto it = map.find(lower);
  if (it != map.end()) {
    return it.value();
  }

  // Single printable character: map to Qt::Key directly
  // Qt::Key values for A-Z are 0x41-0x5A (same as ASCII uppercase)
  // Qt::Key values for 0-9 are 0x30-0x39 (same as ASCII)
  if (name.length() == 1) {
    QChar ch = name[0];
    if (ch >= QLatin1Char('a') && ch <= QLatin1Char('z')) {
      return static_cast<Qt::Key>(ch.toUpper().unicode());
    }
    if (ch >= QLatin1Char('A') && ch <= QLatin1Char('Z')) {
      return static_cast<Qt::Key>(ch.unicode());
    }
    if (ch >= QLatin1Char('0') && ch <= QLatin1Char('9')) {
      return static_cast<Qt::Key>(ch.unicode());
    }
    // Common punctuation
    if (ch == QLatin1Char('-'))
      return Qt::Key_Minus;
    if (ch == QLatin1Char('='))
      return Qt::Key_Equal;
    if (ch == QLatin1Char('['))
      return Qt::Key_BracketLeft;
    if (ch == QLatin1Char(']'))
      return Qt::Key_BracketRight;
    if (ch == QLatin1Char(';'))
      return Qt::Key_Semicolon;
    if (ch == QLatin1Char('\''))
      return Qt::Key_Apostrophe;
    if (ch == QLatin1Char(','))
      return Qt::Key_Comma;
    if (ch == QLatin1Char('.'))
      return Qt::Key_Period;
    if (ch == QLatin1Char('/'))
      return Qt::Key_Slash;
    if (ch == QLatin1Char('\\'))
      return Qt::Key_Backslash;
    if (ch == QLatin1Char('`'))
      return Qt::Key_QuoteLeft;
  }

  return Qt::Key_unknown;
}

KeyCombo KeyNameMapper::parseKeyCombo(const QString& combo) {
  KeyCombo result;
  result.modifiers = Qt::NoModifier;

  // Split on "+"
  const QStringList parts = combo.split(QLatin1Char('+'));
  if (parts.isEmpty()) {
    result.key = Qt::Key_unknown;
    return result;
  }

  // Last token is the key, preceding tokens are modifiers
  const auto& modMap = modifierMap();

  for (int i = 0; i < parts.size() - 1; ++i) {
    const QString mod = parts[i].trimmed().toLower();
    auto it = modMap.find(mod);
    if (it != modMap.end()) {
      result.modifiers |= it.value();
    }
  }

  // Resolve the key (last token)
  const QString keyName = parts.last().trimmed();
  result.key = resolve(keyName);

  return result;
}

}  // namespace qtPilot
