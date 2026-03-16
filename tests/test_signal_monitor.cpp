// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "core/object_registry.h"
#include "introspection/signal_monitor.h"

#include <QApplication>
#include <QPushButton>
#include <QSignalSpy>
#include <QtTest>

using namespace qtPilot;

/// @brief Unit tests for SignalMonitor class.
///
/// Tests signal subscription, notification emission, auto-unsubscribe on
/// object destruction, and lifecycle event notifications.
class TestSignalMonitor : public QObject {
  Q_OBJECT

 private Q_SLOTS:
  void initTestCase();
  void cleanupTestCase();
  void init();
  void cleanup();

  void testSubscribeReturnsValidId();
  void testUnsubscribe();
  void testSignalEmission();
  void testAutoUnsubscribeOnDestruction();
  void testLifecycleCreated();
  void testLifecycleDestroyed();
  void testSubscribeNonexistentObject();
  void testSubscribeNonexistentSignal();

 private:
  // Track objects created during tests for cleanup
  QList<QObject*> m_testObjects;
};

void TestSignalMonitor::initTestCase() {
  // Install object hooks for tracking
  installObjectHooks();
}

void TestSignalMonitor::cleanupTestCase() {
  // Uninstall hooks
  uninstallObjectHooks();
}

void TestSignalMonitor::init() {
  // Disable lifecycle notifications by default
  SignalMonitor::instance()->setLifecycleNotificationsEnabled(false);
}

void TestSignalMonitor::cleanup() {
  // Delete test objects
  qDeleteAll(m_testObjects);
  m_testObjects.clear();

  // Process events to ensure all queued signals are delivered
  QCoreApplication::processEvents();
}

void TestSignalMonitor::testSubscribeReturnsValidId() {
  // Create a button
  auto* btn = new QPushButton();
  btn->setObjectName("testBtn");
  m_testObjects.append(btn);

  // Process events to ensure the object is registered
  QCoreApplication::processEvents();

  // Get object ID
  QString objId = ObjectRegistry::instance()->objectId(btn);
  QVERIFY2(!objId.isEmpty(), "Object ID should not be empty");

  // Subscribe to clicked signal
  QString subId;
  try {
    subId = SignalMonitor::instance()->subscribe(objId, "clicked");
  } catch (const std::exception& e) {
    QFAIL(qPrintable(QString("subscribe threw: %1").arg(e.what())));
  }

  // Verify subscription ID format
  QVERIFY2(subId.startsWith("sub_"), "Subscription ID should start with 'sub_'");

  // Verify subscription count increased
  QVERIFY(SignalMonitor::instance()->subscriptionCount() >= 1);

  // Cleanup
  SignalMonitor::instance()->unsubscribe(subId);
}

void TestSignalMonitor::testUnsubscribe() {
  auto* btn = new QPushButton();
  btn->setObjectName("unsubBtn");
  m_testObjects.append(btn);

  QCoreApplication::processEvents();

  QString objId = ObjectRegistry::instance()->objectId(btn);
  QString subId = SignalMonitor::instance()->subscribe(objId, "clicked");

  int countBefore = SignalMonitor::instance()->subscriptionCount();
  QVERIFY(countBefore >= 1);

  // Unsubscribe
  SignalMonitor::instance()->unsubscribe(subId);

  int countAfter = SignalMonitor::instance()->subscriptionCount();
  QCOMPARE(countAfter, countBefore - 1);
}

void TestSignalMonitor::testSignalEmission() {
  auto* btn = new QPushButton();
  btn->setObjectName("emitBtn");
  m_testObjects.append(btn);

  QCoreApplication::processEvents();

  QString objId = ObjectRegistry::instance()->objectId(btn);
  QString subId = SignalMonitor::instance()->subscribe(objId, "clicked");

  // Set up spy for signalEmitted
  QSignalSpy spy(SignalMonitor::instance(), &SignalMonitor::signalEmitted);
  QVERIFY(spy.isValid());

  // Trigger the signal
  btn->click();

  // Verify notification was emitted
  QCOMPARE(spy.count(), 1);

  // Verify notification content
  QJsonObject notification = spy.at(0).at(0).toJsonObject();
  QCOMPARE(notification["subscriptionId"].toString(), subId);
  QCOMPARE(notification["objectId"].toString(), objId);
  QCOMPARE(notification["signal"].toString(), QString("clicked"));
  QVERIFY(notification.contains("arguments"));

  // Cleanup
  SignalMonitor::instance()->unsubscribe(subId);
}

void TestSignalMonitor::testAutoUnsubscribeOnDestruction() {
  auto* btn = new QPushButton();
  btn->setObjectName("destroyBtn");
  // Don't add to m_testObjects - we'll delete manually

  QCoreApplication::processEvents();

  QString objId = ObjectRegistry::instance()->objectId(btn);
  QString subId = SignalMonitor::instance()->subscribe(objId, "clicked");

  int countBefore = SignalMonitor::instance()->subscriptionCount();
  QVERIFY(countBefore >= 1);

  // Delete the object
  delete btn;

  // Process events to allow cleanup
  QCoreApplication::processEvents();

  // Subscription should have been auto-removed
  int countAfter = SignalMonitor::instance()->subscriptionCount();
  QCOMPARE(countAfter, countBefore - 1);
}

void TestSignalMonitor::testLifecycleCreated() {
  // Enable lifecycle notifications
  SignalMonitor::instance()->setLifecycleNotificationsEnabled(true);

  // Set up spy
  QSignalSpy spy(SignalMonitor::instance(), &SignalMonitor::objectCreated);
  QVERIFY(spy.isValid());

  // Create an object
  auto* obj = new QObject();
  obj->setObjectName("lifecycleTest");
  m_testObjects.append(obj);

  // Process events - objectCreated is emitted via QueuedConnection
  QCoreApplication::processEvents();

  // Should have received at least one objectCreated notification
  // (might get more from internal Qt objects)
  QVERIFY2(spy.count() >= 1,
           qPrintable(QString("Expected >= 1 created events, got %1").arg(spy.count())));

  // Find our notification
  bool found = false;
  for (int i = 0; i < spy.count(); ++i) {
    QJsonObject notification = spy.at(i).at(0).toJsonObject();
    if (notification["className"].toString() == "QObject") {
      QCOMPARE(notification["event"].toString(), QString("created"));
      found = true;
      break;
    }
  }
  QVERIFY2(found, "Should have received created notification for QObject");
}

void TestSignalMonitor::testLifecycleDestroyed() {
  // Enable lifecycle notifications
  SignalMonitor::instance()->setLifecycleNotificationsEnabled(true);

  // For lifecycle destroyed notifications to contain the objectId, we need
  // to subscribe to the object first (so SignalMonitor caches the ID).
  auto* obj = new QPushButton();
  obj->setObjectName("destroyLifecycleTest");
  // Don't add to m_testObjects - we'll delete manually

  QCoreApplication::processEvents();

  QString objId = ObjectRegistry::instance()->objectId(obj);

  // Subscribe so the objectId gets cached in SignalMonitor
  QString subId = SignalMonitor::instance()->subscribe(objId, "clicked");

  // Set up spy
  QSignalSpy spy(SignalMonitor::instance(), &SignalMonitor::objectDestroyed);
  QVERIFY(spy.isValid());

  // Delete the object
  delete obj;

  // Process events
  QCoreApplication::processEvents();

  // Should have received at least one objectDestroyed notification
  QVERIFY2(spy.count() >= 1,
           qPrintable(QString("Expected >= 1 destroyed events, got %1").arg(spy.count())));

  // Find our notification (with cached objectId since we subscribed)
  bool found = false;
  for (int i = 0; i < spy.count(); ++i) {
    QJsonObject notification = spy.at(i).at(0).toJsonObject();
    if (notification["objectId"].toString() == objId) {
      QCOMPARE(notification["event"].toString(), QString("destroyed"));
      found = true;
      break;
    }
  }
  QVERIFY2(found, "Should have received destroyed notification for our object");
}

void TestSignalMonitor::testSubscribeNonexistentObject() {
  bool threw = false;
  try {
    SignalMonitor::instance()->subscribe("nonexistent/object/id", "clicked");
  } catch (const std::runtime_error& e) {
    threw = true;
    QString msg = QString::fromStdString(e.what());
    QVERIFY2(msg.contains("Object not found"), qPrintable(msg));
  }
  QVERIFY2(threw, "Should throw when object not found");
}

void TestSignalMonitor::testSubscribeNonexistentSignal() {
  auto* btn = new QPushButton();
  btn->setObjectName("noSignalBtn");
  m_testObjects.append(btn);

  QCoreApplication::processEvents();

  QString objId = ObjectRegistry::instance()->objectId(btn);

  bool threw = false;
  try {
    SignalMonitor::instance()->subscribe(objId, "nonexistentSignal");
  } catch (const std::runtime_error& e) {
    threw = true;
    QString msg = QString::fromStdString(e.what());
    QVERIFY2(msg.contains("Signal not found"), qPrintable(msg));
  }
  QVERIFY2(threw, "Should throw when signal not found");
}

QTEST_MAIN(TestSignalMonitor)
#include "test_signal_monitor.moc"
