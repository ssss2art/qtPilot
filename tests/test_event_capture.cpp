// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "core/object_registry.h"
#include "introspection/event_capture.h"

#include <QApplication>
#include <QAtomicInteger>
#include <QCloseEvent>
#include <QMainWindow>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalSpy>
#include <QThread>
#include <QtTest>

#include "common/qt_matchers.h"

using namespace qtPilot;
using namespace qtPilot::test;

namespace {

class CaptureWorker final : public QThread {
 public:
  explicit CaptureWorker(EventCapture* capture) : m_capture(capture) {}

  QAtomicInteger<bool> started = false;
  QAtomicInteger<bool> stopped = false;

 protected:
  void run() override {
    m_capture->startCapture();
    started = m_capture->isCapturing();
    m_capture->stopCapture();
    stopped = !m_capture->isCapturing();
  }

 private:
  EventCapture* m_capture;
};

}  // namespace

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
  void testStartStopCaptureFromWorkerThread();
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
  QEXPECT_THAT(ec->isCapturing(), IsFalse());

  ec->startCapture();
  QEXPECT_THAT(ec->isCapturing(), IsTrue());

  ec->stopCapture();
  QEXPECT_THAT(ec->isCapturing(), IsFalse());
}

void TestEventCapture::testStartStopCaptureFromWorkerThread() {
  CaptureWorker worker(EventCapture::instance());
  worker.start();

  QTRY_VERIFY_WITH_TIMEOUT(worker.isFinished(), 2000);
  QEXPECT_THAT(worker.wait(), IsTrue());
  QEXPECT_THAT(worker.started.loadRelaxed(), IsTrue());
  QEXPECT_THAT(worker.stopped.loadRelaxed(), IsTrue());
}

void TestEventCapture::testShowEvent() {
  auto* ec = EventCapture::instance();
  ec->startCapture();

  QSignalSpy spy(ec, &EventCapture::eventCaptured);
  QEXPECT_THAT(spy.isValid(), IsTrue());

  m_window->show();
  QApplication::processEvents();

  // Find a Show notification for our window
  bool found = false;
  for (int i = 0; i < spy.count(); ++i) {
    QJsonObject n = spy.at(i).at(0).toJsonObject();
    if (n["type"].toString() == "Show" && n["objectName"].toString() == "testWindow") {
      found = true;
      QEXPECT_THAT(n, HasJsonField("objectId"));
      QEXPECT_THAT(n, HasJsonField("className"));
      break;
    }
  }
  QEXPECT_THAT(found, IsTrue());
}

void TestEventCapture::testHideEvent() {
  // Show first, then hide
  m_window->show();
  QApplication::processEvents();

  auto* ec = EventCapture::instance();
  ec->startCapture();

  QSignalSpy spy(ec, &EventCapture::eventCaptured);
  QEXPECT_THAT(spy.isValid(), IsTrue());

  m_window->hide();
  QApplication::processEvents();

  bool found = false;
  for (int i = 0; i < spy.count(); ++i) {
    QJsonObject n = spy.at(i).at(0).toJsonObject();
    if (n["type"].toString() == "Hide" && n["objectName"].toString() == "testWindow") {
      found = true;
      QEXPECT_THAT(n, HasJsonField("objectId"));
      QEXPECT_THAT(n, HasJsonField("className"));
      break;
    }
  }
  QEXPECT_THAT(found, IsTrue());
}

void TestEventCapture::testResizeEvent() {
  m_window->show();
  QApplication::processEvents();

  auto* ec = EventCapture::instance();
  ec->startCapture();

  QSignalSpy spy(ec, &EventCapture::eventCaptured);
  QEXPECT_THAT(spy.isValid(), IsTrue());

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
  QEXPECT_THAT(found, IsTrue());
}

void TestEventCapture::testResizeEventContainsSize() {
  m_window->show();
  QApplication::processEvents();

  auto* ec = EventCapture::instance();
  ec->startCapture();

  QSignalSpy spy(ec, &EventCapture::eventCaptured);
  QEXPECT_THAT(spy.isValid(), IsTrue());

  m_window->resize(800, 600);
  QApplication::processEvents();

  for (int i = 0; i < spy.count(); ++i) {
    QJsonObject n = spy.at(i).at(0).toJsonObject();
    if (n["type"].toString() == "Resize" && n["objectName"].toString() == "testWindow") {
      // Resize notifications must include a "size" object with w/h
      QEXPECT_THAT(n, HasJsonField("size"));
      QJsonObject size = n["size"].toObject();
      QEXPECT_THAT(size, HasJsonField("w", 800));
      QEXPECT_THAT(size, HasJsonField("h", 600));
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
  QEXPECT_THAT(spy.isValid(), IsTrue());

  // Send a close event (doesn't destroy — just sends QCloseEvent)
  m_window->close();
  QApplication::processEvents();

  bool found = false;
  for (int i = 0; i < spy.count(); ++i) {
    QJsonObject n = spy.at(i).at(0).toJsonObject();
    if (n["type"].toString() == "Close" && n["objectName"].toString() == "testWindow") {
      found = true;
      QEXPECT_THAT(n, HasJsonField("objectId"));
      QEXPECT_THAT(n, HasJsonField("className"));
      break;
    }
  }
  QEXPECT_THAT(found, IsTrue());
}

void TestEventCapture::testNonWidgetIgnored() {
  auto* ec = EventCapture::instance();
  ec->startCapture();

  QSignalSpy spy(ec, &EventCapture::eventCaptured);
  QEXPECT_THAT(spy.isValid(), IsTrue());

  // Send a show event to a plain QObject (not a QWidget)
  QObject plainObj;
  plainObj.setObjectName("plainObject");
  QEvent showEvent(QEvent::Show);
  QApplication::sendEvent(&plainObj, &showEvent);
  QApplication::processEvents();

  // Should NOT have captured anything for plainObject
  for (int i = 0; i < spy.count(); ++i) {
    QJsonObject n = spy.at(i).at(0).toJsonObject();
    QEXPECT_THAT(n["objectName"].toString(), Ne("plainObject"));
  }
}

void TestEventCapture::testNotCapturingIgnoresEvents() {
  auto* ec = EventCapture::instance();
  // Ensure capture is stopped
  ec->stopCapture();

  QSignalSpy spy(ec, &EventCapture::eventCaptured);
  QEXPECT_THAT(spy.isValid(), IsTrue());

  m_window->show();
  QApplication::processEvents();

  QEXPECT_THAT(spy.count(), Eq(0));
}

QTEST_MAIN(TestEventCapture)
#include "test_event_capture.moc"
