// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "common/qt_matchers.h"
#include "interaction/input_simulator.h"

#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWindow>
#include <QtTest>

using namespace qtPilot;
using namespace qtPilot::test;

class RecordingWindow : public QWindow {
 public:
  QList<QEvent::Type> mouseEvents;
  struct MousePoint {
    ulong timestamp;
    QPointF position;
    QPointF globalPosition;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QPointF scenePressPosition;
    QPointF globalPressPosition;
#endif
  };
  QList<MousePoint> mousePoints;
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
        {
          const auto* mouse = static_cast<QMouseEvent*>(event);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
          mousePoints.append({mouse->timestamp(), mouse->position(), mouse->globalPosition(),
                              mouse->points().at(0).scenePressPosition(),
                              mouse->points().at(0).globalPressPosition()});
#else
          mousePoints.append({mouse->timestamp(), mouse->localPos(), mouse->screenPos()});
#endif
        }
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

namespace {
MATCHER_P2(IsPointerFrameAt, position, global,
           "has timestamped local and global pointer coordinates") {
  *result_listener << "timestamp " << arg.timestamp << ", local " << arg.position.x() << ","
                   << arg.position.y();
  return arg.timestamp > 0 && arg.position == position && arg.globalPosition == global;
}
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
MATCHER_P2(KeepsPointerPressOrigin, scene, global,
           "retains the pointer's scene and global press origin") {
  *result_listener << "scene press " << arg.scenePressPosition.x() << ","
                   << arg.scenePressPosition.y() << ", global press " << arg.globalPressPosition.x()
                   << "," << arg.globalPressPosition.y();
  return arg.scenePressPosition == scene && arg.globalPressPosition == global;
}
#endif
}  // namespace

class TestQWindowInput : public QObject {
  Q_OBJECT

 private slots:
  void testClickSequence();
  void testDoubleClickSequence();
  void testDragSequence();
  void testPointerFramesHaveTimeAndRetainTheirPressOrigin();
  void testTextMapsControlCharactersToSpecialKeys();
};

void TestQWindowInput::testClickSequence() {
  RecordingWindow window;
  window.setGeometry(0, 0, 200, 100);

  InputSimulator::mouseClick(&window, InputSimulator::MouseButton::Left, QPoint(20, 30));

  QEXPECT_THAT(window.mouseEvents,
               ElementsAre(QEvent::MouseButtonPress, QEvent::MouseButtonRelease));
}

void TestQWindowInput::testDoubleClickSequence() {
  RecordingWindow window;
  window.setGeometry(0, 0, 200, 100);

  InputSimulator::mouseDoubleClick(&window, InputSimulator::MouseButton::Left, QPoint(20, 30));

  QEXPECT_THAT(window.mouseEvents,
               ElementsAre(QEvent::MouseButtonPress, QEvent::MouseButtonRelease,
                           QEvent::MouseButtonDblClick, QEvent::MouseButtonRelease));
}

void TestQWindowInput::testDragSequence() {
  RecordingWindow window;
  window.setGeometry(0, 0, 200, 100);

  InputSimulator::mouseDrag(&window, QPoint(10, 20), QPoint(80, 60));

  QEXPECT_THAT(window.mouseEvents, ElementsAre(QEvent::MouseButtonPress, QEvent::MouseMove,
                                               QEvent::MouseButtonRelease));
}

void TestQWindowInput::testPointerFramesHaveTimeAndRetainTheirPressOrigin() {
  RecordingWindow window;
  window.setGeometry(100, 120, 300, 200);
  const QPoint start(40, 50);
  const QPoint end(110, 90);
  const QPointF globalStart(window.mapToGlobal(start));
  InputSimulator::mousePress(&window, InputSimulator::MouseButton::Left, start);
  InputSimulator::mouseMove(&window, end, Qt::LeftButton);
  InputSimulator::mouseRelease(&window, InputSimulator::MouseButton::Left, end);

  QEXPECT_THAT(window.mousePoints, SizeIs(3));
  QEXPECT_THAT(window.mousePoints[0], IsPointerFrameAt(QPointF(start), globalStart));
  QEXPECT_THAT(window.mousePoints[1],
               IsPointerFrameAt(QPointF(end), QPointF(window.mapToGlobal(end))));
  QEXPECT_THAT(window.mousePoints[2],
               IsPointerFrameAt(QPointF(end), QPointF(window.mapToGlobal(end))));
  QEXPECT_THAT(window.mousePoints[1].timestamp, Gt(window.mousePoints[0].timestamp));
  QEXPECT_THAT(window.mousePoints[2].timestamp, Gt(window.mousePoints[1].timestamp));
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  QEXPECT_THAT(window.mousePoints, Each(KeepsPointerPressOrigin(QPointF(start), globalStart)));
#endif
}

void TestQWindowInput::testTextMapsControlCharactersToSpecialKeys() {
  RecordingWindow window;

  InputSimulator::sendText(&window, QStringLiteral("a\n\t\b"));

  QEXPECT_THAT(window.pressedKeys,
               ElementsAre(Qt::Key_A, Qt::Key_Return, Qt::Key_Tab, Qt::Key_Backspace));
  QEXPECT_THAT(window.pressedText, ElementsAre(QStringLiteral("a"), QStringLiteral("\n"),
                                               QStringLiteral("\t"), QStringLiteral("\b")));
}

QTEST_MAIN(TestQWindowInput)
#include "test_qwindow_input.moc"
