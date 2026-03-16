// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "core/object_registry.h"
#include "introspection/event_capture.h"

#include <QApplication>
#include <QCloseEvent>
#include <QMainWindow>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalSpy>
#include <QtTest>

using namespace qtPilot;

/// @brief Unit tests for EventCapture window lifecycle events
/// (Show, Hide, Close, Resize).
class TestEventCapture : public QObject {
  Q_OBJECT

 private Q_SLOTS:
  void initTestCase();
  void cleanupTestCase();
  void init();
  void cleanup();

  void testStartStopCapture();
  void testShowEvent();
  void testHideEvent();
  void testResizeEvent();
  void testResizeEventContainsSize();
  void testCloseEvent();
  void testNonWidgetIgnored();
  void testNotCapturingIgnoresEvents();

 private:
  QMainWindow* m_window = nullptr;
};

void TestEventCapture::initTestCase() {
  installObjectHooks();
}

void TestEventCapture::cleanupTestCase() {
  uninstallObjectHooks();
}

void TestEventCapture::init() {
  m_window = new QMainWindow();
  m_window->setObjectName("testWindow");
  m_window->setGeometry(100, 100, 400, 300);
  QApplication::processEvents();
}

void TestEventCapture::cleanup() {
  EventCapture::instance()->stopCapture();
  delete m_window;
  m_window = nullptr;
  QApplication::processEvents();
}

void TestEventCapture::testStartStopCapture() {
  auto* ec = EventCapture::instance();
  QVERIFY(!ec->isCapturing());

  ec->startCapture();
  QVERIFY(ec->isCapturing());

  ec->stopCapture();
  QVERIFY(!ec->isCapturing());
}

void TestEventCapture::testShowEvent() {
  auto* ec = EventCapture::instance();
  ec->startCapture();

  QSignalSpy spy(ec, &EventCapture::eventCaptured);
  QVERIFY(spy.isValid());

  m_window->show();
  QApplication::processEvents();

  // Find a Show notification for our window
  bool found = false;
  for (int i = 0; i < spy.count(); ++i) {
    QJsonObject n = spy.at(i).at(0).toJsonObject();
    if (n["type"].toString() == "Show" && n["objectName"].toString() == "testWindow") {
      found = true;
      QVERIFY(n.contains("objectId"));
      QVERIFY(n.contains("className"));
      break;
    }
  }
  QVERIFY2(found, "Should have captured a Show event for testWindow");
}

void TestEventCapture::testHideEvent() {
  // Show first, then hide
  m_window->show();
  QApplication::processEvents();

  auto* ec = EventCapture::instance();
  ec->startCapture();

  QSignalSpy spy(ec, &EventCapture::eventCaptured);
  QVERIFY(spy.isValid());

  m_window->hide();
  QApplication::processEvents();

  bool found = false;
  for (int i = 0; i < spy.count(); ++i) {
    QJsonObject n = spy.at(i).at(0).toJsonObject();
    if (n["type"].toString() == "Hide" && n["objectName"].toString() == "testWindow") {
      found = true;
      QVERIFY(n.contains("objectId"));
      QVERIFY(n.contains("className"));
      break;
    }
  }
  QVERIFY2(found, "Should have captured a Hide event for testWindow");
}

void TestEventCapture::testResizeEvent() {
  m_window->show();
  QApplication::processEvents();

  auto* ec = EventCapture::instance();
  ec->startCapture();

  QSignalSpy spy(ec, &EventCapture::eventCaptured);
  QVERIFY(spy.isValid());

  m_window->resize(600, 400);
  QApplication::processEvents();

  bool found = false;
  for (int i = 0; i < spy.count(); ++i) {
    QJsonObject n = spy.at(i).at(0).toJsonObject();
    if (n["type"].toString() == "Resize" && n["objectName"].toString() == "testWindow") {
      found = true;
      break;
    }
  }
  QVERIFY2(found, "Should have captured a Resize event for testWindow");
}

void TestEventCapture::testResizeEventContainsSize() {
  m_window->show();
  QApplication::processEvents();

  auto* ec = EventCapture::instance();
  ec->startCapture();

  QSignalSpy spy(ec, &EventCapture::eventCaptured);
  QVERIFY(spy.isValid());

  m_window->resize(800, 600);
  QApplication::processEvents();

  for (int i = 0; i < spy.count(); ++i) {
    QJsonObject n = spy.at(i).at(0).toJsonObject();
    if (n["type"].toString() == "Resize" && n["objectName"].toString() == "testWindow") {
      // Resize notifications must include a "size" object with w/h
      QVERIFY2(n.contains("size"), "Resize notification must contain 'size'");
      QJsonObject size = n["size"].toObject();
      QVERIFY2(size.contains("w"), "Size must contain 'w'");
      QVERIFY2(size.contains("h"), "Size must contain 'h'");
      QCOMPARE(size["w"].toInt(), 800);
      QCOMPARE(size["h"].toInt(), 600);
      return;
    }
  }
  QFAIL("No Resize event found for testWindow");
}

void TestEventCapture::testCloseEvent() {
  m_window->show();
  QApplication::processEvents();

  auto* ec = EventCapture::instance();
  ec->startCapture();

  QSignalSpy spy(ec, &EventCapture::eventCaptured);
  QVERIFY(spy.isValid());

  // Send a close event (doesn't destroy — just sends QCloseEvent)
  m_window->close();
  QApplication::processEvents();

  bool found = false;
  for (int i = 0; i < spy.count(); ++i) {
    QJsonObject n = spy.at(i).at(0).toJsonObject();
    if (n["type"].toString() == "Close" && n["objectName"].toString() == "testWindow") {
      found = true;
      QVERIFY(n.contains("objectId"));
      QVERIFY(n.contains("className"));
      break;
    }
  }
  QVERIFY2(found, "Should have captured a Close event for testWindow");
}

void TestEventCapture::testNonWidgetIgnored() {
  auto* ec = EventCapture::instance();
  ec->startCapture();

  QSignalSpy spy(ec, &EventCapture::eventCaptured);
  QVERIFY(spy.isValid());

  // Send a show event to a plain QObject (not a QWidget)
  QObject plainObj;
  plainObj.setObjectName("plainObject");
  QEvent showEvent(QEvent::Show);
  QApplication::sendEvent(&plainObj, &showEvent);
  QApplication::processEvents();

  // Should NOT have captured anything for plainObject
  for (int i = 0; i < spy.count(); ++i) {
    QJsonObject n = spy.at(i).at(0).toJsonObject();
    QVERIFY2(n["objectName"].toString() != "plainObject",
             "EventCapture should not capture events on non-QWidget objects");
  }
}

void TestEventCapture::testNotCapturingIgnoresEvents() {
  auto* ec = EventCapture::instance();
  // Ensure capture is stopped
  ec->stopCapture();

  QSignalSpy spy(ec, &EventCapture::eventCaptured);
  QVERIFY(spy.isValid());

  m_window->show();
  QApplication::processEvents();

  QCOMPARE(spy.count(), 0);
}

QTEST_MAIN(TestEventCapture)
#include "test_event_capture.moc"
