// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "input_simulator.h"

#include "compat/compat_gui.h"

#include <stdexcept>

#include <QApplication>
#include <QKeySequence>
#include <QMouseEvent>
#include <QTest>
#include <QWheelEvent>

namespace qtPilot {

void InputSimulator::mouseClick(QWidget* widget, MouseButton button, const QPoint& pos,
                                Qt::KeyboardModifiers modifiers) {
  if (!widget) {
    throw std::invalid_argument("mouseClick: widget cannot be null");
  }

  // Use widget center if no position specified
  QPoint clickPos = pos.isNull() ? widget->rect().center() : pos;

  // Ensure widget is visible and ready for input
  widget->activateWindow();
  widget->raise();
  QApplication::processEvents();

  QTest::mouseClick(widget, toQtButton(button), modifiers, clickPos);
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

}  // namespace qtPilot
