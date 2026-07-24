// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "interaction/input_simulator.h"

#include <QEvent>
#include <QKeyEvent>
#include <QWindow>
#include <QtTest>

using namespace qtPilot;

class RecordingWindow : public QWindow {
 public:
  QList<QEvent::Type> mouseEvents;
  QList<int> pressedKeys;
  QStringList pressedText;

 protected:
  bool event(QEvent* event) override {
    switch (event->type()) {
      case QEvent::MouseButtonPress:
      case QEvent::MouseButtonRelease:
      case QEvent::MouseButtonDblClick:
      case QEvent::MouseMove:
        mouseEvents.append(event->type());
        return true;
      case QEvent::KeyPress: {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        pressedKeys.append(keyEvent->key());
        pressedText.append(keyEvent->text());
        return true;
      }
      case QEvent::KeyRelease:
        return true;
      default:
        return QWindow::event(event);
    }
  }
};

class TestQWindowInput : public QObject {
  Q_OBJECT

 private slots:
  void testClickSequence();
  void testDoubleClickSequence();
  void testDragSequence();
  void testTextMapsControlCharactersToSpecialKeys();
};

void TestQWindowInput::testClickSequence() {
  RecordingWindow window;
  window.setGeometry(0, 0, 200, 100);

  InputSimulator::mouseClick(&window, InputSimulator::MouseButton::Left, QPoint(20, 30));

  QCOMPARE(window.mouseEvents,
           QList<QEvent::Type>({QEvent::MouseButtonPress, QEvent::MouseButtonRelease}));
}

void TestQWindowInput::testDoubleClickSequence() {
  RecordingWindow window;
  window.setGeometry(0, 0, 200, 100);

  InputSimulator::mouseDoubleClick(&window, InputSimulator::MouseButton::Left, QPoint(20, 30));

  QCOMPARE(window.mouseEvents,
           QList<QEvent::Type>({QEvent::MouseButtonPress, QEvent::MouseButtonRelease,
                                QEvent::MouseButtonDblClick, QEvent::MouseButtonRelease}));
}

void TestQWindowInput::testDragSequence() {
  RecordingWindow window;
  window.setGeometry(0, 0, 200, 100);

  InputSimulator::mouseDrag(&window, QPoint(10, 20), QPoint(80, 60));

  QCOMPARE(window.mouseEvents,
           QList<QEvent::Type>({QEvent::MouseButtonPress, QEvent::MouseMove,
                                QEvent::MouseButtonRelease}));
}

void TestQWindowInput::testTextMapsControlCharactersToSpecialKeys() {
  RecordingWindow window;

  InputSimulator::sendText(&window, QStringLiteral("a\n\t\b"));

  QCOMPARE(window.pressedKeys,
           QList<int>({Qt::Key_A, Qt::Key_Return, Qt::Key_Tab, Qt::Key_Backspace}));
  QCOMPARE(window.pressedText,
           QStringList({QStringLiteral("a"), QStringLiteral("\n"), QStringLiteral("\t"),
                        QStringLiteral("\b")}));
}

QTEST_MAIN(TestQWindowInput)
#include "test_qwindow_input.moc"
