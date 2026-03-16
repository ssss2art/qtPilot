// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "core/probe.h"  // For QTPILOT_EXPORT

#include <QMutex>
#include <QObject>
#include <QQueue>
#include <QString>

class QTimer;
class QWebSocket;

namespace qtPilot {

/// @brief Bounded queue for WebSocket notifications with backpressure.
///
/// Decouples notification producers (SignalMonitor, EventCapture) from
/// the WebSocket send path. Uses drop-oldest semantics when the queue
/// is full, and pauses draining when the WebSocket write buffer exceeds
/// a high-water mark.
class QTPILOT_EXPORT NotificationQueue : public QObject {
  Q_OBJECT

 public:
  /// @brief Construct a notification queue.
  /// @param socket The WebSocket to drain messages to.
  /// @param capacity Maximum queue size (default 10000, overridden by QTPILOT_QUEUE_CAPACITY env).
  /// @param batchSize Messages to drain per event loop cycle (default 50).
  /// @param parent Parent QObject.
  explicit NotificationQueue(QWebSocket* socket, int capacity = 10000, int batchSize = 50,
                             QObject* parent = nullptr);

  ~NotificationQueue() override;

  /// @brief Thread-safe enqueue. Drops oldest if at capacity.
  /// @param message The JSON notification string.
  void enqueue(const QString& message);

  /// @brief Number of messages dropped since creation.
  int dropCount() const;

  /// @brief Current number of messages in the queue.
  int queueSize() const;

  /// @brief Current capacity.
  int capacity() const;

 private slots:
  void drain();

 private:
  QWebSocket* m_socket;
  QQueue<QString> m_queue;
  mutable QMutex m_mutex;
  QTimer* m_drainTimer;
  int m_capacity;
  int m_batchSize;
  int m_dropCount = 0;
  bool m_paused = false;

  static constexpr qint64 kHighWaterMark = 1024 * 1024;  // 1 MB
  static constexpr qint64 kLowWaterMark = 512 * 1024;    // 512 KB
};

}  // namespace qtPilot
