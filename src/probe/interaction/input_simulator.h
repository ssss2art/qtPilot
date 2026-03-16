// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "core/probe.h"  // For QTPILOT_EXPORT

#include <QPoint>
#include <QString>
#include <QWidget>

namespace qtPilot {

/// @brief Mouse and keyboard input simulation using QTest functions.
///
/// Provides reliable input simulation for UI testing and automation.
/// Uses Qt's QTest module internally for cross-platform compatibility.
///
/// All mouse positions are widget-local coordinates. If no position is
/// specified, the widget center is used.
///
/// Usage:
/// @code
///   // Click a button
///   InputSimulator::mouseClick(button);
///
///   // Type text into a line edit
///   InputSimulator::sendText(lineEdit, "Hello World");
///
///   // Send key combination
///   InputSimulator::sendKeySequence(widget, "Ctrl+S");
/// @endcode
class QTPILOT_EXPORT InputSimulator {
 public:
  /// Mouse button types
  enum class MouseButton { Left, Right, Middle };

  /// @brief Simulate mouse click (UI-01).
  /// @param widget Target widget
  /// @param button Mouse button to click
  /// @param pos Position relative to widget (default: center)
  /// @param modifiers Keyboard modifiers (Ctrl, Shift, Alt)
  static void mouseClick(QWidget* widget, MouseButton button = MouseButton::Left,
                         const QPoint& pos = QPoint(),
                         Qt::KeyboardModifiers modifiers = Qt::NoModifier);

  /// @brief Simulate mouse double-click.
  /// @param widget Target widget
  /// @param button Mouse button to double-click
  /// @param pos Position relative to widget (default: center)
  /// @param modifiers Keyboard modifiers
  static void mouseDoubleClick(QWidget* widget, MouseButton button = MouseButton::Left,
                               const QPoint& pos = QPoint(),
                               Qt::KeyboardModifiers modifiers = Qt::NoModifier);

  /// @brief Simulate text input (UI-02).
  /// @param widget Target widget (should be focusable)
  /// @param text Text to type
  static void sendText(QWidget* widget, const QString& text);

  /// @brief Simulate key sequence (UI-02).
  /// @param widget Target widget
  /// @param sequence Key sequence string (e.g., "Ctrl+S", "Alt+F4")
  ///        Accepts standard QKeySequence format strings
  static void sendKeySequence(QWidget* widget, const QString& sequence);

  /// @brief Simulate individual key press.
  /// @param widget Target widget
  /// @param key Qt key code
  /// @param modifiers Keyboard modifiers
  static void sendKey(QWidget* widget, Qt::Key key,
                      Qt::KeyboardModifiers modifiers = Qt::NoModifier);

  // --- Extended mouse primitives for Computer Use Mode ---

  /// @brief Simulate mouse button press (without release).
  /// @param widget Target widget
  /// @param button Mouse button to press
  /// @param pos Position relative to widget (default: center)
  /// @param modifiers Keyboard modifiers
  static void mousePress(QWidget* widget, MouseButton button = MouseButton::Left,
                         const QPoint& pos = QPoint(),
                         Qt::KeyboardModifiers modifiers = Qt::NoModifier);

  /// @brief Simulate mouse button release (without press).
  /// @param widget Target widget
  /// @param button Mouse button to release
  /// @param pos Position relative to widget (default: center)
  /// @param modifiers Keyboard modifiers
  static void mouseRelease(QWidget* widget, MouseButton button = MouseButton::Left,
                           const QPoint& pos = QPoint(),
                           Qt::KeyboardModifiers modifiers = Qt::NoModifier);

  /// @brief Simulate mouse move to a position.
  /// @param widget Target widget
  /// @param pos Position relative to widget
  /// @param buttons Mouse buttons held during move (for drag)
  /// @param modifiers Keyboard modifiers
  static void mouseMove(QWidget* widget, const QPoint& pos, Qt::MouseButtons buttons = Qt::NoButton,
                        Qt::KeyboardModifiers modifiers = Qt::NoModifier);

  /// @brief Simulate mouse wheel scroll.
  ///
  /// Constructs QWheelEvent manually (QTest has no wheel simulation).
  /// @param widget Target widget
  /// @param pos Position relative to widget (default: center)
  /// @param dx Horizontal scroll ticks (positive = right)
  /// @param dy Vertical scroll ticks (positive = up)
  /// @param modifiers Keyboard modifiers
  static void scroll(QWidget* widget, const QPoint& pos = QPoint(), int dx = 0, int dy = 0,
                     Qt::KeyboardModifiers modifiers = Qt::NoModifier);

  /// @brief Simulate mouse drag from start to end position.
  ///
  /// Uses manual QMouseEvent construction (not QTest) for reliable
  /// press-move-release sequence. Positions are relative to the window.
  /// @param window Top-level window (used for childAt resolution)
  /// @param startPos Start position relative to window
  /// @param endPos End position relative to window
  /// @param button Mouse button to drag with
  /// @param modifiers Keyboard modifiers
  static void mouseDrag(QWidget* window, const QPoint& startPos, const QPoint& endPos,
                        MouseButton button = MouseButton::Left,
                        Qt::KeyboardModifiers modifiers = Qt::NoModifier);

 private:
  /// Convert MouseButton enum to Qt::MouseButton
  static Qt::MouseButton toQtButton(MouseButton button);
};

}  // namespace qtPilot
