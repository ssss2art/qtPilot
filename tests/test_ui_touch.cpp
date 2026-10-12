// Copyright (c) 2026 qtPilot Contributors
// SPDX-License-Identifier: MIT
//
// qt.ui.touch: touch input from a touchscreen, one frame per call.
//
// Before this, the native surface could only press a mouse: qt.ui.click,
// qt.ui.mouseDown/Move/Up and qt.ui.wheel. A touch app drives most of its UI
// through paths a mouse never takes -- Qt Quick's pointer handlers see a
// TouchScreen device, a long press is a held finger, a pinch is two of them --
// so a client could not reach those paths, and bugs that only touch shows
// stayed out of reach of any scripted run.
//
// Each case asserts what the application itself observed (its handlers' signals
// and the event points they were given), not merely that the call was accepted.

#include "api/error_codes.h"
#include "api/native_mode_api.h"
#include "common/qt_matchers.h"
#include "core/object_registry.h"
#include "transport/jsonrpc_handler.h"

#include <memory>

#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTouchEvent>
#include <QWidget>
#include <QtTest>

using namespace qtPilot;
using namespace qtPilot::test;

namespace {

/// A 400x400 window:
///
///   pad        (50, 40)  300x120  TapHandler: counts taps and long presses, and
///                                 records where a tap landed and on what device
///   nested     inside an offset parent at (20, 200), itself at (10, 10), 40x40
///   pinchPad   (0, 300)   400x100 PinchHandler with no target: records its scale
///   knob       (10, 250)  40x40   DragHandler: moves itself
constexpr char kSceneQml[] = R"(
import QtQuick
import QtQuick.Window

Window {
  objectName: "touchWindow"
  width: 400
  height: 400

  Rectangle {
    id: pad
    objectName: "pad"
    x: 50; y: 40; width: 300; height: 120
    property int taps: 0
    property int longPresses: 0
    property int deviceType: -1
    property point tapPosition: Qt.point(-1, -1)
    TapHandler {
      onTapped: (eventPoint, button) => {
        pad.taps++;
        pad.tapPosition = eventPoint.position;
        pad.deviceType = eventPoint.device.type;
      }
      onLongPressed: pad.longPresses++
    }
  }

  Item {
    x: 20; y: 200
    Rectangle {
      id: nested
      objectName: "nested"
      x: 10; y: 10; width: 40; height: 40
      property int taps: 0
      property point tapPosition: Qt.point(-1, -1)
      TapHandler {
        onTapped: (eventPoint, button) => {
          nested.taps++;
          nested.tapPosition = eventPoint.position;
        }
      }
    }
  }

  Rectangle {
    id: pinchPad
    objectName: "pinchPad"
    x: 0; y: 300; width: 400; height: 100
    property real largestScale: 1
    PinchHandler {
      target: null
      onActiveScaleChanged: pinchPad.largestScale = Math.max(pinchPad.largestScale, activeScale)
    }
  }

  Rectangle {
    objectName: "knob"
    x: 10; y: 250; width: 40; height: 40
    DragHandler {}
  }
}
)";

/// A Quick window built from QML, torn down with the test.
class QuickScene {
 public:
  QuickScene() {
    QQmlComponent component(&m_engine);
    component.setData(QByteArray(kSceneQml), QUrl());
    m_window.reset(qobject_cast<QQuickWindow*>(component.create()));
    if (!m_window) {
      qFatal("QuickScene: QML did not create a window: %s", qPrintable(component.errorString()));
    }
    m_window->show();
    if (!QTest::qWaitForWindowExposed(m_window.get())) {
      qWarning("QuickScene: window was shown but never exposed");
    }
  }

  QQuickWindow* window() const { return m_window.get(); }

  QQuickItem* item(const char* objectName) const {
    auto* found = m_window->findChild<QQuickItem*>(QString::fromLatin1(objectName));
    if (!found) {
      qFatal("QuickScene: no item named %s", objectName);
    }
    return found;
  }

 private:
  QQmlEngine m_engine;
  std::unique_ptr<QQuickWindow> m_window;
};

/// A widget that takes touch, and keeps every touch event it is sent.
class TouchRecorder final : public QWidget {
 public:
  TouchRecorder() {
    setAttribute(Qt::WA_AcceptTouchEvents);
    resize(200, 100);
  }

  struct Seen {
    QEvent::Type type;
    QList<QPointF> positions;
  };
  QList<Seen> seen;

 protected:
  bool event(QEvent* event) override {
    switch (event->type()) {
      case QEvent::TouchBegin:
      case QEvent::TouchUpdate:
      case QEvent::TouchEnd: {
        auto* touch = static_cast<QTouchEvent*>(event);
        Seen record{event->type(), {}};
        for (const QEventPoint& point : touch->points()) {
          record.positions.append(point.position());
        }
        seen.append(record);
        event->accept();
        return true;
      }
      default:
        return QWidget::event(event);
    }
  }
};

/// Long enough for a Qt Quick window to deliver one frame's input.
constexpr int kFrameMs = 20;

/// One finger, as qt.ui.touch takes it.
QJsonObject finger(int id, qreal x, qreal y) {
  return QJsonObject{
      {QStringLiteral("id"), id}, {QStringLiteral("x"), x}, {QStringLiteral("y"), y}};
}

/// The fingers of one frame.
QJsonArray fingers(std::initializer_list<QJsonObject> points) {
  QJsonArray array;
  for (const QJsonObject& point : points) {
    array.append(point);
  }
  return array;
}

/// The states a result reports, in id order: ["pressed", "moved", ...].
QStringList statesOf(const QJsonObject& response) {
  QStringList states;
  const QJsonArray touches = response[QStringLiteral("result")]
                                 .toObject()[QStringLiteral("result")]
                                 .toObject()[QStringLiteral("touches")]
                                 .toArray();
  for (const QJsonValue& touch : touches) {
    states.append(touch.toObject()[QStringLiteral("state")].toString());
  }
  return states;
}

}  // namespace

class TestUiTouch : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase();
  void cleanupTestCase();
  void cleanup();

  // What the application sees
  void testTapReachesTheItemAsATouchscreenTap();
  void testHeldFingerIsALongPress();
  void testDraggedFingerMovesADragHandlerItem();
  void testTwoFingersPinch();
  void testPositionsAreInTheTargetsCoordinates();
  void testWidgetReceivesTouchEvents();

  // What the call reports
  void testResultReportsEachFingersState();
  void testResultReportsWindowPositions();

  // Errors
  void testPointsAreRequired();
  void testDuplicateFingerIsRejected();
  void testReleasingWithNothingHeldIsRejected();
  void testAnotherWindowWhileHeldIsRejected();
  void testNonVisualObjectIsRejected();

 private:
  /// @brief qt.ui.touch at @p target with @p points.
  QJsonObject touch(QObject* target, const QJsonArray& points);
  /// @brief qt.ui.touch with exactly @p params.
  QJsonObject call(const QJsonObject& params);
  /// @brief Lift every finger still down, whichever window holds them.
  void releaseAll();

  std::unique_ptr<JsonRpcHandler> m_handler;
  std::unique_ptr<NativeModeApi> m_api;
  QPointer<QObject> m_lastTarget;
  int m_requestId = 1;
};

void TestUiTouch::initTestCase() {
  installObjectHooks();
  m_handler = std::make_unique<JsonRpcHandler>();
  m_api = std::make_unique<NativeModeApi>(m_handler.get());
}

void TestUiTouch::cleanupTestCase() {
  m_api.reset();
  m_handler.reset();
  uninstallObjectHooks();
}

void TestUiTouch::cleanup() {
  releaseAll();
}

QJsonObject TestUiTouch::touch(QObject* target, const QJsonArray& points) {
  m_lastTarget = target;
  return call(
      QJsonObject{{QStringLiteral("objectId"), ObjectRegistry::instance()->objectId(target)},
                  {QStringLiteral("points"), points}});
}

QJsonObject TestUiTouch::call(const QJsonObject& params) {
  const QJsonObject request{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                            {QStringLiteral("method"), QStringLiteral("qt.ui.touch")},
                            {QStringLiteral("params"), params},
                            {QStringLiteral("id"), m_requestId++}};
  const QString response = m_handler->HandleMessage(
      QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact)));
  // The touch is queued, as qt.ui.click is: let it land before the caller looks.
  QCoreApplication::processEvents();
  return QJsonDocument::fromJson(response.toUtf8()).object();
}

void TestUiTouch::releaseAll() {
  if (m_lastTarget) {
    touch(m_lastTarget, QJsonArray{});
  }
}

// --- What the application sees ---------------------------------------------

void TestUiTouch::testTapReachesTheItemAsATouchscreenTap() {
  QuickScene scene;
  QQuickItem* pad = scene.item("pad");

  QEXPECT_THAT(touch(pad, fingers({finger(0, 30, 20)})), IsJsonRpcSuccess());
  QEXPECT_THAT(touch(pad, QJsonArray{}), IsJsonRpcSuccess());

  QTRY_COMPARE(pad->property("taps").toInt(), 1);
  QCOMPARE(pad->property("tapPosition").toPointF(), QPointF(30, 20));
  QCOMPARE(pad->property("deviceType").toInt(),
           static_cast<int>(QInputDevice::DeviceType::TouchScreen));
}

void TestUiTouch::testHeldFingerIsALongPress() {
  QuickScene scene;
  QQuickItem* pad = scene.item("pad");

  QEXPECT_THAT(touch(pad, fingers({finger(0, 100, 60)})), IsJsonRpcSuccess());
  // Longer than TapHandler's default long-press threshold (0.8 s).
  QTRY_COMPARE_WITH_TIMEOUT(pad->property("longPresses").toInt(), 1, 3000);
  QEXPECT_THAT(touch(pad, QJsonArray{}), IsJsonRpcSuccess());

  QCOMPARE(pad->property("taps").toInt(), 0);
}

void TestUiTouch::testDraggedFingerMovesADragHandlerItem() {
  QuickScene scene;
  QQuickItem* knob = scene.item("knob");
  const qreal startX = knob->x();

  // Coordinates stay in the window here, so moving the knob does not move them.
  QQuickWindow* window = scene.window();
  for (int step = 0; step <= 6; ++step) {
    QEXPECT_THAT(touch(window, fingers({finger(0, 30 + step * 20, 270)})), IsJsonRpcSuccess());
  }
  QEXPECT_THAT(touch(window, QJsonArray{}), IsJsonRpcSuccess());

  QTRY_COMPARE(knob->x(), startX + 120);
}

void TestUiTouch::testTwoFingersPinch() {
  QuickScene scene;
  QQuickItem* pinchPad = scene.item("pinchPad");

  // Fingers 40 apart, spread to 360 apart. Each frame is a frame apart, as a
  // finger's are: Qt Quick folds touch updates that arrive within one frame
  // into the last of them, and a pinch needs to see the spread happen.
  for (int step = 0; step <= 16; ++step) {
    const qreal half = 20 + step * 10;
    QEXPECT_THAT(touch(pinchPad, fingers({finger(0, 200 - half, 50), finger(1, 200 + half, 50)})),
                 IsJsonRpcSuccess());
    QTest::qWait(kFrameMs);
  }
  QEXPECT_THAT(touch(pinchPad, QJsonArray{}), IsJsonRpcSuccess());

  // PinchHandler measures from where it activated, a touch drag threshold into
  // the spread rather than at the press, so it reports less than the full 9.
  // Well over 1 is what only two fingers moving apart can produce.
  const qreal scale = pinchPad->property("largestScale").toReal();
  QVERIFY2(scale > 4.0 && scale <= 9.0, qPrintable(QString::number(scale)));
}

void TestUiTouch::testPositionsAreInTheTargetsCoordinates() {
  QuickScene scene;
  QQuickItem* nested = scene.item("nested");

  QEXPECT_THAT(touch(nested, fingers({finger(0, 5, 7)})), IsJsonRpcSuccess());
  QEXPECT_THAT(touch(nested, QJsonArray{}), IsJsonRpcSuccess());

  QTRY_COMPARE(nested->property("taps").toInt(), 1);
  QCOMPARE(nested->property("tapPosition").toPointF(), QPointF(5, 7));
}

void TestUiTouch::testWidgetReceivesTouchEvents() {
  TouchRecorder widget;
  widget.show();
  QVERIFY(QTest::qWaitForWindowExposed(&widget));

  QEXPECT_THAT(touch(&widget, fingers({finger(0, 20, 30)})), IsJsonRpcSuccess());
  QEXPECT_THAT(touch(&widget, fingers({finger(0, 60, 30)})), IsJsonRpcSuccess());
  QEXPECT_THAT(touch(&widget, QJsonArray{}), IsJsonRpcSuccess());

  QTRY_COMPARE(widget.seen.size(), 3);
  QCOMPARE(widget.seen[0].type, QEvent::TouchBegin);
  QCOMPARE(widget.seen[0].positions, QList<QPointF>{QPointF(20, 30)});
  QCOMPARE(widget.seen[1].type, QEvent::TouchUpdate);
  QCOMPARE(widget.seen[1].positions, QList<QPointF>{QPointF(60, 30)});
  QCOMPARE(widget.seen[2].type, QEvent::TouchEnd);
}

// --- What the call reports ---------------------------------------------------

void TestUiTouch::testResultReportsEachFingersState() {
  QuickScene scene;
  QQuickItem* pinchPad = scene.item("pinchPad");

  QCOMPARE(statesOf(touch(pinchPad, fingers({finger(0, 10, 10)}))), QStringList{"pressed"});
  QCOMPARE(statesOf(touch(pinchPad, fingers({finger(0, 10, 10), finger(1, 50, 10)}))),
           (QStringList{"stationary", "pressed"}));
  QCOMPARE(statesOf(touch(pinchPad, fingers({finger(0, 20, 10)}))),
           (QStringList{"moved", "released"}));
  QCOMPARE(statesOf(touch(pinchPad, QJsonArray{})), QStringList{"released"});
}

void TestUiTouch::testResultReportsWindowPositions() {
  QuickScene scene;
  QQuickItem* nested = scene.item("nested");

  // nested sits at (30, 210) in the window.
  const QJsonObject response = touch(nested, fingers({finger(3, 5.5, 7)}));
  QEXPECT_THAT(response, IsJsonRpcSuccess(HasJsonField("result", HasJsonField("touches"))));
  const QJsonObject only = response[QStringLiteral("result")]
                               .toObject()[QStringLiteral("result")]
                               .toObject()[QStringLiteral("touches")]
                               .toArray()
                               .first()
                               .toObject();
  QCOMPARE(only[QStringLiteral("id")].toInt(), 3);
  QCOMPARE(only[QStringLiteral("x")].toDouble(), 35.5);
  QCOMPARE(only[QStringLiteral("y")].toDouble(), 217.0);
}

// --- Errors -------------------------------------------------------------------

void TestUiTouch::testPointsAreRequired() {
  QuickScene scene;
  QEXPECT_THAT(call(QJsonObject{{QStringLiteral("objectId"),
                                 ObjectRegistry::instance()->objectId(scene.item("pad"))}}),
               IsJsonRpcError(JsonRpcError::kInvalidParams));
}

void TestUiTouch::testDuplicateFingerIsRejected() {
  QuickScene scene;
  QQuickItem* pad = scene.item("pad");

  QEXPECT_THAT(touch(pad, fingers({finger(0, 10, 10), finger(0, 20, 10)})),
               IsJsonRpcError(JsonRpcError::kInvalidParams));
  // Nothing was pressed by the rejected frame.
  QCOMPARE(statesOf(touch(pad, fingers({finger(0, 10, 10)}))), QStringList{"pressed"});
}

void TestUiTouch::testReleasingWithNothingHeldIsRejected() {
  QuickScene scene;
  QEXPECT_THAT(touch(scene.item("pad"), QJsonArray{}),
               IsJsonRpcError(JsonRpcError::kInvalidParams));
  m_lastTarget = nullptr;
}

void TestUiTouch::testAnotherWindowWhileHeldIsRejected() {
  QuickScene first;
  QuickScene second;
  QQuickItem* pad = first.item("pad");

  QEXPECT_THAT(touch(pad, fingers({finger(0, 10, 10)})), IsJsonRpcSuccess());
  QEXPECT_THAT(touch(second.item("pad"), fingers({finger(1, 10, 10)})),
               IsJsonRpcError(JsonRpcError::kInvalidParams));
  // The first window's finger is still down, and lifts normally.
  m_lastTarget = pad;
  QCOMPARE(statesOf(touch(pad, QJsonArray{})), QStringList{"released"});
  QTRY_COMPARE(pad->property("taps").toInt(), 1);
}

void TestUiTouch::testNonVisualObjectIsRejected() {
  QObject plain;
  QEXPECT_THAT(touch(&plain, fingers({finger(0, 1, 1)})),
               IsJsonRpcError(ErrorCode::kObjectNotWidget));
  m_lastTarget = nullptr;
}

QTEST_MAIN(TestUiTouch)
#include "test_ui_touch.moc"
