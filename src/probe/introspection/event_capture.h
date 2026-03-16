// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "core/probe.h"  // For QTPILOT_EXPORT

#include <QJsonObject>
#include <QMutex>
#include <QObject>
#include <QSet>

namespace qtPilot {

/// @brief Global event filter that captures user-interaction QEvents.
///
/// When capture is active, installs itself as an event filter on
/// QCoreApplication to observe every event delivered to every widget.
/// Only user-interaction events (mouse, keyboard, focus) are captured;
/// paint, timer, layout, and other noise is ignored.
///
/// Each captured event is emitted as a JSON notification via eventCaptured(),
/// which the probe wires to the WebSocket as "qtpilot.eventCaptured".
///
/// Thread Safety: All public methods are thread-safe.
class QTPILOT_EXPORT EventCapture : public QObject {
  Q_OBJECT

 public:
  /// @brief Get the singleton instance.
  static EventCapture* instance();

  /// @brief Start capturing events (installs event filter on qApp).
  void startCapture();

  /// @brief Stop capturing events (removes event filter from qApp).
  void stopCapture();

  /// @brief Check if capture is currently active.
  bool isCapturing() const;

 Q_SIGNALS:
  /// @brief Emitted for each captured user-interaction event.
  ///
  /// Notification JSON contains:
  ///   - type: QEvent type name (e.g. "MouseButtonPress")
  ///   - objectId: hierarchical widget ID
  ///   - objectName: QObject::objectName
  ///   - className: widget class name
  ///   - (event-specific fields: button, pos, globalPos, key, text, modifiers, reason)
  void eventCaptured(const QJsonObject& notification);

 protected:
  /// @brief Event filter override. Always returns false (observe only).
  bool eventFilter(QObject* watched, QEvent* event) override;

 public:
  // Constructor/destructor public for Q_GLOBAL_STATIC compatibility
  EventCapture();
  ~EventCapture() override;

 private:
  /// @brief Build notification JSON for a mouse event.
  QJsonObject buildMouseNotification(QObject* widget, QEvent* event, const QString& typeName);

  /// @brief Build notification JSON for a key event.
  QJsonObject buildKeyNotification(QObject* widget, QEvent* event, const QString& typeName);

  /// @brief Build notification JSON for a focus event.
  QJsonObject buildFocusNotification(QObject* widget, QEvent* event, const QString& typeName);

  /// @brief Build notification JSON for a window lifecycle event (Show, Hide, Close, Resize,
  /// Activate).
  QJsonObject buildWindowNotification(QObject* widget, QEvent* event, const QString& typeName);

  bool m_capturing = false;

  /// @brief Set of QEvent::Type values we capture.
  QSet<int> m_capturedTypes;

  mutable QMutex m_mutex;
};

}  // namespace qtPilot
