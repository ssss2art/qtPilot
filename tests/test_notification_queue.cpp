// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "transport/notification_queue.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <QWebSocket>

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

    QCOMPARE(queue.queueSize(), count);
    QCOMPARE(queue.dropCount(), 0);

    // Process events to let drain timer fire
    QCoreApplication::processEvents();

    // After drain, queue should be empty (all 10 < batchSize of 50)
    QCOMPARE(queue.queueSize(), 0);
    QCOMPARE(queue.dropCount(), 0);
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
    QCOMPARE(queue.queueSize(), capacity);
    QCOMPARE(queue.dropCount(), 3);
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

    QCOMPARE(queue.queueSize(), 10);

    // One drain cycle should only take batchSize messages
    QCoreApplication::processEvents();

    // After one drain cycle, 10 - 3 = 7 remaining
    QCOMPARE(queue.queueSize(), 7);

    // Process more events until all drained
    for (int i = 0; i < 5; ++i) {
      QCoreApplication::processEvents();
    }

    QCOMPARE(queue.queueSize(), 0);
  }

  /// Verify capacity() returns the configured capacity.
  void testCapacityAccessor() {
    QWebSocket socket;
    qtPilot::NotificationQueue queue(&socket, 500, 50);
    QCOMPARE(queue.capacity(), 500);
  }

 private:
  QCoreApplication* app_ = nullptr;
};

QTEST_APPLESS_MAIN(TestNotificationQueue)
#include "test_notification_queue.moc"
