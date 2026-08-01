// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "core/object_registry.h"
#include "transport/jsonrpc_handler.h"

#include <QGuiApplication>
#include <QJsonDocument>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtTest>

using namespace qtPilot;

class TestQmlScreenshot : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase();
  void cleanupTestCase();
  void testQuickWindowUsesQmlCapturePath();
  void testUnattachedQuickItemIsRejected();
  void testDestroyedQuickItemIsNotFound();
  void testNonVisualObjectIsRejected();

 private:
  QJsonObject callScreenshot(const QString& id);
  int m_requestId = 1;
};

void TestQmlScreenshot::initTestCase() {
  installObjectHooks();
}

void TestQmlScreenshot::cleanupTestCase() {
  uninstallObjectHooks();
}

QJsonObject TestQmlScreenshot::callScreenshot(const QString& id) {
  JsonRpcHandler handler;
  QJsonObject request{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                      {QStringLiteral("method"), QStringLiteral("qtpilot.screenshot")},
                      {QStringLiteral("params"), QJsonObject{{QStringLiteral("id"), id}}},
                      {QStringLiteral("id"), m_requestId++}};
  return QJsonDocument::fromJson(handler
                                     .HandleMessage(QString::fromUtf8(
                                         QJsonDocument(request).toJson(QJsonDocument::Compact)))
                                     .toUtf8())
      .object();
}

void TestQmlScreenshot::testQuickWindowUsesQmlCapturePath() {
  QQuickWindow window;
  window.setObjectName(QStringLiteral("qmlScreenshotWindow"));
  window.setGeometry(0, 0, 120, 80);
  window.show();
  QCoreApplication::processEvents();

  const QString id = ObjectRegistry::instance()->objectId(&window);
  const QJsonObject response = callScreenshot(id);
  if (response.contains(QStringLiteral("error"))) {
    const QString message =
        response[QStringLiteral("error")].toObject()[QStringLiteral("message")].toString();
    QVERIFY2(!message.contains(QStringLiteral("not a widget or QML item")), qPrintable(message));
    QVERIFY2(!message.contains(QStringLiteral("not rendered")), qPrintable(message));
    return;
  }

  const QString image =
      response[QStringLiteral("result")].toObject()[QStringLiteral("image")].toString();
  QVERIFY(QByteArray::fromBase64(image.toLatin1()).startsWith("\x89PNG"));
}

void TestQmlScreenshot::testUnattachedQuickItemIsRejected() {
  QQuickItem item;
  item.setObjectName(QStringLiteral("unattachedQuickItem"));
  const QString id = ObjectRegistry::instance()->objectId(&item);

  const QString message =
      callScreenshot(id)[QStringLiteral("error")].toObject()[QStringLiteral("message")].toString();
  QVERIFY2(message.contains(QStringLiteral("not on a window")), qPrintable(message));
}

void TestQmlScreenshot::testDestroyedQuickItemIsNotFound() {
  auto* item = new QQuickItem();
  item->setObjectName(QStringLiteral("destroyedQuickItem"));
  const QString id = ObjectRegistry::instance()->objectId(item);
  delete item;
  QCoreApplication::processEvents();

  const QString message =
      callScreenshot(id)[QStringLiteral("error")].toObject()[QStringLiteral("message")].toString();
  QVERIFY2(message.contains(QStringLiteral("Object not found")), qPrintable(message));
}

void TestQmlScreenshot::testNonVisualObjectIsRejected() {
  QObject object;
  object.setObjectName(QStringLiteral("nonVisualScreenshotObject"));
  const QString id = ObjectRegistry::instance()->objectId(&object);

  const QString message =
      callScreenshot(id)[QStringLiteral("error")].toObject()[QStringLiteral("message")].toString();
  QVERIFY2(message.contains(QStringLiteral("not a widget or QML item")), qPrintable(message));
}

QTEST_MAIN(TestQmlScreenshot)
#include "test_qml_screenshot.moc"
