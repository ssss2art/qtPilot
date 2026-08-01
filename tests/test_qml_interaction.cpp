// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT
//
// Covers the QML routing for qtpilot.getGeometry / hitTest / click / sendKeys.
// Before this, all four qobject_cast<QWidget*> and rejected every QQuickItem
// with "Object is not a widget", leaving pure Qt Quick apps unable to be
// measured or driven through the native surface.

#include "core/object_registry.h"
#include "transport/jsonrpc_handler.h"

#include <QGuiApplication>
#include <QJsonDocument>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtTest>

using namespace qtPilot;

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
  QVERIFY2(message.contains(QStringLiteral("not a widget or QML item")), qPrintable(message));
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
  QVERIFY2(message.contains(QStringLiteral("not a widget or QML item")), qPrintable(message));
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
  QVERIFY2(message.contains(QStringLiteral("not a widget or QML item")), qPrintable(message));
}

QTEST_MAIN(TestQmlInteraction)
#include "test_qml_interaction.moc"
