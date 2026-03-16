// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "core/probe.h"  // For QTPILOT_EXPORT

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QMutex>
#include <QObject>
#include <QPointer>

namespace qtPilot {

// Forward declaration
class SignalRelay;

/// @brief Signal subscription and notification system for real-time events.
///
/// SignalMonitor enables clients to subscribe to signals on specific objects
/// and receive push notifications when those signals emit. It also supports
/// object lifecycle notifications (created/destroyed events).
///
/// Implements requirements SIG-01 through SIG-05:
///   - SIG-01: Subscribe to signals on objects
///   - SIG-02: Unsubscribe from signals
///   - SIG-03: Receive signal emission notifications
///   - SIG-04: Receive object created notifications
///   - SIG-05: Receive object destroyed notifications
///
/// Thread Safety: All public methods are thread-safe.
///
/// Usage:
/// @code
/// QString subId = SignalMonitor::instance()->subscribe(objectId, "clicked");
/// connect(SignalMonitor::instance(), &SignalMonitor::signalEmitted,
///         [](const QJsonObject& n) { qDebug() << n; });
/// @endcode
class QTPILOT_EXPORT SignalMonitor : public QObject {
  Q_OBJECT

 public:
  /// @brief Get the singleton instance.
  /// @return Pointer to the global SignalMonitor instance.
  static SignalMonitor* instance();

  /// @brief Subscribe to a signal on an object (SIG-01).
  ///
  /// Creates a dynamic connection to the specified signal. When the signal
  /// emits, a notification is generated and sent to signalEmitted().
  ///
  /// @param objectId Object's hierarchical ID (from ObjectRegistry).
  /// @param signalName Signal name without parameters (e.g., "clicked").
  /// @return Subscription ID for later unsubscribe.
  /// @throws std::runtime_error if object not found or signal not found.
  QString subscribe(const QString& objectId, const QString& signalName);

  /// @brief Unsubscribe from a signal (SIG-02).
  ///
  /// Removes the subscription and disconnects from the signal.
  ///
  /// @param subscriptionId ID returned from subscribe().
  void unsubscribe(const QString& subscriptionId);

  /// @brief Unsubscribe all subscriptions for an object.
  ///
  /// Removes all subscriptions associated with the given object ID.
  ///
  /// @param objectId Object's hierarchical ID.
  void unsubscribeAll(const QString& objectId);

  /// @brief Enable/disable object lifecycle notifications (SIG-04, SIG-05).
  ///
  /// When enabled, objectCreated and objectDestroyed signals are emitted
  /// for every QObject in the application. Disabled by default because
  /// this can be very noisy in large applications.
  ///
  /// @param enabled True to enable lifecycle notifications.
  void setLifecycleNotificationsEnabled(bool enabled);

  /// @brief Check if lifecycle notifications are enabled.
  /// @return True if lifecycle notifications are enabled.
  bool lifecycleNotificationsEnabled() const;

  /// @brief Get active subscription count.
  /// @return Number of active subscriptions.
  int subscriptionCount() const;

 Q_SIGNALS:
  /// @brief Emitted when a subscribed signal fires (SIG-03).
  ///
  /// Notification JSON object contains:
  ///   - subscriptionId: The subscription ID
  ///   - objectId: The object's hierarchical ID
  ///   - signal: The signal name
  ///   - arguments: Array of signal arguments (empty for MVP)
  ///
  /// @param notification JSON object with signal emission details.
  void signalEmitted(const QJsonObject& notification);

  /// @brief Emitted when an object is created (SIG-04).
  ///
  /// Only emitted when lifecycle notifications are enabled.
  /// Notification JSON object contains:
  ///   - event: "created"
  ///   - objectId: The object's hierarchical ID
  ///   - className: The object's class name
  ///
  /// @param notification JSON object with creation details.
  void objectCreated(const QJsonObject& notification);

  /// @brief Emitted when an object is destroyed (SIG-05).
  ///
  /// Only emitted when lifecycle notifications are enabled.
  /// Notification JSON object contains:
  ///   - event: "destroyed"
  ///   - objectId: The object's hierarchical ID
  ///
  /// @param notification JSON object with destruction details.
  void objectDestroyed(const QJsonObject& notification);

 private Q_SLOTS:
  /// @brief Handle ObjectRegistry::objectAdded.
  void onObjectAdded(QObject* obj);

  /// @brief Handle ObjectRegistry::objectRemoved.
  void onObjectRemoved(QObject* obj);

  /// @brief Handle destruction of subscribed objects.
  void onSubscribedObjectDestroyed(QObject* obj);

 public:
  // Constructor/destructor public for Q_GLOBAL_STATIC compatibility
  // Use instance() to get the singleton - do not construct directly
  SignalMonitor();
  ~SignalMonitor() override;

 private:
  /// @brief Internal structure tracking a single subscription.
  struct Subscription {
    QPointer<QObject> object;            ///< Weak pointer to the subscribed object
    QString objectId;                    ///< Cached object ID
    QString signalName;                  ///< Signal name without parameters
    QMetaObject::Connection connection;  ///< Connection handle for disconnect
    SignalRelay* relay = nullptr;        ///< Relay object for dynamic signal connection
  };

  /// @brief Map from subscription ID to subscription data.
  QHash<QString, Subscription> m_subscriptions;

  /// @brief Cache of recently destroyed object IDs for lifecycle notifications.
  /// Populated by onSubscribedObjectDestroyed (DirectConnection), read by
  /// onObjectRemoved (QueuedConnection).
  QHash<QObject*, QString> m_destroyedObjectIds;

  /// @brief Counter for generating unique subscription IDs.
  int m_nextId = 1;

  /// @brief Whether lifecycle notifications are enabled.
  bool m_lifecycleEnabled = false;

  /// @brief Mutex for thread-safe access.
  mutable QMutex m_mutex;
};

}  // namespace qtPilot
