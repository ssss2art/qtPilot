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
#include "interaction/input_simulator.h"
#include "transport/jsonrpc_handler.h"

#include <cmath>
#include <memory>
#include <numbers>

#include <QApplication>
#include <QGraphicsProxyWidget>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGraphicsWidget>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMouseEvent>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QScroller>
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
    property int cancels: 0
    property int deviceType: -1
    property point tapPosition: Qt.point(-1, -1)
    TapHandler {
      onTapped: (eventPoint, button) => {
        pad.taps++;
        pad.tapPosition = eventPoint.position;
        pad.deviceType = eventPoint.device.type;
      }
      onLongPressed: pad.longPresses++
      onCanceled: pad.cancels++
    }
  }

  Rectangle {
    id: hiddenPad
    objectName: "hiddenPad"
    x: 0; y: 0; width: 40; height: 40
    visible: false
  }

  Rectangle {
    id: disabledPad
    objectName: "disabledPad"
    x: 0; y: 0; width: 40; height: 40
    enabled: false
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
    id: translationPad
    objectName: "translationPad"
    x: 100; y: 180; width: 280; height: 50
    property point translation: Qt.point(0, 0)
    property point pressOrigin: Qt.point(-1, -1)
    property int activations: 0
    DragHandler {
      target: null
      dragThreshold: 0
      onActiveChanged: {
        if (active) {
          translationPad.activations++;
          translationPad.pressOrigin = centroid.scenePressPosition;
        }
      }
      onActiveTranslationChanged: translationPad.translation = Qt.point(activeTranslation.x, activeTranslation.y)
    }
  }

  Rectangle {
    objectName: "knob"
    x: 10; y: 250; width: 40; height: 40
    DragHandler {}
  }
}
)";

/// A 400x400 window with a Flickable for drag/flick & momentum tests.
constexpr char kFlickableSceneQml[] = R"(
import QtQuick
import QtQuick.Window

Window {
  width: 300
  height: 200

  Flickable {
    id: flickable
    objectName: "flickable"
    anchors.fill: parent
    contentWidth: 300
    contentHeight: 1200
    clip: true

    Rectangle {
      width: 300
      height: 1200
      color: "lightgray"
    }
  }
}
)";

/// A 400x400 window with a MultiPointTouchArea tracking 3+ simultaneous fingers.
constexpr char kMultiPointSceneQml[] = R"(
import QtQuick
import QtQuick.Window

Window {
  width: 400
  height: 300

  MultiPointTouchArea {
    id: multiTouch
    objectName: "multiTouch"
    anchors.fill: parent
    minimumTouchPoints: 1
    maximumTouchPoints: 5

    property int maxSimultaneous: 0
    property int currentActiveCount: 0
    property int pressedEvents: 0
    property int updatedEvents: 0
    property int releasedEvents: 0

    touchPoints: [
      TouchPoint { id: tp0; objectName: "tp0" },
      TouchPoint { id: tp1; objectName: "tp1" },
      TouchPoint { id: tp2; objectName: "tp2" }
    ]

    onPressed: (touchPoints) => { multiTouch.pressedEvents += touchPoints.length; }
    onUpdated: (touchPoints) => { multiTouch.updatedEvents += touchPoints.length; }
    onReleased: (touchPoints) => { multiTouch.releasedEvents += touchPoints.length; }
    onTouchUpdated: (touchPoints) => {
      multiTouch.currentActiveCount = touchPoints.length;
      multiTouch.maxSimultaneous = Math.max(multiTouch.maxSimultaneous, touchPoints.length);
    }
  }
}
)";

/// A 400x400 window with a PinchHandler tracking scale-down and rotation.
constexpr char kPinchTransformSceneQml[] = R"(
import QtQuick
import QtQuick.Window

Window {
  width: 400
  height: 400

  Rectangle {
    id: transformPad
    objectName: "transformPad"
    anchors.fill: parent
    property real smallestScale: 1
    property real maxAbsRotation: 0

    PinchHandler {
      target: null
      onActiveScaleChanged: {
        transformPad.smallestScale = Math.min(transformPad.smallestScale, activeScale);
      }
      onActiveRotationChanged: {
        transformPad.maxAbsRotation = Math.max(transformPad.maxAbsRotation, Math.abs(activeRotation));
      }
    }
  }
}
)";

/// A 200x200 window with a MouseArea (no TapHandler) to verify touch-to-mouse synthesis.
constexpr char kMouseAreaSceneQml[] = R"(
import QtQuick
import QtQuick.Window

Window {
  width: 200
  height: 200

  Rectangle {
    id: mousePad
    objectName: "mousePad"
    anchors.fill: parent
    property int presses: 0
    property int releases: 0
    property int clicks: 0
    property point clickPosition: Qt.point(-1, -1)

    MouseArea {
      anchors.fill: parent
      onPressed: mousePad.presses++
      onReleased: mousePad.releases++
      onClicked: (mouse) => {
        mousePad.clicks++;
        mousePad.clickPosition = Qt.point(mouse.x, mouse.y);
      }
    }
  }
}
)";

/// A Quick window built from QML, torn down with the test.
class QuickScene {
 public:
  explicit QuickScene(const char* qml = kSceneQml) {
    QQmlComponent component(&m_engine);
    component.setData(QByteArray(qml), QUrl());
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
  explicit TouchRecorder(QWidget* parent = nullptr) : QWidget(parent) {
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
      case QEvent::TouchEnd:
      case QEvent::TouchCancel: {
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

/// A widget that does NOT opt into touch events, recording synthesized mouse events.
class MouseRecorder final : public QWidget {
 public:
  explicit MouseRecorder(QWidget* parent = nullptr) : QWidget(parent) { resize(200, 100); }

  int pressCount = 0;
  int moveCount = 0;
  int releaseCount = 0;
  QPointF lastPressPos;
  QPointF lastReleasePos;

 protected:
  void mousePressEvent(QMouseEvent* event) override {
    ++pressCount;
    lastPressPos = event->position();
    event->accept();
  }

  void mouseMoveEvent(QMouseEvent* event) override {
    ++moveCount;
    event->accept();
  }

  void mouseReleaseEvent(QMouseEvent* event) override {
    ++releaseCount;
    lastReleasePos = event->position();
    event->accept();
  }
};

/// A QGraphicsWidget that accepts touch events and records them.
class TouchGraphicsWidget final : public QGraphicsWidget {
 public:
  explicit TouchGraphicsWidget(QGraphicsItem* parent = nullptr) : QGraphicsWidget(parent) {
    setAcceptTouchEvents(true);
    resize(120, 80);
  }

  struct Seen {
    QEvent::Type type;
    QList<QPointF> positions;
  };
  QList<Seen> seen;

 protected:
  bool sceneEvent(QEvent* event) override {
    switch (event->type()) {
      case QEvent::TouchBegin:
      case QEvent::TouchUpdate:
      case QEvent::TouchEnd:
      case QEvent::TouchCancel: {
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
        return QGraphicsWidget::sceneEvent(event);
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
  void testTargetlessMouseDragReportsTranslationFromItsPress();
  void testTargetlessTouchDragReportsTranslationFromItsPress();
  void testTwoFingersPinch();
  void testPositionsAreInTheTargetsCoordinates();
  void testWidgetReceivesTouchEvents();
  void testFlickableDragAndMomentum();
  void testMultiPointTouchAreaThreeFingers();
  void testPinchInScalesDown();
  void testPinchRotationChangesAngle();
  void testQmlTouchSynthesizesMouseForMouseArea();
  void testWidgetScrollAreaKineticScroll();
  void testWidgetNestedChildCoordinateMapping();
  void testWidgetWithoutAcceptTouchEventsReceivesSynthesizedMouse();

  // What the call reports
  void testResultReportsEachFingersState();
  void testResultReportsWindowPositions();

  // Hardening & target parity
  void testDryRunValidatesAndReportsWithoutMutatingOrDelivering();
  void testWidgetInsideGraphicsProxyWidgetRoutesTouchToView();
  void testGraphicsObjectWithViewObjectIdReceivesTouch();
  void testInvisibleOrDisabledQuickItemIsRejected();
  void testCancelReleasesHeldFingersCleanly();
  void testCancelWhenNothingHeldIsClean();

  // Errors
  void testPointsAreRequired();
  void testInvalidPointPayloadsAreRejected_data();
  void testInvalidPointPayloadsAreRejected();
  void testDuplicateFingerIsRejected();
  void testReleasingWithNothingHeldIsRejected();
  void testAnotherWindowWhileHeldIsRejected();
  void testWindowDestroyedWhileHeldClearsState();
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

void TestUiTouch::testTargetlessMouseDragReportsTranslationFromItsPress() {
  QuickScene scene;
  QQuickItem* pad = scene.item("translationPad");
  const QPoint start(130, 200);
  const auto release = qScopeGuard([&scene, start] {
    InputSimulator::mouseRelease(scene.window(), InputSimulator::MouseButton::Left,
                                 start + QPoint(120, 0));
  });
  for (int step = 0; step <= 6; ++step) {
    const QPoint at = start + QPoint(step * 20, 0);
    if (step == 0)
      InputSimulator::mousePress(scene.window(), InputSimulator::MouseButton::Left, at);
    else
      InputSimulator::mouseMove(scene.window(), at, Qt::LeftButton);
    QTest::qWait(kFrameMs);
  }
  QTRY_COMPARE(pad->property("activations").toInt(), 1);
  QEXPECT_THAT(pad->property("pressOrigin").toPointF(), Eq(QPointF(start)));
  QTRY_COMPARE(pad->property("translation").toPointF(), QPointF(120, 0));
}

void TestUiTouch::testTargetlessTouchDragReportsTranslationFromItsPress() {
  QuickScene scene;
  const auto release = qScopeGuard([this, &scene] { touch(scene.window(), QJsonArray{}); });
  QQuickItem* pad = scene.item("translationPad");
  for (int step = 0; step <= 6; ++step) {
    QEXPECT_THAT(touch(scene.window(), fingers({finger(0, 130 + step * 20, 200)})),
                 IsJsonRpcSuccess());
    QTest::qWait(kFrameMs);
  }
  QTRY_COMPARE(pad->property("activations").toInt(), 1);
  QEXPECT_THAT(pad->property("pressOrigin").toPointF(), Eq(QPointF(130, 200)));
  QTRY_COMPARE(pad->property("translation").toPointF(), QPointF(120, 0));
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

  // PinchHandler measures from where it activated, some way into the spread
  // rather than at the press, so it reports less than the full 9; how far in
  // depends on the Qt version and the machine's frame timing (5.1 on one, 3.6 on
  // another). Well over 1 is what only two fingers moving apart can produce.
  const qreal scale = pinchPad->property("largestScale").toReal();
  QVERIFY2(scale > 2.0 && scale <= 9.0, qPrintable(QString::number(scale)));
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

void TestUiTouch::testFlickableDragAndMomentum() {
  QuickScene scene(kFlickableSceneQml);
  QQuickItem* flickable = scene.item("flickable");

  // Drag upward fast enough to trigger a flick, spacing frames by kFrameMs so
  // Qt Quick's velocity tracker sees real movement over time.
  for (int step = 0; step <= 5; ++step) {
    QEXPECT_THAT(touch(flickable, fingers({finger(0, 150, 170 - step * 30)})), IsJsonRpcSuccess());
    QTest::qWait(kFrameMs);
  }
  QTRY_VERIFY_WITH_TIMEOUT(flickable->property("contentY").toReal() > 0.0, 2000);
  const qreal contentYBeforeRelease = flickable->property("contentY").toReal();
  QVERIFY2(contentYBeforeRelease > 0.0, qPrintable(QString::number(contentYBeforeRelease)));

  QEXPECT_THAT(touch(flickable, QJsonArray{}), IsJsonRpcSuccess());
  QTRY_VERIFY_WITH_TIMEOUT(!flickable->property("moving").toBool(), 3000);

  const qreal settledContentY = flickable->property("contentY").toReal();
  QVERIFY2(
      settledContentY > contentYBeforeRelease,
      qPrintable(
          QStringLiteral("before=%1 settled=%2").arg(contentYBeforeRelease).arg(settledContentY)));
}

void TestUiTouch::testMultiPointTouchAreaThreeFingers() {
  QuickScene scene(kMultiPointSceneQml);
  QQuickItem* area = scene.item("multiTouch");

  // Press 3 fingers simultaneously.
  QEXPECT_THAT(touch(area, fingers({finger(0, 50, 50), finger(1, 150, 50), finger(2, 250, 50)})),
               IsJsonRpcSuccess());
  QTest::qWait(kFrameMs);
  QTRY_COMPARE(area->property("maxSimultaneous").toInt(), 3);
  QCOMPARE(area->property("currentActiveCount").toInt(), 3);

  // Move all 3 fingers.
  QEXPECT_THAT(touch(area, fingers({finger(0, 50, 110), finger(1, 150, 110), finger(2, 250, 110)})),
               IsJsonRpcSuccess());
  QTest::qWait(kFrameMs);
  QTRY_VERIFY(area->property("updatedEvents").toInt() >= 3);

  // Partial release: lift finger 1 while keeping fingers 0 and 2 down.
  const QJsonObject partialResponse =
      touch(area, fingers({finger(0, 50, 110), finger(2, 250, 110)}));
  QEXPECT_THAT(partialResponse, IsJsonRpcSuccess());
  QCOMPARE(statesOf(partialResponse), (QStringList{"stationary", "released", "stationary"}));
  QTest::qWait(kFrameMs);
  QTRY_COMPARE(area->property("currentActiveCount").toInt(), 2);

  // Final release: lift remaining fingers.
  const QJsonObject finalResponse = touch(area, QJsonArray{});
  QEXPECT_THAT(finalResponse, IsJsonRpcSuccess());
  QCOMPARE(statesOf(finalResponse), (QStringList{"released", "released"}));
  QTest::qWait(kFrameMs);
  QTRY_COMPARE(area->property("currentActiveCount").toInt(), 0);
  QVERIFY(area->property("releasedEvents").toInt() >= 3);
}

void TestUiTouch::testPinchInScalesDown() {
  QuickScene scene(kPinchTransformSceneQml);
  QQuickItem* pad = scene.item("transformPad");

  // Start 320 px apart and pinch inward to 40 px apart.
  for (int step = 0; step <= 14; ++step) {
    const qreal half = 160 - step * 10;
    QEXPECT_THAT(touch(pad, fingers({finger(0, 200 - half, 200), finger(1, 200 + half, 200)})),
                 IsJsonRpcSuccess());
    QTest::qWait(kFrameMs);
  }
  QEXPECT_THAT(touch(pad, QJsonArray{}), IsJsonRpcSuccess());

  const qreal scale = pad->property("smallestScale").toReal();
  QVERIFY2(scale > 0.0 && scale < 0.5, qPrintable(QString::number(scale)));
}

void TestUiTouch::testPinchRotationChangesAngle() {
  QuickScene scene(kPinchTransformSceneQml);
  QQuickItem* pad = scene.item("transformPad");

  constexpr qreal kCenterX = 200.0;
  constexpr qreal kCenterY = 200.0;
  constexpr qreal kRadius = 100.0;
  constexpr int kSteps = 12;
  constexpr qreal kTotalRadians = std::numbers::pi / 3.0;  // 60 degrees

  for (int step = 0; step <= kSteps; ++step) {
    const qreal angle = (kTotalRadians * step) / kSteps;
    const qreal dx = kRadius * std::cos(angle);
    const qreal dy = kRadius * std::sin(angle);
    QEXPECT_THAT(touch(pad, fingers({finger(0, kCenterX - dx, kCenterY - dy),
                                     finger(1, kCenterX + dx, kCenterY + dy)})),
                 IsJsonRpcSuccess());
    QTest::qWait(kFrameMs);
  }
  QEXPECT_THAT(touch(pad, QJsonArray{}), IsJsonRpcSuccess());

  const qreal maxAbsRotation = pad->property("maxAbsRotation").toReal();
  QVERIFY2(maxAbsRotation > 20.0, qPrintable(QString::number(maxAbsRotation)));
}

void TestUiTouch::testQmlTouchSynthesizesMouseForMouseArea() {
  QuickScene scene(kMouseAreaSceneQml);
  QQuickItem* mousePad = scene.item("mousePad");

  QEXPECT_THAT(touch(mousePad, fingers({finger(0, 80, 90)})), IsJsonRpcSuccess());
  QEXPECT_THAT(touch(mousePad, QJsonArray{}), IsJsonRpcSuccess());

  QTRY_COMPARE(mousePad->property("clicks").toInt(), 1);
  QCOMPARE(mousePad->property("presses").toInt(), 1);
  QCOMPARE(mousePad->property("releases").toInt(), 1);
  QCOMPARE(mousePad->property("clickPosition").toPointF(), QPointF(80, 90));
}

void TestUiTouch::testWidgetScrollAreaKineticScroll() {
  QScrollArea scrollArea;
  scrollArea.resize(240, 200);
  auto* content = new QWidget;
  content->resize(220, 1200);
  scrollArea.setWidget(content);
  scrollArea.show();
  QVERIFY(QTest::qWaitForWindowExposed(&scrollArea));

  QScroller::grabGesture(scrollArea.viewport(), QScroller::TouchGesture);

  QWidget* viewport = scrollArea.viewport();
  for (int step = 0; step <= 5; ++step) {
    QEXPECT_THAT(touch(viewport, fingers({finger(0, 100, 170 - step * 30)})), IsJsonRpcSuccess());
    QTest::qWait(kFrameMs);
  }
  const int scrollBeforeRelease = scrollArea.verticalScrollBar()->value();
  QEXPECT_THAT(touch(viewport, QJsonArray{}), IsJsonRpcSuccess());

  QScroller* scroller = QScroller::scroller(viewport);
  QTRY_COMPARE_WITH_TIMEOUT(scroller->state(), QScroller::Inactive, 3000);
  QVERIFY2(scrollArea.verticalScrollBar()->value() > 0,
           qPrintable(QStringLiteral("before=%1 after=%2")
                          .arg(scrollBeforeRelease)
                          .arg(scrollArea.verticalScrollBar()->value())));
}

void TestUiTouch::testWidgetNestedChildCoordinateMapping() {
  QWidget root;
  root.resize(300, 300);
  auto* middle = new QWidget(&root);
  middle->setGeometry(40, 50, 200, 200);
  auto* nested = new TouchRecorder(middle);
  nested->setGeometry(30, 20, 100, 80);

  root.show();
  QVERIFY(QTest::qWaitForWindowExposed(&root));

  // nested sits at (40 + 30, 50 + 20) = (70, 70) in root window coordinates.
  const QJsonObject response = touch(nested, fingers({finger(0, 15, 25)}));
  QEXPECT_THAT(response, IsJsonRpcSuccess());
  QEXPECT_THAT(touch(nested, QJsonArray{}), IsJsonRpcSuccess());

  QTRY_COMPARE(nested->seen.size(), 2);
  QCOMPARE(nested->seen[0].type, QEvent::TouchBegin);
  QCOMPARE(nested->seen[0].positions, QList<QPointF>{QPointF(15, 25)});

  const QJsonObject only = response[QStringLiteral("result")]
                               .toObject()[QStringLiteral("result")]
                               .toObject()[QStringLiteral("touches")]
                               .toArray()
                               .first()
                               .toObject();
  QCOMPARE(only[QStringLiteral("x")].toDouble(), 85.0);
  QCOMPARE(only[QStringLiteral("y")].toDouble(), 95.0);
}

void TestUiTouch::testWidgetWithoutAcceptTouchEventsReceivesSynthesizedMouse() {
  MouseRecorder widget;
  widget.show();
  QVERIFY(QTest::qWaitForWindowExposed(&widget));

  QEXPECT_THAT(touch(&widget, fingers({finger(0, 50, 40)})), IsJsonRpcSuccess());
  QEXPECT_THAT(touch(&widget, fingers({finger(0, 60, 40)})), IsJsonRpcSuccess());
  QEXPECT_THAT(touch(&widget, QJsonArray{}), IsJsonRpcSuccess());

  QTRY_COMPARE(widget.pressCount, 1);
  QCOMPARE(widget.releaseCount, 1);
  QCOMPARE(widget.lastPressPos, QPointF(50, 40));
  QCOMPARE(widget.lastReleasePos, QPointF(60, 40));
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

// --- Hardening & target parity ------------------------------------------------

void TestUiTouch::testDryRunValidatesAndReportsWithoutMutatingOrDelivering() {
  QuickScene scene;
  QQuickItem* pad = scene.item("pad");
  m_lastTarget = pad;

  const QJsonObject dryPress =
      call(QJsonObject{{QStringLiteral("objectId"), ObjectRegistry::instance()->objectId(pad)},
                       {QStringLiteral("points"), fingers({finger(0, 30, 20)})},
                       {QStringLiteral("dryRun"), true}});
  QEXPECT_THAT(dryPress, IsJsonRpcSuccess());
  const QJsonObject dryPressResult =
      dryPress[QStringLiteral("result")].toObject()[QStringLiteral("result")].toObject();
  QCOMPARE(dryPressResult[QStringLiteral("dryRun")].toBool(), true);
  QCOMPARE(dryPressResult[QStringLiteral("deferred")].toBool(), false);
  QCOMPARE(statesOf(dryPress), QStringList{"pressed"});

  // Nothing was held: an empty frame is still rejected, and no tap occurred.
  QEXPECT_THAT(touch(pad, QJsonArray{}), IsJsonRpcError(JsonRpcError::kInvalidParams));
  QCOMPARE(pad->property("taps").toInt(), 0);

  // Now actually hold finger 0, then dryRun a move + second finger.
  QEXPECT_THAT(touch(pad, fingers({finger(0, 30, 20)})), IsJsonRpcSuccess());
  const QJsonObject dryMove =
      call(QJsonObject{{QStringLiteral("objectId"), ObjectRegistry::instance()->objectId(pad)},
                       {QStringLiteral("points"), fingers({finger(0, 50, 20), finger(1, 80, 20)})},
                       {QStringLiteral("dryRun"), true}});
  QEXPECT_THAT(dryMove, IsJsonRpcSuccess());
  QCOMPARE(statesOf(dryMove), (QStringList{"moved", "pressed"}));

  // Lifting now should only release finger 0 (at its original position), not finger 1.
  QCOMPARE(statesOf(touch(pad, QJsonArray{})), QStringList{"released"});
}

void TestUiTouch::testWidgetInsideGraphicsProxyWidgetRoutesTouchToView() {
  QGraphicsScene scene;
  auto* recorder = new TouchRecorder;
  QGraphicsProxyWidget* proxy = scene.addWidget(recorder);
  proxy->setAcceptTouchEvents(true);
  proxy->setPos(40, 30);

  QGraphicsView view(&scene);
  view.viewport()->setAttribute(Qt::WA_AcceptTouchEvents);
  view.resize(320, 240);
  view.show();
  QVERIFY(QTest::qWaitForWindowExposed(&view));
  m_lastTarget = recorder;

  QEXPECT_THAT(touch(recorder, fingers({finger(0, 20, 30)})), IsJsonRpcSuccess());
  QEXPECT_THAT(touch(recorder, QJsonArray{}), IsJsonRpcSuccess());

  QTRY_COMPARE(recorder->seen.size(), 2);
  QCOMPARE(recorder->seen[0].type, QEvent::TouchBegin);
  QCOMPARE(recorder->seen[0].positions, QList<QPointF>{QPointF(20, 30)});
  QCOMPARE(recorder->seen[1].type, QEvent::TouchEnd);
}

void TestUiTouch::testGraphicsObjectWithViewObjectIdReceivesTouch() {
  QGraphicsScene scene;
  auto* item = new TouchGraphicsWidget;
  item->setPos(50, 40);
  scene.addItem(item);

  QGraphicsView view(&scene);
  view.viewport()->setAttribute(Qt::WA_AcceptTouchEvents);
  view.resize(320, 240);
  view.show();
  QVERIFY(QTest::qWaitForWindowExposed(&view));

  QGraphicsScene otherScene;
  QGraphicsView otherView(&otherScene);
  otherView.resize(200, 200);
  otherView.show();
  QVERIFY(QTest::qWaitForWindowExposed(&otherView));

  const QString itemId = ObjectRegistry::instance()->objectId(item);
  const QString viewId = ObjectRegistry::instance()->objectId(&view);
  const QString otherViewId = ObjectRegistry::instance()->objectId(&otherView);
  m_lastTarget = item;

  // Mismatched viewObjectId is rejected.
  QEXPECT_THAT(call(QJsonObject{{QStringLiteral("objectId"), itemId},
                                {QStringLiteral("viewObjectId"), otherViewId},
                                {QStringLiteral("points"), fingers({finger(0, 25, 35)})}}),
               IsJsonRpcError(JsonRpcError::kInvalidParams));

  // Matching viewObjectId delivers touch to the QGraphicsObject at its local coordinates.
  QEXPECT_THAT(call(QJsonObject{{QStringLiteral("objectId"), itemId},
                                {QStringLiteral("viewObjectId"), viewId},
                                {QStringLiteral("points"), fingers({finger(0, 25, 35)})}}),
               IsJsonRpcSuccess());
  QEXPECT_THAT(call(QJsonObject{{QStringLiteral("objectId"), itemId},
                                {QStringLiteral("viewObjectId"), viewId},
                                {QStringLiteral("points"), QJsonArray{}}}),
               IsJsonRpcSuccess());

  QTRY_COMPARE(item->seen.size(), 2);
  QCOMPARE(item->seen[0].type, QEvent::TouchBegin);
  QCOMPARE(item->seen[0].positions, QList<QPointF>{QPointF(25, 35)});
  QCOMPARE(item->seen[1].type, QEvent::TouchEnd);
}

void TestUiTouch::testInvisibleOrDisabledQuickItemIsRejected() {
  QuickScene scene;
  QQuickItem* hiddenPad = scene.item("hiddenPad");
  QQuickItem* disabledPad = scene.item("disabledPad");

  QEXPECT_THAT(touch(hiddenPad, fingers({finger(0, 10, 10)})),
               IsJsonRpcError(ErrorCode::kWidgetNotVisible));
  QEXPECT_THAT(touch(disabledPad, fingers({finger(0, 10, 10)})),
               IsJsonRpcError(ErrorCode::kWidgetNotEnabled));
  m_lastTarget = nullptr;
}

void TestUiTouch::testCancelReleasesHeldFingersCleanly() {
  QuickScene first;
  QuickScene second;
  QQuickItem* pad = first.item("pad");
  QQuickItem* secondPad = second.item("pad");

  QEXPECT_THAT(touch(pad, fingers({finger(0, 30, 20)})), IsJsonRpcSuccess());

  const QJsonObject cancelResponse =
      call(QJsonObject{{QStringLiteral("objectId"), ObjectRegistry::instance()->objectId(pad)},
                       {QStringLiteral("cancel"), true}});
  QEXPECT_THAT(cancelResponse, IsJsonRpcSuccess());
  const QJsonObject cancelResult =
      cancelResponse[QStringLiteral("result")].toObject()[QStringLiteral("result")].toObject();
  QCOMPARE(cancelResult[QStringLiteral("canceled")].toBool(), true);
  QCOMPARE(statesOf(cancelResponse), QStringList{"released"});

  // State is cleared: empty frame is rejected and touching another window works.
  m_lastTarget = nullptr;
  QEXPECT_THAT(touch(pad, QJsonArray{}), IsJsonRpcError(JsonRpcError::kInvalidParams));
  m_lastTarget = secondPad;
  QCOMPARE(statesOf(touch(secondPad, fingers({finger(0, 30, 20)}))), QStringList{"pressed"});
  QCOMPARE(statesOf(touch(secondPad, QJsonArray{})), QStringList{"released"});
  QTRY_COMPARE(secondPad->property("taps").toInt(), 1);
}

void TestUiTouch::testCancelWhenNothingHeldIsClean() {
  QuickScene scene;
  QQuickItem* pad = scene.item("pad");

  const QJsonObject cancelResponse =
      call(QJsonObject{{QStringLiteral("objectId"), ObjectRegistry::instance()->objectId(pad)},
                       {QStringLiteral("cancel"), true}});
  QEXPECT_THAT(cancelResponse, IsJsonRpcSuccess());
  const QJsonObject cancelResult =
      cancelResponse[QStringLiteral("result")].toObject()[QStringLiteral("result")].toObject();
  QCOMPARE(cancelResult[QStringLiteral("canceled")].toBool(), true);
  QCOMPARE(cancelResult[QStringLiteral("touches")].toArray().size(), 0);

  // Subsequent normal touch works cleanly.
  QCOMPARE(statesOf(touch(pad, fingers({finger(0, 30, 20)}))), QStringList{"pressed"});
  QCOMPARE(statesOf(touch(pad, QJsonArray{})), QStringList{"released"});
  QTRY_COMPARE(pad->property("taps").toInt(), 1);
}

// --- Errors -------------------------------------------------------------------

void TestUiTouch::testPointsAreRequired() {
  QuickScene scene;
  QEXPECT_THAT(call(QJsonObject{{QStringLiteral("objectId"),
                                 ObjectRegistry::instance()->objectId(scene.item("pad"))}}),
               IsJsonRpcError(JsonRpcError::kInvalidParams));
}

void TestUiTouch::testInvalidPointPayloadsAreRejected_data() {
  QTest::addColumn<QJsonArray>("points");

  QTest::newRow("negative-id") << QJsonArray{QJsonObject{
      {QStringLiteral("id"), -1}, {QStringLiteral("x"), 10}, {QStringLiteral("y"), 10}}};
  QTest::newRow("float-id") << QJsonArray{QJsonObject{
      {QStringLiteral("id"), 1.5}, {QStringLiteral("x"), 10}, {QStringLiteral("y"), 10}}};
  QTest::newRow("string-id") << QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("0")},
                                                       {QStringLiteral("x"), 10},
                                                       {QStringLiteral("y"), 10}}};
  QTest::newRow("missing-x") << QJsonArray{
      QJsonObject{{QStringLiteral("id"), 0}, {QStringLiteral("y"), 10}}};
  QTest::newRow("non-numeric-x") << QJsonArray{
      QJsonObject{{QStringLiteral("id"), 0},
                  {QStringLiteral("x"), QStringLiteral("10")},
                  {QStringLiteral("y"), 10}}};
  QTest::newRow("missing-y") << QJsonArray{
      QJsonObject{{QStringLiteral("id"), 0}, {QStringLiteral("x"), 10}}};
  QTest::newRow("non-numeric-y") << QJsonArray{
      QJsonObject{{QStringLiteral("id"), 0},
                  {QStringLiteral("x"), 10},
                  {QStringLiteral("y"), QJsonValue::Null}}};
  QTest::newRow("non-object-entry") << QJsonArray{42};
}

void TestUiTouch::testInvalidPointPayloadsAreRejected() {
  QFETCH(QJsonArray, points);

  QuickScene scene;
  QQuickItem* pad = scene.item("pad");
  QEXPECT_THAT(touch(pad, points), IsJsonRpcError(JsonRpcError::kInvalidParams));
  m_lastTarget = nullptr;
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

void TestUiTouch::testWindowDestroyedWhileHeldClearsState() {
  auto first = std::make_unique<QuickScene>();
  QuickScene second;
  QQuickItem* firstPad = first->item("pad");
  QQuickItem* secondPad = second.item("pad");

  // Hold a finger on the first window, then destroy that window without lifting.
  QEXPECT_THAT(touch(firstPad, fingers({finger(0, 30, 20)})), IsJsonRpcSuccess());
  first.reset();
  QCoreApplication::processEvents();
  m_lastTarget = nullptr;

  // Held state was cleared with the destroyed window, so points: [] has nothing to lift.
  QEXPECT_THAT(touch(secondPad, QJsonArray{}), IsJsonRpcError(JsonRpcError::kInvalidParams));
  m_lastTarget = nullptr;

  // Touching the second window now works normally.
  QCOMPARE(statesOf(touch(secondPad, fingers({finger(0, 30, 20)}))), QStringList{"pressed"});
  QCOMPARE(statesOf(touch(secondPad, QJsonArray{})), QStringList{"released"});
  QTRY_COMPARE(secondPad->property("taps").toInt(), 1);
}

void TestUiTouch::testNonVisualObjectIsRejected() {
  QObject plain;
  QEXPECT_THAT(touch(&plain, fingers({finger(0, 1, 1)})),
               IsJsonRpcError(ErrorCode::kObjectNotWidget));
  m_lastTarget = nullptr;
}

QTEST_MAIN(TestUiTouch)
#include "test_ui_touch.moc"
