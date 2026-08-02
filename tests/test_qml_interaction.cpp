// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT
//
// Covers the QML routing for qtpilot.getGeometry / hitTest / click / sendKeys.
// Before this, all four qobject_cast<QWidget*> and rejected every QQuickItem
// with "Object is not a widget", leaving pure Qt Quick apps unable to be
// measured or driven through the native surface.

#include "api/computer_use_mode_api.h"
#include "core/object_registry.h"
#include "introspection/event_capture.h"
#include "transport/jsonrpc_handler.h"

#include <QGuiApplication>
#include <QJsonDocument>
#include <QMouseEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtTest>

#include <memory>

using namespace qtPilot;

namespace {

class DeleteOnKeyItem final : public QQuickItem {
 protected:
  bool event(QEvent* event) override {
    const bool handled = QQuickItem::event(event);
    if (event->type() == QEvent::KeyPress) {
      deleteLater();
    }
    return handled;
  }
};

}  // namespace

class TestQmlInteraction : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase();
  void cleanupTestCase();

  void testGeometryOfQuickItem();
  void testGeometryOfQuickWindow();
  void testGeometryRejectsNonVisualObject();
  void testGeometryOfUnrenderedItemHasNullGlobal();

  void testHitTestFindsItemInWindow();
  void testHitTestRejectsNonVisualParent();

  void testClickOnQuickItemIsAccepted();
  void testClickOnQuickWindowIsAccepted();
  void testClickRejectsNonVisualObject();
  void testClickOnUnattachedItemIsRejected();

  void testSendKeysOnQuickItemIsAccepted();
  void testSendKeysRejectsNonVisualObject();
  void testSendKeysStopsWhenItemIsDestroyedByText();
  void testComputerUseKeyActivatesQuickShortcuts();

  void testEventCaptureSeesQuickTargets();
  void testEventCaptureIgnoresNonVisualObjects();
  void testEventCaptureDoesNotDoubleReportQuickInput();
  void testEventCaptureSeesTapHandlerInput();

  void testHitTestOutsideSceneReportsMiss();
  void testSendKeysWithoutTextOrSequenceIsRejected();
  void testHitTestRespectsZOrder();

 private:
  QJsonObject call(const QString& method, const QJsonObject& params);
  QJsonObject callWithId(const QString& method, const QString& id);
  static QString errorOf(const QJsonObject& response);

  int m_requestId = 1;
};

void TestQmlInteraction::initTestCase() {
  installObjectHooks();
}

void TestQmlInteraction::cleanupTestCase() {
  uninstallObjectHooks();
}

QJsonObject TestQmlInteraction::call(const QString& method, const QJsonObject& params) {
  JsonRpcHandler handler;
  QJsonObject request{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                      {QStringLiteral("method"), method},
                      {QStringLiteral("params"), params},
                      {QStringLiteral("id"), m_requestId++}};
  return QJsonDocument::fromJson(handler
                                     .HandleMessage(QString::fromUtf8(
                                         QJsonDocument(request).toJson(QJsonDocument::Compact)))
                                     .toUtf8())
      .object();
}

QJsonObject TestQmlInteraction::callWithId(const QString& method, const QString& id) {
  return call(method, QJsonObject{{QStringLiteral("id"), id}});
}

QString TestQmlInteraction::errorOf(const QJsonObject& response) {
  return response[QStringLiteral("error")].toObject()[QStringLiteral("message")].toString();
}

// --- getGeometry ------------------------------------------------------------

void TestQmlInteraction::testGeometryOfQuickItem() {
  QQuickWindow window;
  window.setGeometry(0, 0, 200, 150);
  auto* item = new QQuickItem(window.contentItem());
  item->setObjectName(QStringLiteral("geometryItem"));
  item->setPosition(QPointF(20, 30));
  item->setSize(QSizeF(60, 40));
  window.show();
  QCoreApplication::processEvents();

  const QJsonObject result =
      callWithId(QStringLiteral("qtpilot.getGeometry"),
                 ObjectRegistry::instance()->objectId(item))[QStringLiteral("result")]
          .toObject();
  QVERIFY2(!result.isEmpty(), "getGeometry returned no result for a QQuickItem");

  const QJsonObject local = result[QStringLiteral("local")].toObject();
  QCOMPARE(local[QStringLiteral("x")].toDouble(), 20.0);
  QCOMPARE(local[QStringLiteral("y")].toDouble(), 30.0);
  QCOMPARE(local[QStringLiteral("width")].toDouble(), 60.0);
  QCOMPARE(local[QStringLiteral("height")].toDouble(), 40.0);

  // Scene coords are what Qt Quick input events use, so they must be present
  // and must account for the item's offset within the scene.
  const QJsonObject scene = result[QStringLiteral("scene")].toObject();
  QCOMPARE(scene[QStringLiteral("x")].toDouble(), 20.0);
  QCOMPARE(scene[QStringLiteral("y")].toDouble(), 30.0);
  QCOMPARE(scene[QStringLiteral("width")].toDouble(), 60.0);
}

void TestQmlInteraction::testGeometryOfQuickWindow() {
  QQuickWindow window;
  window.setObjectName(QStringLiteral("geometryWindow"));
  window.setGeometry(0, 0, 240, 180);
  window.show();
  QCoreApplication::processEvents();

  const QJsonObject result =
      callWithId(QStringLiteral("qtpilot.getGeometry"),
                 ObjectRegistry::instance()->objectId(&window))[QStringLiteral("result")]
          .toObject();
  QVERIFY2(!result.isEmpty(), "getGeometry returned no result for a QQuickWindow");

  // A window's local rect sits at the origin -- it has no parent to offset from.
  const QJsonObject local = result[QStringLiteral("local")].toObject();
  QCOMPARE(local[QStringLiteral("x")].toDouble(), 0.0);
  QCOMPARE(local[QStringLiteral("y")].toDouble(), 0.0);
  QCOMPARE(local[QStringLiteral("width")].toDouble(), 240.0);
  QCOMPARE(local[QStringLiteral("height")].toDouble(), 180.0);
  QVERIFY(result.contains(QStringLiteral("devicePixelRatio")));
}

void TestQmlInteraction::testGeometryRejectsNonVisualObject() {
  QObject object;
  object.setObjectName(QStringLiteral("nonVisualGeometryObject"));
  const QString message = errorOf(callWithId(QStringLiteral("qtpilot.getGeometry"),
                                             ObjectRegistry::instance()->objectId(&object)));
  QVERIFY2(message.contains(QStringLiteral("not a widget, window, or QML item")),
           qPrintable(message));
}

void TestQmlInteraction::testGeometryOfUnrenderedItemHasNullGlobal() {
  // An item with no window has valid local/scene coords but maps to no screen
  // position; reporting 0,0 would be a lie the caller could act on.
  QQuickItem item;
  item.setObjectName(QStringLiteral("unrenderedGeometryItem"));
  item.setSize(QSizeF(10, 10));

  const QJsonObject result =
      callWithId(QStringLiteral("qtpilot.getGeometry"),
                 ObjectRegistry::instance()->objectId(&item))[QStringLiteral("result")]
          .toObject();
  QVERIFY2(!result.isEmpty(), "getGeometry returned no result for an unrendered QQuickItem");
  QVERIFY(result[QStringLiteral("global")].isNull());
}

// --- hitTest ----------------------------------------------------------------

void TestQmlInteraction::testHitTestFindsItemInWindow() {
  QQuickWindow window;
  window.setGeometry(0, 0, 200, 200);
  auto* item = new QQuickItem(window.contentItem());
  item->setObjectName(QStringLiteral("hitTargetItem"));
  item->setPosition(QPointF(50, 50));
  item->setSize(QSizeF(80, 60));
  window.show();
  QCoreApplication::processEvents();

  const QString expected = ObjectRegistry::instance()->objectId(item);
  // Scene coords: a point inside the item's rect.
  const QJsonObject result =
      call(QStringLiteral("qtpilot.hitTest"),
           QJsonObject{{QStringLiteral("parentId"), ObjectRegistry::instance()->objectId(&window)},
                       {QStringLiteral("x"), 70},
                       {QStringLiteral("y"), 70}})[QStringLiteral("result")]
          .toObject();

  QCOMPARE(result[QStringLiteral("id")].toString(), expected);
}

void TestQmlInteraction::testHitTestRejectsNonVisualParent() {
  QObject object;
  object.setObjectName(QStringLiteral("nonVisualHitParent"));
  const QString message = errorOf(
      call(QStringLiteral("qtpilot.hitTest"),
           QJsonObject{{QStringLiteral("parentId"), ObjectRegistry::instance()->objectId(&object)},
                       {QStringLiteral("x"), 1},
                       {QStringLiteral("y"), 1}}));
  QVERIFY2(message.contains(QStringLiteral("not a widget, window, or QML item")),
           qPrintable(message));
}

// --- click ------------------------------------------------------------------

void TestQmlInteraction::testClickOnQuickItemIsAccepted() {
  QQuickWindow window;
  window.setGeometry(0, 0, 200, 150);
  auto* item = new QQuickItem(window.contentItem());
  item->setObjectName(QStringLiteral("clickItem"));
  item->setPosition(QPointF(10, 10));
  item->setSize(QSizeF(50, 50));
  window.show();
  QCoreApplication::processEvents();

  const QJsonObject response =
      callWithId(QStringLiteral("qtpilot.click"), ObjectRegistry::instance()->objectId(item));
  QVERIFY2(!response.contains(QStringLiteral("error")), qPrintable(errorOf(response)));
  QVERIFY(response[QStringLiteral("result")].toObject()[QStringLiteral("success")].toBool());
}

void TestQmlInteraction::testClickOnQuickWindowIsAccepted() {
  QQuickWindow window;
  window.setObjectName(QStringLiteral("clickWindow"));
  window.setGeometry(0, 0, 200, 150);
  window.show();
  QCoreApplication::processEvents();

  const QJsonObject response =
      callWithId(QStringLiteral("qtpilot.click"), ObjectRegistry::instance()->objectId(&window));
  QVERIFY2(!response.contains(QStringLiteral("error")), qPrintable(errorOf(response)));
  QVERIFY(response[QStringLiteral("result")].toObject()[QStringLiteral("success")].toBool());
}

void TestQmlInteraction::testClickRejectsNonVisualObject() {
  QObject object;
  object.setObjectName(QStringLiteral("nonVisualClickObject"));
  const QString message = errorOf(
      callWithId(QStringLiteral("qtpilot.click"), ObjectRegistry::instance()->objectId(&object)));
  QVERIFY2(message.contains(QStringLiteral("not a widget, window, or QML item")),
           qPrintable(message));
}

void TestQmlInteraction::testClickOnUnattachedItemIsRejected() {
  QQuickItem item;
  item.setObjectName(QStringLiteral("unattachedClickItem"));
  const QString message = errorOf(
      callWithId(QStringLiteral("qtpilot.click"), ObjectRegistry::instance()->objectId(&item)));
  QVERIFY2(message.contains(QStringLiteral("not on a window")), qPrintable(message));
}

// --- sendKeys ---------------------------------------------------------------

void TestQmlInteraction::testSendKeysOnQuickItemIsAccepted() {
  QQuickWindow window;
  window.setGeometry(0, 0, 200, 150);
  auto* item = new QQuickItem(window.contentItem());
  item->setObjectName(QStringLiteral("keysItem"));
  item->setSize(QSizeF(50, 50));
  item->setFlag(QQuickItem::ItemIsFocusScope);
  window.show();
  QCoreApplication::processEvents();

  const QJsonObject response =
      call(QStringLiteral("qtpilot.sendKeys"),
           QJsonObject{{QStringLiteral("id"), ObjectRegistry::instance()->objectId(item)},
                       {QStringLiteral("text"), QStringLiteral("hi")}});
  QVERIFY2(!response.contains(QStringLiteral("error")), qPrintable(errorOf(response)));
  QVERIFY(response[QStringLiteral("result")].toObject()[QStringLiteral("success")].toBool());
}

void TestQmlInteraction::testSendKeysRejectsNonVisualObject() {
  QObject object;
  object.setObjectName(QStringLiteral("nonVisualKeysObject"));
  const QString message = errorOf(
      call(QStringLiteral("qtpilot.sendKeys"),
           QJsonObject{{QStringLiteral("id"), ObjectRegistry::instance()->objectId(&object)},
                       {QStringLiteral("text"), QStringLiteral("x")}}));
  QVERIFY2(message.contains(QStringLiteral("not a widget, window, or QML item")),
           qPrintable(message));
}

void TestQmlInteraction::testSendKeysStopsWhenItemIsDestroyedByText() {
  QQuickWindow window;
  window.setGeometry(0, 0, 200, 150);
  auto* item = new DeleteOnKeyItem();
  item->setParentItem(window.contentItem());
  item->setObjectName(QStringLiteral("deleteOnKeyItem"));
  item->setSize(QSizeF(50, 50));
  window.show();
  QCoreApplication::processEvents();

  const QJsonObject response =
      call(QStringLiteral("qtpilot.sendKeys"),
           QJsonObject{{QStringLiteral("id"), ObjectRegistry::instance()->objectId(item)},
                       {QStringLiteral("text"), QStringLiteral("x")},
                       {QStringLiteral("sequence"), QStringLiteral("Ctrl+A")}});

  const QString message = errorOf(response);
  QVERIFY2(message.contains(QStringLiteral("destroyed while typing")), qPrintable(message));
}

void TestQmlInteraction::testComputerUseKeyActivatesQuickShortcuts() {
  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.setData(R"(
    import QtQuick 2.15

    Item {
      id: root
      property int metaPlusCount: 0
      property int controlMinusCount: 0
      property int questionMarkCount: 0
      focus: true

      Shortcut { sequence: "Meta++"; onActivated: root.metaPlusCount++ }
      Shortcut { sequence: "Ctrl+-"; onActivated: root.controlMinusCount++ }
      Shortcut { sequence: "?"; onActivated: root.questionMarkCount++ }
    }
  )",
                    QUrl());
  QVERIFY2(component.isReady(), qPrintable(component.errorString()));

  std::unique_ptr<QObject> object(component.create());
  QVERIFY2(object, qPrintable(component.errorString()));
  auto* item = qobject_cast<QQuickItem*>(object.get());
  QVERIFY(item);

  QQuickWindow window;
  window.setGeometry(0, 0, 200, 150);
  item->setParentItem(window.contentItem());
  item->setSize(QSizeF(200, 150));
  window.show();
  window.requestActivate();
  item->forceActiveFocus();
  QTRY_VERIFY(window.isActive());

  JsonRpcHandler handler;
  ComputerUseModeApi api(&handler);
  auto callComputerUseKey = [this, &handler](const QString& key) {
    const QJsonObject request{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                              {QStringLiteral("method"), QStringLiteral("cu.key")},
                              {QStringLiteral("params"), QJsonObject{{QStringLiteral("key"), key}}},
                              {QStringLiteral("id"), m_requestId++}};
    QJsonParseError parseError;
    const QJsonDocument responseDocument = QJsonDocument::fromJson(
        handler
            .HandleMessage(QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact)))
            .toUtf8(),
        &parseError);
    if (parseError.error != QJsonParseError::NoError || !responseDocument.isObject()) {
      QTest::qFail(
          qPrintable(QStringLiteral("Invalid JSON-RPC response: %1").arg(parseError.errorString())),
          __FILE__, __LINE__);
      return QJsonObject{};
    }
    return responseDocument.object();
  };

  QVERIFY(!callComputerUseKey(QStringLiteral("meta+Plus")).contains(QStringLiteral("error")));
  QTRY_COMPARE(item->property("metaPlusCount").toInt(), 1);

  QVERIFY(!callComputerUseKey(QStringLiteral("ctrl+Minus")).contains(QStringLiteral("error")));
  QTRY_COMPARE(item->property("controlMinusCount").toInt(), 1);

  QVERIFY(!callComputerUseKey(QStringLiteral("QuestionMark")).contains(QStringLiteral("error")));
  QTRY_COMPARE(item->property("questionMarkCount").toInt(), 1);
}

// --- event capture ----------------------------------------------------------

void TestQmlInteraction::testEventCaptureSeesQuickTargets() {
  // EventCapture filtered on qobject_cast<QWidget*>, so a pure Qt Quick app --
  // which has no QWidget anywhere -- produced no events at all.
  QQuickWindow window;
  window.setGeometry(0, 0, 200, 150);
  auto* item = new QQuickItem(window.contentItem());
  item->setObjectName(QStringLiteral("eventItem"));
  item->setSize(QSizeF(50, 50));
  window.show();
  QCoreApplication::processEvents();

  int captured = 0;
  auto* capture = EventCapture::instance();
  const auto conn = connect(capture, &EventCapture::eventCaptured, this,
                            [&captured](const QJsonObject&) { ++captured; });
  capture->startCapture();

  QMouseEvent press(QEvent::MouseButtonPress, QPointF(10, 10), QPointF(10, 10), Qt::LeftButton,
                    Qt::LeftButton, Qt::NoModifier);
  QCoreApplication::sendEvent(item, &press);

  capture->stopCapture();
  disconnect(conn);

  QVERIFY2(captured > 0, "EventCapture saw no events for a QQuickItem target");
}

void TestQmlInteraction::testEventCaptureIgnoresNonVisualObjects() {
  QObject plain;
  int captured = 0;
  auto* capture = EventCapture::instance();
  const auto conn = connect(capture, &EventCapture::eventCaptured, this,
                            [&captured](const QJsonObject&) { ++captured; });
  capture->startCapture();

  QMouseEvent press(QEvent::MouseButtonPress, QPointF(1, 1), QPointF(1, 1), Qt::LeftButton,
                    Qt::LeftButton, Qt::NoModifier);
  QCoreApplication::sendEvent(&plain, &press);

  capture->stopCapture();
  disconnect(conn);

  QCOMPARE(captured, 0);
}

void TestQmlInteraction::testEventCaptureDoesNotDoubleReportQuickInput() {
  // Qt Quick dispatches each input event to the QQuickWindow *and* the target
  // item. Reporting both doubles every click and leaves the consumer guessing
  // which objectId is the real target.
  QQuickWindow window;
  window.setGeometry(0, 0, 200, 150);
  auto* item = new QQuickItem(window.contentItem());
  item->setObjectName(QStringLiteral("dedupItem"));
  item->setSize(QSizeF(50, 50));
  window.show();
  QCoreApplication::processEvents();

  int captured = 0;
  auto* capture = EventCapture::instance();
  const auto conn = connect(capture, &EventCapture::eventCaptured, this,
                            [&captured](const QJsonObject&) { ++captured; });
  capture->startCapture();

  QMouseEvent press(QEvent::MouseButtonPress, QPointF(10, 10), QPointF(10, 10), Qt::LeftButton,
                    Qt::LeftButton, Qt::NoModifier);
  QCoreApplication::sendEvent(&window, &press);  // window-level dispatch
  QCoreApplication::sendEvent(item, &press);     // item-level dispatch

  capture->stopCapture();
  disconnect(conn);

  // Only the item-level dispatch is reported.
  QCOMPARE(captured, 1);
}

void TestQmlInteraction::testEventCaptureSeesTapHandlerInput() {
  QQmlEngine engine;
  QQuickWindow window;
  window.setGeometry(0, 0, 200, 150);

  QQmlComponent component(&engine);
  component.setData(R"(
    import QtQuick 2.15
    Item {
      objectName: "tapHandlerItem"
      width: 100
      height: 100
      TapHandler {}
    }
  )",
                    QUrl());
  QObject* object = component.create();
  QVERIFY2(object, qPrintable(component.errorString()));
  auto* item = qobject_cast<QQuickItem*>(object);
  QVERIFY(item);
  item->setParent(window.contentItem());
  item->setParentItem(window.contentItem());

  window.show();
  QCoreApplication::processEvents();

  int capturedMouseEvents = 0;
  int capturedItemPresses = 0;
  QStringList observedEvents;
  auto* capture = EventCapture::instance();
  const auto conn =
      connect(capture, &EventCapture::eventCaptured, this,
              [&capturedMouseEvents, &capturedItemPresses,
               &observedEvents](const QJsonObject& notification) {
                const QString type = notification[QStringLiteral("type")].toString();
                observedEvents.append(notification[QStringLiteral("objectName")].toString() +
                                      QStringLiteral(":") + type);
                if (type == QStringLiteral("MouseButtonPress") ||
                    type == QStringLiteral("MouseButtonRelease")) {
                  ++capturedMouseEvents;
                }
                if (notification[QStringLiteral("objectName")].toString() ==
                        QStringLiteral("tapHandlerItem") &&
                    type == QStringLiteral("MouseButtonPress")) {
                  ++capturedItemPresses;
                }
              });
  capture->startCapture();

  QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, QPoint(20, 20));

  capture->stopCapture();
  disconnect(conn);
  QVERIFY2(capturedMouseEvents == 2,
           qPrintable(QStringLiteral("observed: %1").arg(observedEvents.join(", "))));
  QCOMPARE(capturedItemPresses, 1);
}

void TestQmlInteraction::testHitTestOutsideSceneReportsMiss() {
  // itemAt used to fall back to the content item, so hitTest could never say
  // "nothing here" for a QML parent -- any coordinate looked like a hit.
  QQuickWindow window;
  window.setObjectName(QStringLiteral("missWindow"));
  window.setGeometry(0, 0, 200, 200);
  window.show();
  QCoreApplication::processEvents();

  const QJsonObject result =
      call(QStringLiteral("qtpilot.hitTest"),
           QJsonObject{{QStringLiteral("parentId"), ObjectRegistry::instance()->objectId(&window)},
                       {QStringLiteral("x"), -5000},
                       {QStringLiteral("y"), -5000}})[QStringLiteral("result")]
          .toObject();

  QVERIFY2(
      result[QStringLiteral("id")].isNull(),
      qPrintable(
          QStringLiteral("expected null, got: %1").arg(result[QStringLiteral("id")].toString())));
}

void TestQmlInteraction::testHitTestRespectsZOrder() {
  // childItems() is document order; Qt Quick stacks by z. A raised earlier
  // sibling must win over a later one.
  QQuickWindow window;
  window.setGeometry(0, 0, 200, 200);
  auto* raised = new QQuickItem(window.contentItem());
  raised->setObjectName(QStringLiteral("raisedItem"));
  raised->setPosition(QPointF(0, 0));
  raised->setSize(QSizeF(100, 100));
  raised->setZ(10);

  auto* later = new QQuickItem(window.contentItem());
  later->setObjectName(QStringLiteral("laterItem"));
  later->setPosition(QPointF(0, 0));
  later->setSize(QSizeF(100, 100));  // same rect, added after, but z = 0
  window.show();
  QCoreApplication::processEvents();

  const QJsonObject result =
      call(QStringLiteral("qtpilot.hitTest"),
           QJsonObject{{QStringLiteral("parentId"), ObjectRegistry::instance()->objectId(&window)},
                       {QStringLiteral("x"), 50},
                       {QStringLiteral("y"), 50}})[QStringLiteral("result")]
          .toObject();

  QCOMPARE(result[QStringLiteral("id")].toString(), ObjectRegistry::instance()->objectId(raised));
}

void TestQmlInteraction::testSendKeysWithoutTextOrSequenceIsRejected() {
  // A no-op that reports success hides a typo'd parameter name, and on the QML
  // path it would still move focus as a side effect.
  QQuickWindow window;
  window.setGeometry(0, 0, 200, 150);
  auto* item = new QQuickItem(window.contentItem());
  item->setObjectName(QStringLiteral("emptyKeysItem"));
  item->setSize(QSizeF(50, 50));
  window.show();
  QCoreApplication::processEvents();

  const QJsonObject response =
      call(QStringLiteral("qtpilot.sendKeys"),
           QJsonObject{{QStringLiteral("id"), ObjectRegistry::instance()->objectId(item)}});
  const QJsonObject error = response[QStringLiteral("error")].toObject();
  const QString message = error[QStringLiteral("message")].toString();
  QCOMPARE(error[QStringLiteral("code")].toInt(), JsonRpcError::kInvalidParams);
  QVERIFY2(message.contains(QStringLiteral("non-empty")), qPrintable(message));
}

QTEST_MAIN(TestQmlInteraction)
#include "test_qml_interaction.moc"
