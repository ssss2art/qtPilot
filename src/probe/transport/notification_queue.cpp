// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "transport/notification_queue.h"

#include <QTimer>
#include <QWebSocket>

namespace qtPilot {

NotificationQueue::NotificationQueue(QWebSocket* socket, int capacity, int batchSize,
                                     QObject* parent)
    : QObject(parent), m_socket(socket), m_capacity(capacity), m_batchSize(batchSize) {
  // Override capacity from environment
  QByteArray envCap = qgetenv("QTPILOT_QUEUE_CAPACITY");
  if (!envCap.isEmpty()) {
    bool ok = false;
    int envVal = envCap.toInt(&ok);
    if (ok && envVal > 0) {
      m_capacity = envVal;
    }
  }

  m_drainTimer = new QTimer(this);
  m_drainTimer->setInterval(0);
  connect(m_drainTimer, &QTimer::timeout, this, &NotificationQueue::drain);
  m_drainTimer->start();
}

NotificationQueue::~NotificationQueue() = default;

void NotificationQueue::enqueue(const QString& message) {
  QMutexLocker lock(&m_mutex);
  if (m_queue.size() >= m_capacity) {
    m_queue.dequeue();  // drop oldest
    ++m_dropCount;
  }
  m_queue.enqueue(message);
}

int NotificationQueue::dropCount() const {
  QMutexLocker lock(&m_mutex);
  return m_dropCount;
}

int NotificationQueue::queueSize() const {
  QMutexLocker lock(&m_mutex);
  // qsizetype -> int: bounded by m_capacity, but state the conversion so the
  // stricter iOS warning set does not reject it.
  return static_cast<int>(m_queue.size());
}

int NotificationQueue::capacity() const {
  return m_capacity;
}

void NotificationQueue::drain() {
  if (!m_socket) {
    return;
  }

  // Backpressure: check bytes still pending in the socket write buffer
  qint64 pending = m_socket->bytesToWrite();
  if (m_paused) {
    if (pending <= kLowWaterMark) {
      m_paused = false;
    } else {
      return;  // still above low-water mark, skip this cycle
    }
  } else if (pending > kHighWaterMark) {
    m_paused = true;
    return;
  }

  // Drain up to batchSize messages
  QMutexLocker lock(&m_mutex);
  // qMin returns const T&, so an explicit template argument would bind that
  // reference to temporaries created by converting the arguments. Use same-typed
  // lvalues instead and narrow once, deliberately.
  const qsizetype queued = m_queue.size();
  const qsizetype batch = static_cast<qsizetype>(m_batchSize);
  const int count = static_cast<int>(qMin(batch, queued));
  for (int i = 0; i < count; ++i) {
    QString msg = m_queue.dequeue();
    lock.unlock();
    m_socket->sendTextMessage(msg);
    lock.relock();
  }
}

}  // namespace qtPilot
