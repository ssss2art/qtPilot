// Copyright (c) 2026 qtPilot Contributors
// SPDX-License-Identifier: MIT
//
// qt.ui.screenshot on Qt Quick targets.
//
// The native method used to resolve its target as a QWidget and reject every
// QQuickWindow and QQuickItem with "Object is not a widget", so a pure Qt Quick
// app could not be looked at through the native surface at all. These tests pin
// what a capture of a Quick target must contain -- not merely that it is a PNG,
// but that it is the item's pixels, cropped to the item, at the same device
// pixel ratio convention as the widget path.
//
// Every case is run twice by CTest: once at the platform's own ratio and once
// under QT_SCALE_FACTOR=2, so the DPR arithmetic is exercised by real numbers.

#include "api/error_codes.h"
#include "api/native_mode_api.h"
#include "common/image_matchers.h"
#include "common/qt_matchers.h"
#include "core/object_registry.h"
#include "transport/jsonrpc_handler.h"

#include <memory>

#include <QApplication>
#include <QJsonDocument>
#include <QPalette>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QWidget>
#include <QtTest>

#ifdef QTPILOT_TEST_HAS_QUICKWIDGETS
#include <QQuickWidget>
#endif

using namespace qtPilot;
using namespace qtPilot::test;

namespace {

const QColor kWhite(0xff, 0xff, 0xff);
const QColor kRed(0xff, 0x00, 0x00);
const QColor kGreen(0x00, 0xff, 0x00);
const QColor kBlue(0x00, 0x00, 0xff);

/// A white 200x150 window holding one item of each shape under test.
///
///   redBox       (20, 30)  60x40   solid red
///   nestedBlue   (110, 30) 30x30   blue, inside an offset parent, with a
///                                  10x10 green square in its top-left corner
///   scaledGreen  (20, 90)  20x20   green, scale 2 about its top-left, so it
///                                  covers (20, 90) 40x40 on screen
///   hiddenBox    visible: false
///   emptyItem    zero width and height
constexpr char kSceneQml[] = R"(
import QtQuick 2.15
import QtQuick.Window 2.15

Window {
  objectName: "screenshotWindow"
  width: 200
  height: 150
  color: "#ffffff"

  Rectangle { objectName: "redBox"; x: 20; y: 30; width: 60; height: 40; color: "#ff0000" }

  Item {
    x: 100; y: 10
    Rectangle {
      objectName: "nestedBlue"; x: 10; y: 20; width: 30; height: 30; color: "#0000ff"
      Rectangle { x: 0; y: 0; width: 10; height: 10; color: "#00ff00" }
    }
  }

  Rectangle {
    objectName: "scaledGreen"; x: 20; y: 90; width: 20; height: 20; color: "#00ff00"
    scale: 2; transformOrigin: Item.TopLeft
  }

  Rectangle { objectName: "hiddenBox"; x: 150; y: 100; width: 20; height: 20; visible: false }
  Item { objectName: "emptyItem"; x: 5; y: 5 }
}
)";

/// The content of a QQuickView / QQuickWidget host: a root item, not a Window.
constexpr char kHostedQml[] = R"(
import QtQuick 2.15

Rectangle {
  width: 120
  height: 80
  color: "#ffffff"
  Rectangle { objectName: "hostedRed"; x: 10; y: 20; width: 50; height: 30; color: "#ff0000" }
}
)";

/// @brief Logical size to the physical pixels a capture at @p dpr has.
QSize physical(const QSizeF& logical, qreal dpr) {
  return QSize(qRound(logical.width() * dpr), qRound(logical.height() * dpr));
}

/// @brief Logical point to the pixel a capture at @p dpr has it at.
QPoint physical(const QPointF& logical, qreal dpr) {
  return QPoint(qRound(logical.x() * dpr), qRound(logical.y() * dpr));
}

/// @brief A successful qt.ui.screenshot whose image satisfies @p imageMatcher.
template <typename ImageMatcher>
auto IsScreenshot(ImageMatcher&& imageMatcher) {
  // The JSON-RPC result is the response envelope; the capture is its own `result`.
  return IsJsonRpcSuccess(
      HasJsonField("result", DecodesToImage(std::forward<ImageMatcher>(imageMatcher))));
}

/// @brief A Quick window built from QML, torn down with the test.
class QuickScene {
 public:
  enum class Shown { Yes, No };

  explicit QuickScene(const char* qml, Shown shown = Shown::Yes) {
    QQmlComponent component(&m_engine);
    component.setData(QByteArray(qml), QUrl());
    m_window.reset(qobject_cast<QQuickWindow*>(component.create()));
    if (!m_window) {
      qFatal("QuickScene: QML did not create a window: %s", qPrintable(component.errorString()));
    }
    if (shown == Shown::Yes) {
      m_window->show();
      if (!QTest::qWaitForWindowExposed(m_window.get())) {
        qWarning("QuickScene: window was shown but never exposed");
      }
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

  qreal dpr() const { return m_window->effectiveDevicePixelRatio(); }

 private:
  QQmlEngine m_engine;
  std::unique_ptr<QQuickWindow> m_window;
};

}  // namespace

class TestUiScreenshotQuick : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase();
  void cleanupTestCase();

  // Windows
  void testQuickWindowCapturesTheWholeScene();
  void testQuickWindowRegionIsInWindowCoordinates();

  // Items
  void testItemIsCroppedToItsSceneRect();
  void testNestedItemIsCroppedToItsSceneRect();
  void testTransformedItemIsCroppedToItsTransformedBounds();
  void testItemRegionIsInItemCoordinates();
  void testItemFullWindowCapturesItsWindow();
  void testItemInUnexposedWindowIsRenderedOffscreen();

  // Hosts
  void testItemInQuickViewIsCaptured();
  void testQuickWidgetIsCapturedAsAWidget();
  void testItemInQuickWidgetIsCaptured();

  // Widgets are unchanged
  void testWidgetCaptureIsUnchanged();

  // Errors
  void testInvisibleItemIsRejected();
  void testZeroSizeItemIsRejected();
  void testItemNotOnAWindowIsRejected();
  void testNonVisualObjectIsRejected();
  void testDestroyedItemIsNotFound();

 private:
  /// @brief qt.ui.screenshot of @p target, with any further @p params.
  QJsonObject screenshot(QObject* target, QJsonObject params = {});
  /// @brief qt.ui.screenshot with exactly @p params.
  QJsonObject callScreenshot(const QJsonObject& params);

  std::unique_ptr<JsonRpcHandler> m_handler;
  std::unique_ptr<NativeModeApi> m_api;
  int m_requestId = 1;
};

void TestUiScreenshotQuick::initTestCase() {
  installObjectHooks();
  m_handler = std::make_unique<JsonRpcHandler>();
  m_api = std::make_unique<NativeModeApi>(m_handler.get());
}

void TestUiScreenshotQuick::cleanupTestCase() {
  m_api.reset();
  m_handler.reset();
  uninstallObjectHooks();
}

QJsonObject TestUiScreenshotQuick::screenshot(QObject* target, QJsonObject params) {
  params[QStringLiteral("objectId")] = ObjectRegistry::instance()->objectId(target);
  return callScreenshot(params);
}

QJsonObject TestUiScreenshotQuick::callScreenshot(const QJsonObject& params) {
  const QJsonObject request{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                            {QStringLiteral("method"), QStringLiteral("qt.ui.screenshot")},
                            {QStringLiteral("params"), params},
                            {QStringLiteral("id"), m_requestId++}};
  const QString response = m_handler->HandleMessage(
      QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact)));
  return QJsonDocument::fromJson(response.toUtf8()).object();
}

// --- Windows ----------------------------------------------------------------

void TestUiScreenshotQuick::testQuickWindowCapturesTheWholeScene() {
  QuickScene scene(kSceneQml);
  const qreal dpr = scene.dpr();

  QEXPECT_THAT(screenshot(scene.window()),
               IsScreenshot(AllOf(ImageSizeIs(physical(QSizeF(200, 150), dpr)),
                                  PixelAt(physical(QPointF(50, 50), dpr), ColorNear(kRed)),
                                  PixelAt(physical(QPointF(125, 45), dpr), ColorNear(kBlue)),
                                  PixelAt(physical(QPointF(190, 10), dpr), ColorNear(kWhite)))));
}

void TestUiScreenshotQuick::testQuickWindowRegionIsInWindowCoordinates() {
  QuickScene scene(kSceneQml);
  const qreal dpr = scene.dpr();

  // Exactly the red box, addressed in window coordinates.
  const QJsonObject region{{"x", 20}, {"y", 30}, {"width", 60}, {"height", 40}};
  QEXPECT_THAT(
      screenshot(scene.window(), {{"region", region}}),
      IsScreenshot(AllOf(ImageSizeIs(physical(QSizeF(60, 40), dpr)), AllPixels(ColorNear(kRed)))));
}

// --- Items ------------------------------------------------------------------

void TestUiScreenshotQuick::testItemIsCroppedToItsSceneRect() {
  QuickScene scene(kSceneQml);
  const qreal dpr = scene.dpr();

  // Every pixel red: a crop off by one row or column would pick up white.
  QEXPECT_THAT(
      screenshot(scene.item("redBox")),
      IsScreenshot(AllOf(ImageSizeIs(physical(QSizeF(60, 40), dpr)), AllPixels(ColorNear(kRed)))));
}

void TestUiScreenshotQuick::testNestedItemIsCroppedToItsSceneRect() {
  QuickScene scene(kSceneQml);
  const qreal dpr = scene.dpr();

  // Item-local (0,0) must land on the green corner, so the crop used the
  // item's scene position (110, 30), not its parent-relative (10, 20).
  QEXPECT_THAT(screenshot(scene.item("nestedBlue")),
               IsScreenshot(AllOf(ImageSizeIs(physical(QSizeF(30, 30), dpr)),
                                  PixelAt(physical(QPointF(2, 2), dpr), ColorNear(kGreen)),
                                  PixelAt(physical(QPointF(20, 20), dpr), ColorNear(kBlue)),
                                  PixelAt(physical(QPointF(28, 2), dpr), ColorNear(kBlue)))));
}

void TestUiScreenshotQuick::testTransformedItemIsCroppedToItsTransformedBounds() {
  QuickScene scene(kSceneQml);
  const qreal dpr = scene.dpr();

  // 20x20 at scale 2: what is on screen is 40x40, all of it green.
  QEXPECT_THAT(screenshot(scene.item("scaledGreen")),
               IsScreenshot(AllOf(ImageSizeIs(physical(QSizeF(40, 40), dpr)),
                                  AllPixels(ColorNear(kGreen)))));
}

void TestUiScreenshotQuick::testItemRegionIsInItemCoordinates() {
  QuickScene scene(kSceneQml);
  const qreal dpr = scene.dpr();

  // Like a widget's region: relative to the target, so (0,0) is the corner of
  // the nested item -- the green square -- and not the window's corner.
  const QJsonObject region{{"x", 0}, {"y", 0}, {"width", 10}, {"height", 10}};
  QEXPECT_THAT(screenshot(scene.item("nestedBlue"), {{"region", region}}),
               IsScreenshot(AllOf(ImageSizeIs(physical(QSizeF(10, 10), dpr)),
                                  AllPixels(ColorNear(kGreen)))));
}

void TestUiScreenshotQuick::testItemFullWindowCapturesItsWindow() {
  QuickScene scene(kSceneQml);
  const qreal dpr = scene.dpr();

  QEXPECT_THAT(screenshot(scene.item("redBox"), {{"fullWindow", true}}),
               IsScreenshot(AllOf(ImageSizeIs(physical(QSizeF(200, 150), dpr)),
                                  PixelAt(physical(QPointF(5, 5), dpr), ColorNear(kWhite)),
                                  PixelAt(physical(QPointF(50, 50), dpr), ColorNear(kRed)))));
}

void TestUiScreenshotQuick::testItemInUnexposedWindowIsRenderedOffscreen() {
  // Never shown, so never exposed: there is nothing on any screen to grab, and
  // a screen grab would capture whatever lies under it. The scene graph can
  // still render it offscreen, which is what a headless driver needs.
  QuickScene scene(kSceneQml, QuickScene::Shown::No);
  QVERIFY(!scene.window()->isExposed());
  const qreal dpr = scene.dpr();

  QEXPECT_THAT(
      screenshot(scene.item("redBox")),
      IsScreenshot(AllOf(ImageSizeIs(physical(QSizeF(60, 40), dpr)), AllPixels(ColorNear(kRed)))));
}

// --- Hosts ------------------------------------------------------------------

void TestUiScreenshotQuick::testItemInQuickViewIsCaptured() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.filePath(QStringLiteral("Hosted.qml"));
  {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(kHostedQml);
  }

  QQuickView view;
  view.setSource(QUrl::fromLocalFile(path));
  QCOMPARE(view.status(), QQuickView::Ready);
  view.show();
  QVERIFY(QTest::qWaitForWindowExposed(&view));
  const qreal dpr = view.effectiveDevicePixelRatio();

  auto* item = view.rootObject()->findChild<QQuickItem*>(QStringLiteral("hostedRed"));
  QVERIFY(item);
  QEXPECT_THAT(screenshot(item), IsScreenshot(AllOf(ImageSizeIs(physical(QSizeF(50, 30), dpr)),
                                                    AllPixels(ColorNear(kRed)))));
}

void TestUiScreenshotQuick::testQuickWidgetIsCapturedAsAWidget() {
#ifndef QTPILOT_TEST_HAS_QUICKWIDGETS
  QSKIP("Qt Quick Widgets is not available in this Qt build");
#else
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.filePath(QStringLiteral("Hosted.qml"));
  {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(kHostedQml);
  }

  QQuickWidget host;
  host.setResizeMode(QQuickWidget::SizeRootObjectToView);
  host.setSource(QUrl::fromLocalFile(path));
  QCOMPARE(host.status(), QQuickWidget::Ready);
  host.resize(120, 80);
  host.show();
  QVERIFY(QTest::qWaitForWindowExposed(&host));
  const qreal dpr = host.devicePixelRatioF();

  QEXPECT_THAT(screenshot(&host),
               IsScreenshot(AllOf(ImageSizeIs(physical(QSizeF(120, 80), dpr)),
                                  PixelAt(physical(QPointF(30, 30), dpr), ColorNear(kRed)),
                                  PixelAt(physical(QPointF(100, 70), dpr), ColorNear(kWhite)))));
#endif
}

void TestUiScreenshotQuick::testItemInQuickWidgetIsCaptured() {
#ifndef QTPILOT_TEST_HAS_QUICKWIDGETS
  QSKIP("Qt Quick Widgets is not available in this Qt build");
#else
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.filePath(QStringLiteral("Hosted.qml"));
  {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(kHostedQml);
  }

  QQuickWidget host;
  host.setResizeMode(QQuickWidget::SizeRootObjectToView);
  host.setSource(QUrl::fromLocalFile(path));
  QCOMPARE(host.status(), QQuickWidget::Ready);
  host.resize(120, 80);
  host.show();
  QVERIFY(QTest::qWaitForWindowExposed(&host));
  const qreal dpr = host.devicePixelRatioF();

  // The item's window is the QQuickWidget's internal offscreen QQuickWindow,
  // which is never itself exposed.
  auto* item = host.rootObject()->findChild<QQuickItem*>(QStringLiteral("hostedRed"));
  QVERIFY(item);
  QEXPECT_THAT(screenshot(item), IsScreenshot(AllOf(ImageSizeIs(physical(QSizeF(50, 30), dpr)),
                                                    AllPixels(ColorNear(kRed)))));
#endif
}

// --- Widgets ----------------------------------------------------------------

void TestUiScreenshotQuick::testWidgetCaptureIsUnchanged() {
  QWidget widget;
  widget.setAutoFillBackground(true);
  QPalette palette = widget.palette();
  palette.setColor(QPalette::Window, kRed);
  widget.setPalette(palette);
  widget.resize(60, 40);
  widget.show();
  QVERIFY(QTest::qWaitForWindowExposed(&widget));
  const qreal dpr = widget.devicePixelRatioF();

  QEXPECT_THAT(screenshot(&widget), IsScreenshot(AllOf(ImageSizeIs(physical(QSizeF(60, 40), dpr)),
                                                       AllPixels(ColorNear(kRed)))));
}

// --- Errors -----------------------------------------------------------------

void TestUiScreenshotQuick::testInvisibleItemIsRejected() {
  QuickScene scene(kSceneQml);

  QEXPECT_THAT(screenshot(scene.item("hiddenBox")),
               IsJsonRpcError(ErrorCode::kWidgetNotVisible, QStrContains("not visible")));
}

void TestUiScreenshotQuick::testZeroSizeItemIsRejected() {
  QuickScene scene(kSceneQml);

  QEXPECT_THAT(screenshot(scene.item("emptyItem")),
               IsJsonRpcError(ErrorCode::kWidgetNotVisible, QStrContains("zero size")));
}

void TestUiScreenshotQuick::testItemNotOnAWindowIsRejected() {
  QQuickItem item;
  item.setSize(QSizeF(10, 10));

  QEXPECT_THAT(screenshot(&item),
               IsJsonRpcError(ErrorCode::kWidgetNotVisible, QStrContains("not on a window")));
}

void TestUiScreenshotQuick::testNonVisualObjectIsRejected() {
  QObject object;
  object.setObjectName(QStringLiteral("nonVisualScreenshotTarget"));

  QEXPECT_THAT(screenshot(&object),
               IsJsonRpcError(ErrorCode::kObjectNotWidget,
                              QStrContains("not a widget, Qt Quick window, or Qt Quick item")));
}

void TestUiScreenshotQuick::testDestroyedItemIsNotFound() {
  auto* item = new QQuickItem();
  const QString id = ObjectRegistry::instance()->objectId(item);
  delete item;

  const QJsonObject response = callScreenshot(QJsonObject{{QStringLiteral("objectId"), id}});
  QEXPECT_THAT(response, IsJsonRpcError(ErrorCode::kObjectNotFound));
}

QTEST_MAIN(TestUiScreenshotQuick)
#include "test_ui_screenshot_quick.moc"
