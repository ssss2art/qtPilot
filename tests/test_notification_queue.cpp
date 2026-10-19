// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "common/qt_matchers.h"
#include "transport/notification_queue.h"

#include <QAbstractEventDispatcher>
#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <QWebSocket>

using namespace qtPilot::test;

class TestNotificationQueue : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() {
    if (QCoreApplication::instance() == nullptr) {
      int argc = 0;
      char** argv = nullptr;
      app_ = new QCoreApplication(argc, argv);
    }
  }

  void cleanupTestCase() {}

  /// Enqueue N messages and verify FIFO order after draining.
  void testEnqueueDrain() {
    QWebSocket socket;
    qtPilot::NotificationQueue queue(&socket, 100, 50);

    const int count = 10;
    for (int i = 0; i < count; ++i) {
      queue.enqueue(QString("msg_%1").arg(i));
    }

    QEXPECT_THAT(queue.queueSize(), Eq(count));
    QEXPECT_THAT(queue.dropCount(), Eq(0));

    // Process events to let drain timer fire
    QCoreApplication::processEvents();

    // After drain, queue should be empty (all 10 < batchSize of 50)
    QEXPECT_THAT(queue.queueSize(), Eq(0));
    QEXPECT_THAT(queue.dropCount(), Eq(0));
  }

  /// Exceed capacity and verify oldest dropped and dropCount increments.
  void testCapacityDrop() {
    QWebSocket socket;
    const int capacity = 5;
    qtPilot::NotificationQueue queue(&socket, capacity, 50);

    // Enqueue more than capacity without draining
    for (int i = 0; i < 8; ++i) {
      queue.enqueue(QString("msg_%1").arg(i));
    }

    // Should have dropped 3 oldest messages
    QEXPECT_THAT(queue.queueSize(), Eq(capacity));
    QEXPECT_THAT(queue.dropCount(), Eq(3));
  }

  /// Verify batch size limits messages per drain cycle.
  void testBatchSize() {
    QWebSocket socket;
    const int batchSize = 3;
    qtPilot::NotificationQueue queue(&socket, 100, batchSize);

    // Enqueue 10 messages
    for (int i = 0; i < 10; ++i) {
      queue.enqueue(QString("msg_%1").arg(i));
    }

    QEXPECT_THAT(queue.queueSize(), Eq(10));

    // One drain cycle should only take batchSize messages
    QCoreApplication::processEvents();

    // After one drain cycle, 10 - 3 = 7 remaining
    QEXPECT_THAT(queue.queueSize(), Eq(7));

    // Process more events until all drained
    for (int i = 0; i < 5; ++i) {
      QCoreApplication::processEvents();
    }

    QEXPECT_THAT(queue.queueSize(), Eq(0));
  }

  /// A connected client with nothing to send costs nothing: no timer runs while the queue is idle.
  void testIdleQueueRunsNoTimer() {
    QWebSocket socket;
    qtPilot::NotificationQueue queue(&socket, 100, 50);
    QEXPECT_THAT(activeTimers(queue), Eq(0));

    queue.enqueue("msg");
    QCoreApplication::processEvents();
    QEXPECT_THAT(queue.queueSize(), Eq(0));
    QEXPECT_THAT(activeTimers(queue), Eq(0));
  }

  /// Timers running for @p queue, on it or on any object it owns.
  static qsizetype activeTimers(QObject& queue) {
    auto* dispatcher = QAbstractEventDispatcher::instance();
    qsizetype count = dispatcher->registeredTimers(&queue).size();
    for (QObject* child : queue.findChildren<QObject*>()) {
      count += dispatcher->registeredTimers(child).size();
    }
    return count;
  }

  /// Producers on other threads (signal monitors) still get their messages drained.
  void testEnqueueFromAnotherThreadDrains() {
    QWebSocket socket;
    qtPilot::NotificationQueue queue(&socket, 100, 50);
    QThread* producer = QThread::create([&queue] {
      for (int i = 0; i < 5; ++i) {
        queue.enqueue(QString("msg_%1").arg(i));
      }
    });
    producer->start();
    QVERIFY(producer->wait(5000));
    delete producer;
    QEXPECT_THAT(queue.queueSize(), Eq(5));
    QCoreApplication::processEvents();
    QEXPECT_THAT(queue.queueSize(), Eq(0));
  }

  /// Verify capacity() returns the configured capacity.
  void testCapacityAccessor() {
    QWebSocket socket;
    qtPilot::NotificationQueue queue(&socket, 500, 50);
    QEXPECT_THAT(queue.capacity(), Eq(500));
  }

 private:
  QCoreApplication* app_ = nullptr;
};

QTEST_APPLESS_MAIN(TestNotificationQueue)
#include "test_notification_queue.moc"
