// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "input_simulator.h"

#include "compat/compat_gui.h"

#include <stdexcept>

#include <QApplication>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include <QPointer>
#include <QTest>
#include <QWheelEvent>
#include <QWindow>

namespace qtPilot {

void InputSimulator::mouseClick(QWidget* widget, MouseButton button, const QPoint& pos,
                                Qt::KeyboardModifiers modifiers) {
  if (!widget) {
    throw std::invalid_argument("mouseClick: widget cannot be null");
  }

  mouseClickAt(widget, button, pos.isNull() ? widget->rect().center() : pos, modifiers);
}

void InputSimulator::mouseClickAt(QWidget* widget, MouseButton button, const QPoint& pos,
                                  Qt::KeyboardModifiers modifiers) {
  if (!widget) {
    throw std::invalid_argument("mouseClickAt: widget cannot be null");
  }

  // Ensure widget is visible and ready for input
  widget->activateWindow();
  widget->raise();
  QApplication::processEvents();

  // QTest::mouseClick() also treats QPoint(0, 0) as its centre sentinel, so
  // deliver the pair directly to preserve an explicit top-left position.
  QPointer<QWidget> guard(widget);
  const Qt::MouseButton qtButton = toQtButton(button);
  const QPoint globalPos = widget->mapToGlobal(pos);
  QMouseEvent press(QEvent::MouseButtonPress, QPointF(pos), QPointF(globalPos), qtButton, qtButton,
                    modifiers);
  QCoreApplication::sendEvent(widget, &press);
  QCoreApplication::processEvents();
  if (!guard) {
    return;
  }

  QMouseEvent release(QEvent::MouseButtonRelease, QPointF(pos), QPointF(globalPos), qtButton,
                      Qt::NoButton, modifiers);
  QCoreApplication::sendEvent(widget, &release);
  QCoreApplication::processEvents();
}

void InputSimulator::mouseDoubleClick(QWidget* widget, MouseButton button, const QPoint& pos,
                                      Qt::KeyboardModifiers modifiers) {
  if (!widget) {
    throw std::invalid_argument("mouseDoubleClick: widget cannot be null");
  }

  // Use widget center if no position specified
  QPoint clickPos = pos.isNull() ? widget->rect().center() : pos;

  // Ensure widget is visible and ready for input
  widget->activateWindow();
  widget->raise();
  QApplication::processEvents();

  QTest::mouseDClick(widget, toQtButton(button), modifiers, clickPos);
}

void InputSimulator::sendText(QWidget* widget, const QString& text) {
  if (!widget) {
    throw std::invalid_argument("sendText: widget cannot be null");
  }

  // Ensure widget has focus for keyboard input
  widget->setFocus();
  QApplication::processEvents();

  // QTest::keyClicks sends each character as a key event
  QTest::keyClicks(widget, text);
}

void InputSimulator::sendKeySequence(QWidget* widget, const QString& sequence) {
  if (!widget) {
    throw std::invalid_argument("sendKeySequence: widget cannot be null");
  }

  // Parse sequence string like "Ctrl+Shift+A" or "Ctrl+S"
  QKeySequence keySeq(sequence, QKeySequence::PortableText);

  if (keySeq.isEmpty()) {
    throw std::invalid_argument("sendKeySequence: invalid key sequence '" + sequence.toStdString() +
                                "'");
  }

  widget->setFocus();
  QApplication::processEvents();

  // Extract key and modifiers from first key combination
  Qt::Key extractedKey;
  Qt::KeyboardModifiers mods;
  qtPilot::compat::extractKeyCombination(keySeq, 0, extractedKey, mods);

  QTest::keyClick(widget, extractedKey, mods);
}

void InputSimulator::sendKey(QWidget* widget, Qt::Key key, Qt::KeyboardModifiers modifiers) {
  if (!widget) {
    throw std::invalid_argument("sendKey: widget cannot be null");
  }

  widget->setFocus();
  QApplication::processEvents();

  QTest::keyClick(widget, key, modifiers);
}

Qt::MouseButton InputSimulator::toQtButton(MouseButton button) {
  switch (button) {
    case MouseButton::Left:
      return Qt::LeftButton;
    case MouseButton::Right:
      return Qt::RightButton;
    case MouseButton::Middle:
      return Qt::MiddleButton;
    default:
      return Qt::LeftButton;
  }
}

// --- Extended mouse primitives for Computer Use Mode ---

void InputSimulator::mousePress(QWidget* widget, MouseButton button, const QPoint& pos,
                                Qt::KeyboardModifiers modifiers) {
  if (!widget) {
    throw std::invalid_argument("mousePress: widget cannot be null");
  }

  QPoint localPos = pos.isNull() ? widget->rect().center() : pos;
  QPoint globalPos = widget->mapToGlobal(localPos);
  Qt::MouseButton qtButton = toQtButton(button);

  QMouseEvent event(QEvent::MouseButtonPress, QPointF(localPos), QPointF(globalPos), qtButton,
                    qtButton, modifiers);
  QCoreApplication::sendEvent(widget, &event);
  QApplication::processEvents();
}

void InputSimulator::mouseRelease(QWidget* widget, MouseButton button, const QPoint& pos,
                                  Qt::KeyboardModifiers modifiers) {
  if (!widget) {
    throw std::invalid_argument("mouseRelease: widget cannot be null");
  }

  QPoint localPos = pos.isNull() ? widget->rect().center() : pos;
  QPoint globalPos = widget->mapToGlobal(localPos);
  Qt::MouseButton qtButton = toQtButton(button);

  // After release, no buttons are held
  QMouseEvent event(QEvent::MouseButtonRelease, QPointF(localPos), QPointF(globalPos), qtButton,
                    Qt::NoButton, modifiers);
  QCoreApplication::sendEvent(widget, &event);
  QApplication::processEvents();
}

void InputSimulator::mouseMove(QWidget* widget, const QPoint& pos, Qt::MouseButtons buttons,
                               Qt::KeyboardModifiers modifiers) {
  if (!widget) {
    throw std::invalid_argument("mouseMove: widget cannot be null");
  }

  QPoint globalPos = widget->mapToGlobal(pos);

  QMouseEvent event(QEvent::MouseMove, QPointF(pos), QPointF(globalPos), Qt::NoButton, buttons,
                    modifiers);
  QCoreApplication::sendEvent(widget, &event);
  QApplication::processEvents();
}

void InputSimulator::scroll(QWidget* widget, const QPoint& pos, int dx, int dy,
                            Qt::KeyboardModifiers modifiers) {
  if (!widget) {
    throw std::invalid_argument("scroll: widget cannot be null");
  }

  QPoint localPos = pos.isNull() ? widget->rect().center() : pos;
  QPoint globalPos = widget->mapToGlobal(localPos);

  // 120 units = 1 standard mouse wheel tick (15 degrees)
  QPoint angleDelta(dx * 120, dy * 120);
  QPoint pixelDelta(0, 0);

  QWheelEvent event(QPointF(localPos), QPointF(globalPos), pixelDelta, angleDelta, Qt::NoButton,
                    modifiers, Qt::NoScrollPhase,
                    false  // not inverted
  );
  QCoreApplication::sendEvent(widget, &event);
  QApplication::processEvents();
}

void InputSimulator::mouseDrag(QWidget* window, const QPoint& startPos, const QPoint& endPos,
                               MouseButton button, Qt::KeyboardModifiers modifiers) {
  if (!window) {
    throw std::invalid_argument("mouseDrag: window cannot be null");
  }

  Qt::MouseButton qtButton = toQtButton(button);

  // Resolve start widget and local coordinates
  QWidget* startWidget = window->childAt(startPos);
  if (!startWidget)
    startWidget = window;
  QPoint localStart = startWidget->mapFrom(window, startPos);
  QPoint globalStart = startWidget->mapToGlobal(localStart);

  // Press at start position
  QMouseEvent pressEvent(QEvent::MouseButtonPress, QPointF(localStart), QPointF(globalStart),
                         qtButton, qtButton, modifiers);
  QCoreApplication::sendEvent(startWidget, &pressEvent);
  QApplication::processEvents();

  // Resolve end widget and local coordinates
  QWidget* endWidget = window->childAt(endPos);
  if (!endWidget)
    endWidget = window;
  QPoint localEnd = endWidget->mapFrom(window, endPos);
  QPoint globalEnd = endWidget->mapToGlobal(localEnd);

  // Move to end position (button held)
  QMouseEvent moveEvent(QEvent::MouseMove, QPointF(localEnd), QPointF(globalEnd), Qt::NoButton,
                        qtButton, modifiers);
  QCoreApplication::sendEvent(endWidget, &moveEvent);
  QApplication::processEvents();

  // Release at end position
  QMouseEvent releaseEvent(QEvent::MouseButtonRelease, QPointF(localEnd), QPointF(globalEnd),
                           qtButton, Qt::NoButton, modifiers);
  QCoreApplication::sendEvent(endWidget, &releaseEvent);
  QApplication::processEvents();
}

// --- QWindow overloads (pure Qt Quick apps) ---

namespace {

// The window overloads always receive an explicit, bounds-resolved position
// from the Computer-Use dispatch layer, so there is no "default to center"
// convenience here — that would misread a legitimate (0,0) top-left position
// (QPoint(0,0).isNull() is true) as "unset".

/// @brief Deliver a QMouseEvent to a window at a local position.
void sendMouseToWindow(QWindow* window, QEvent::Type type, const QPoint& localPos,
                       Qt::MouseButton button, Qt::MouseButtons buttons,
                       Qt::KeyboardModifiers modifiers) {
  QPoint globalPos = window->mapToGlobal(localPos);
  QMouseEvent event(type, QPointF(localPos), QPointF(globalPos), button, buttons, modifiers);
  QCoreApplication::sendEvent(window, &event);
  QCoreApplication::processEvents();
}

}  // namespace

void InputSimulator::mousePress(QWindow* window, MouseButton button, const QPoint& pos,
                                Qt::KeyboardModifiers modifiers) {
  if (!window) {
    throw std::invalid_argument("mousePress: window cannot be null");
  }
  QPointer<QWindow> guard(window);
  Qt::MouseButton qtButton = toQtButton(button);
  sendMouseToWindow(window, QEvent::MouseButtonPress, pos, qtButton, qtButton, modifiers);
}

void InputSimulator::mouseRelease(QWindow* window, MouseButton button, const QPoint& pos,
                                  Qt::KeyboardModifiers modifiers) {
  if (!window) {
    throw std::invalid_argument("mouseRelease: window cannot be null");
  }
  QPointer<QWindow> guard(window);
  sendMouseToWindow(window, QEvent::MouseButtonRelease, pos, toQtButton(button), Qt::NoButton,
                    modifiers);
}

void InputSimulator::mouseClick(QWindow* window, MouseButton button, const QPoint& pos,
                                Qt::KeyboardModifiers modifiers) {
  if (!window) {
    throw std::invalid_argument("mouseClick: window cannot be null");
  }
  // Each primitive pumps the event loop; if a handler destroys the window
  // (e.g. clicking a "Close" control), bail before re-dereferencing it.
  QPointer<QWindow> guard(window);
  mousePress(window, button, pos, modifiers);
  if (!guard)
    return;
  mouseRelease(window, button, pos, modifiers);
}

void InputSimulator::mouseDoubleClick(QWindow* window, MouseButton button, const QPoint& pos,
                                      Qt::KeyboardModifiers modifiers) {
  if (!window) {
    throw std::invalid_argument("mouseDoubleClick: window cannot be null");
  }
  QPointer<QWindow> guard(window);
  Qt::MouseButton qtButton = toQtButton(button);
  // Qt delivers a double-click as press, release, dblclick, release — the second
  // physical press arrives AS the MouseButtonDblClick, not a separate press, so
  // there is no extra MousePress (which would fire a spurious onPressed).
  mousePress(window, button, pos, modifiers);
  if (!guard)
    return;
  mouseRelease(window, button, pos, modifiers);
  if (!guard)
    return;
  sendMouseToWindow(window, QEvent::MouseButtonDblClick, pos, qtButton, qtButton, modifiers);
  if (!guard)
    return;
  mouseRelease(window, button, pos, modifiers);
}

void InputSimulator::mouseMove(QWindow* window, const QPoint& pos, Qt::MouseButtons buttons,
                               Qt::KeyboardModifiers modifiers) {
  if (!window) {
    throw std::invalid_argument("mouseMove: window cannot be null");
  }
  QPointer<QWindow> guard(window);
  sendMouseToWindow(window, QEvent::MouseMove, pos, Qt::NoButton, buttons, modifiers);
}

void InputSimulator::scroll(QWindow* window, const QPoint& pos, int dx, int dy,
                            Qt::KeyboardModifiers modifiers) {
  if (!window) {
    throw std::invalid_argument("scroll: window cannot be null");
  }
  QPointer<QWindow> guard(window);
  QPoint localPos = pos;
  QPoint globalPos = window->mapToGlobal(localPos);

  // 120 units = 1 standard mouse wheel tick (15 degrees)
  QPoint angleDelta(dx * 120, dy * 120);
  QPoint pixelDelta(0, 0);

  QWheelEvent event(QPointF(localPos), QPointF(globalPos), pixelDelta, angleDelta, Qt::NoButton,
                    modifiers, Qt::NoScrollPhase, false);
  QCoreApplication::sendEvent(window, &event);
  if (guard) {
    QCoreApplication::processEvents();
  }
}

void InputSimulator::mouseDrag(QWindow* window, const QPoint& startPos, const QPoint& endPos,
                               MouseButton button, Qt::KeyboardModifiers modifiers) {
  if (!window) {
    throw std::invalid_argument("mouseDrag: window cannot be null");
  }
  Qt::MouseButton qtButton = toQtButton(button);
  // QQuickWindow routes each event to the item at the scene position; no
  // childAt resolution is needed (unlike the QWidget path). Guard against the
  // window being destroyed by a handler between pumped events.
  QPointer<QWindow> guard(window);
  sendMouseToWindow(window, QEvent::MouseButtonPress, startPos, qtButton, qtButton, modifiers);
  if (!guard)
    return;
  sendMouseToWindow(window, QEvent::MouseMove, endPos, Qt::NoButton, qtButton, modifiers);
  if (!guard)
    return;
  sendMouseToWindow(window, QEvent::MouseButtonRelease, endPos, qtButton, Qt::NoButton, modifiers);
}

void InputSimulator::sendText(QWindow* window, const QString& text) {
  if (!window) {
    throw std::invalid_argument("sendText: window cannot be null");
  }
  // Deliver each character as a key press+release carrying its text; a
  // QQuickWindow forwards these to its focused item (e.g. a TextInput).
  // Guard against window destruction mid-loop by a key handler.
  QPointer<QWindow> guard(window);
  for (const QChar ch : text) {
    if (!guard)
      break;
    QString s(ch);
    int key;
    switch (ch.unicode()) {
      case u'\n':
      case u'\r':
        key = Qt::Key_Return;  // so QML onAccepted / Keys.onReturnPressed fires
        break;
      case u'\t':
        key = Qt::Key_Tab;
        break;
      case u'\b':
        key = Qt::Key_Backspace;
        break;
      default:
        // ASCII letters/digits/punctuation map 1:1 to Qt::Key_* by their
        // uppercased code point (Qt keys are case-insensitive — the letter case
        // lives in text()). Non-ASCII gets a best-effort code; text() carries
        // the actual character for insertion regardless.
        key = ch.toUpper().unicode();
        break;
    }
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, s);
    QCoreApplication::sendEvent(window, &press);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier, s);
    QCoreApplication::sendEvent(window, &release);
  }
  QCoreApplication::processEvents();
}

void InputSimulator::sendKey(QWindow* window, Qt::Key key, Qt::KeyboardModifiers modifiers) {
  if (!window) {
    throw std::invalid_argument("sendKey: window cannot be null");
  }
  QKeyEvent press(QEvent::KeyPress, key, modifiers);
  QCoreApplication::sendEvent(window, &press);
  QKeyEvent release(QEvent::KeyRelease, key, modifiers);
  QCoreApplication::sendEvent(window, &release);
  QCoreApplication::processEvents();
}

void InputSimulator::sendKeySequence(QWindow* window, const QString& sequence) {
  if (!window) {
    throw std::invalid_argument("sendKeySequence: window cannot be null");
  }

  QKeySequence keySeq(sequence, QKeySequence::PortableText);
  if (keySeq.isEmpty()) {
    throw std::invalid_argument("sendKeySequence: invalid key sequence '" + sequence.toStdString() +
                                "'");
  }

  // Mirrors the QWidget overload: only the first key combination is sent.
  Qt::Key extractedKey;
  Qt::KeyboardModifiers mods;
  qtPilot::compat::extractKeyCombination(keySeq, 0, extractedKey, mods);

  // No setFocus() equivalent here -- a QWindow delivers to whatever item
  // currently holds focus, which is what the caller wants for a shortcut.
  sendKey(window, extractedKey, mods);
}

}  // namespace qtPilot
