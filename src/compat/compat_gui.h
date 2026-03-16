// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <QKeySequence>
#include <QMouseEvent>
#include <QtGlobal>

namespace qtPilot {
namespace compat {

/// Returns the local position of a mouse event as QPoint.
/// Qt6: Uses position().toPoint() (pos() is deprecated in Qt 6.6+).
/// Qt5: Uses pos().
inline QPoint mousePos(const QMouseEvent* event) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  return event->position().toPoint();
#else
  return event->pos();
#endif
}

/// Returns the global (screen) position of a mouse event as QPoint.
/// Qt6: Uses globalPosition().toPoint() (globalPos() is deprecated in Qt 6.6+).
/// Qt5: Uses globalPos().
inline QPoint mouseGlobalPos(const QMouseEvent* event) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  return event->globalPosition().toPoint();
#else
  return event->globalPos();
#endif
}

/// Extracts the key and modifiers from a QKeySequence at the given index.
/// Qt6: Uses QKeyCombination::key() and QKeyCombination::keyboardModifiers().
/// Qt5: Uses bitmask extraction on the integer key code.
inline void extractKeyCombination(const QKeySequence& seq, int index, Qt::Key& key,
                                  Qt::KeyboardModifiers& mods) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  key = seq[index].key();
  mods = seq[index].keyboardModifiers();
#else
  key = static_cast<Qt::Key>(seq[index] & ~Qt::KeyboardModifierMask);
  mods = Qt::KeyboardModifiers(seq[index] & Qt::KeyboardModifierMask);
#endif
}

}  // namespace compat
}  // namespace qtPilot
