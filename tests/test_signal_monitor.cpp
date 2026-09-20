// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "common/qt_matchers.h"
#include "core/object_registry.h"
#include "introspection/signal_monitor.h"

#include <QApplication>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QtTest>

using namespace qtPilot;
using namespace qtPilot::test;

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
  void testSignalArgumentValues();
  void testAutoUnsubscribeOnDestruction();
  void testLifecycleCreated();
  void testLifecycleDestroyed();
  void testSubscribeNonexistentObject();
  void testSubscribeNonexistentSignal();
  void testSubscribeExpectedSuccess();
  void testSubscribeExpectedNonexistentObject();
  void testSubscribeExpectedNonexistentSignal();

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
  // Creation notifications are published only while a client is connected.
  ObjectRegistry::instance()->setClientConnected(false);

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
  QEXPECT_THAT(objId, QIsNotEmpty());

  // Subscribe to clicked signal
  QString subId;
  try {
    subId = SignalMonitor::instance()->subscribe(objId, "clicked");
  } catch (const std::exception& e) {
    QFAIL(qPrintable(QString("subscribe threw: %1").arg(e.what())));
  }

  // Verify subscription ID format
  QEXPECT_THAT(subId, QStrStartsWith("sub_"));

  // Verify subscription count increased
  QEXPECT_THAT(SignalMonitor::instance()->subscriptionCount(), Ge(1));

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
  QEXPECT_THAT(countBefore, Ge(1));

  // Unsubscribe
  SignalMonitor::instance()->unsubscribe(subId);

  int countAfter = SignalMonitor::instance()->subscriptionCount();
  QEXPECT_THAT(countAfter, Eq(countBefore - 1));
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
  QEXPECT_THAT(spy.isValid(), IsTrue());

  // Trigger the signal
  btn->click();

  // Verify notification was emitted
  QEXPECT_THAT(spy.count(), Eq(1));

  // Verify notification content
  QJsonObject notification = spy.at(0).at(0).toJsonObject();
  QEXPECT_THAT(notification, AllOf(
      HasJsonField("subscriptionId", QStrEq(subId)),
      HasJsonField("objectId", QStrEq(objId)),
      HasJsonField("signal", QStrEq("clicked")),
      HasJsonField("arguments")));
  // clicked(bool checked) emits its argument value (false for a plain click).
  QJsonArray args = notification["arguments"].toArray();
  QEXPECT_THAT(args.size(), Eq(1));
  QEXPECT_THAT(args.at(0).toBool(), IsFalse());

  // Cleanup
  SignalMonitor::instance()->unsubscribe(subId);
}

void TestSignalMonitor::testSignalArgumentValues() {
  // A signal carrying a QString argument should deliver its value, not an
  // empty placeholder.
  auto* edit = new QLineEdit();
  edit->setObjectName("argEdit");
  m_testObjects.append(edit);

  QCoreApplication::processEvents();

  QString objId = ObjectRegistry::instance()->objectId(edit);
  QString subId = SignalMonitor::instance()->subscribe(objId, "textChanged");

  QSignalSpy spy(SignalMonitor::instance(), &SignalMonitor::signalEmitted);
  QEXPECT_THAT(spy.isValid(), IsTrue());

  edit->setText(QStringLiteral("hello"));

  QEXPECT_THAT(spy.count(), Eq(1));
  QJsonObject notification = spy.at(0).at(0).toJsonObject();
  QEXPECT_THAT(notification, AllOf(
      HasJsonField("signal", QStrEq("textChanged")),
      HasJsonField("arguments", JsonArrayContains(QStrEq("hello")))));

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
  QEXPECT_THAT(countBefore, Ge(1));

  // Delete the object
  delete btn;

  // Process events to allow cleanup
  QCoreApplication::processEvents();

  // Subscription should have been auto-removed
  int countAfter = SignalMonitor::instance()->subscriptionCount();
  QEXPECT_THAT(countAfter, Eq(countBefore - 1));
}

void TestSignalMonitor::testLifecycleCreated() {
  // Model the transport connection that enables live object-added events.
  ObjectRegistry::instance()->setClientConnected(true);

  // Enable lifecycle notifications
  SignalMonitor::instance()->setLifecycleNotificationsEnabled(true);

  // Set up spy
  QSignalSpy spy(SignalMonitor::instance(), &SignalMonitor::objectCreated);
  QEXPECT_THAT(spy.isValid(), IsTrue());

  // Create an object
  auto* obj = new QObject();
  obj->setObjectName("lifecycleTest");
  m_testObjects.append(obj);

  // Process events - objectCreated is emitted via QueuedConnection
  QCoreApplication::processEvents();

  // Should have received at least one objectCreated notification
  // (might get more from internal Qt objects)
  QEXPECT_THAT(spy.count(), Ge(1));

  // Find our notification
  bool found = false;
  for (int i = 0; i < spy.count(); ++i) {
    QJsonObject notification = spy.at(i).at(0).toJsonObject();
    if (notification["className"].toString() == "QObject") {
      QEXPECT_THAT(notification, HasJsonField("event", QStrEq("created")));
      found = true;
      break;
    }
  }
  QEXPECT_THAT(found, IsTrue());
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
  QEXPECT_THAT(spy.isValid(), IsTrue());

  // Delete the object
  delete obj;

  // Process events
  QCoreApplication::processEvents();

  // Should have received at least one objectDestroyed notification
  QEXPECT_THAT(spy.count(), Ge(1));

  // Find our notification (with cached objectId since we subscribed)
  bool found = false;
  for (int i = 0; i < spy.count(); ++i) {
    QJsonObject notification = spy.at(i).at(0).toJsonObject();
    if (notification["objectId"].toString() == objId) {
      QEXPECT_THAT(notification, HasJsonField("event", QStrEq("destroyed")));
      found = true;
      break;
    }
  }
  QEXPECT_THAT(found, IsTrue());
}

void TestSignalMonitor::testSubscribeNonexistentObject() {
  bool threw = false;
  try {
    SignalMonitor::instance()->subscribe("nonexistent/object/id", "clicked");
  } catch (const std::runtime_error& e) {
    threw = true;
    QString msg = QString::fromStdString(e.what());
    QEXPECT_THAT(msg, QStrContains("Object not found"));
  }
  QEXPECT_THAT(threw, IsTrue());
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
    QEXPECT_THAT(msg, QStrContains("Signal not found"));
  }
  QEXPECT_THAT(threw, IsTrue());
}

void TestSignalMonitor::testSubscribeExpectedSuccess() {
  auto* btn = new QPushButton();
  btn->setObjectName("expectedTestBtn");
  m_testObjects.append(btn);

  QCoreApplication::processEvents();

  QString objId = ObjectRegistry::instance()->objectId(btn);
  QEXPECT_THAT(objId, QIsNotEmpty());

  auto res = SignalMonitor::instance()->subscribeExpected(objId, "clicked");
  QVERIFY(res.has_value());
  QEXPECT_THAT(*res, QStrStartsWith("sub_"));

  // Clean up
  SignalMonitor::instance()->unsubscribe(*res);
}

void TestSignalMonitor::testSubscribeExpectedNonexistentObject() {
  auto res = SignalMonitor::instance()->subscribeExpected("nonexistent/object/id", "clicked");
  QVERIFY(!res.has_value());
  QCOMPARE(static_cast<int>(res.error().kind), static_cast<int>(SignalErrorKind::ObjectNotFound));
  QEXPECT_THAT(res.error().message, QStrContains("Object not found"));
}

void TestSignalMonitor::testSubscribeExpectedNonexistentSignal() {
  auto* btn = new QPushButton();
  btn->setObjectName("noSignalExpectedBtn");
  m_testObjects.append(btn);

  QCoreApplication::processEvents();

  QString objId = ObjectRegistry::instance()->objectId(btn);

  auto res = SignalMonitor::instance()->subscribeExpected(objId, "nonexistentSignal");
  QVERIFY(!res.has_value());
  QCOMPARE(static_cast<int>(res.error().kind), static_cast<int>(SignalErrorKind::SignalNotFound));
  QEXPECT_THAT(res.error().message, QStrContains("Signal not found"));
}

QTEST_MAIN(TestSignalMonitor)
#include "test_signal_monitor.moc"
