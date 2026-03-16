// Copyright (c) 2024 QtMCP Contributors
// SPDX-License-Identifier: MIT

#include "introspection/signal_monitor.h"

#include "core/object_registry.h"

#include <stdexcept>

#include <QDebug>
#include <QGlobalStatic>
#include <QMetaMethod>
#include <QMutexLocker>

namespace qtmcp {

/// @brief Helper class that acts as a relay between dynamic signals and SignalMonitor.
///
/// Each subscription creates one SignalRelay instance. The relay has a generic
/// handleSignal() slot that can be connected to any parameterless signal (or signals
/// whose parameters we ignore for MVP). When invoked, it emits signalTriggered
/// with the subscription context.
class SignalRelay : public QObject {
  Q_OBJECT

 public:
  SignalRelay(const QString& subId, const QString& objId, const QString& sigName, QObject* parent)
      : QObject(parent), m_subscriptionId(subId), m_objectId(objId), m_signalName(sigName) {}

  void setObjectId(const QString& id) { m_objectId = id; }

 Q_SIGNALS:
  /// @brief Emitted when the monitored signal fires.
  void signalTriggered(const QJsonObject& notification);

 public Q_SLOTS:
  /// @brief Generic slot that can receive any signal (ignoring parameters).
  void handleSignal() {
    QJsonObject notification;
    notification[QStringLiteral("subscriptionId")] = m_subscriptionId;
    notification[QStringLiteral("objectId")] = m_objectId;
    notification[QStringLiteral("signal")] = m_signalName;
    notification[QStringLiteral("arguments")] = QJsonArray();  // Empty for MVP

    Q_EMIT signalTriggered(notification);
  }

 private:
  QString m_subscriptionId;
  QString m_objectId;
  QString m_signalName;
};

// Thread-safe singleton storage using Q_GLOBAL_STATIC
Q_GLOBAL_STATIC(SignalMonitor, s_signalMonitorInstance)

SignalMonitor* SignalMonitor::instance() {
  return s_signalMonitorInstance();
}

SignalMonitor::SignalMonitor() : QObject(nullptr) {
  // Connect to ObjectRegistry lifecycle events
  connect(ObjectRegistry::instance(), &ObjectRegistry::objectAdded, this,
          &SignalMonitor::onObjectAdded);
  connect(ObjectRegistry::instance(), &ObjectRegistry::objectRemoved, this,
          &SignalMonitor::onObjectRemoved);

  // Update cached objectIds in subscriptions and relays when IDs are refreshed
  connect(ObjectRegistry::instance(), &ObjectRegistry::objectIdChanged, this,
          [this](QObject* obj, const QString& /*oldId*/, const QString& newId) {
            QMutexLocker lock(&m_mutex);
            for (auto it = m_subscriptions.begin(); it != m_subscriptions.end(); ++it) {
              if (it->object.data() == obj) {
                it->objectId = newId;
                // Also update the relay so emitted notifications use the new ID
                if (it->relay) {
                  it->relay->setObjectId(newId);
                }
              }
            }
          });

  qDebug() << "[QtMCP] SignalMonitor created";
}

SignalMonitor::~SignalMonitor() {
  // Disconnect all subscriptions and delete relays
  QMutexLocker lock(&m_mutex);
  for (auto it = m_subscriptions.begin(); it != m_subscriptions.end(); ++it) {
    if (it->connection) {
      QObject::disconnect(it->connection);
    }
    delete it->relay;
  }
  m_subscriptions.clear();

  qDebug() << "[QtMCP] SignalMonitor destroyed";
}

QString SignalMonitor::subscribe(const QString& objectId, const QString& signalName) {
  // Find the object by ID
  QObject* obj = ObjectRegistry::instance()->findById(objectId);
  if (!obj) {
    throw std::runtime_error("Object not found: " + objectId.toStdString());
  }

  const QMetaObject* meta = obj->metaObject();

  // Find signal by name
  int signalIndex = -1;
  for (int i = 0; i < meta->methodCount(); ++i) {
    QMetaMethod method = meta->method(i);
    if (method.methodType() == QMetaMethod::Signal &&
        QString::fromLatin1(method.name()) == signalName) {
      signalIndex = i;
      break;
    }
  }

  if (signalIndex < 0) {
    throw std::runtime_error("Signal not found: " + signalName.toStdString());
  }

  // Generate unique subscription ID
  QString subId;
  {
    QMutexLocker lock(&m_mutex);
    subId = QStringLiteral("sub_%1").arg(m_nextId++);
  }

  QMetaMethod signal = meta->method(signalIndex);

  // Create a SignalRelay QObject per subscription that stores context
  // and has a generic slot for receiving the signal.
  auto* relay = new SignalRelay(subId, objectId, signalName, this);

  // Find the relay's handleSignal slot
  const QMetaObject* relayMeta = relay->metaObject();
  int slotIndex = relayMeta->indexOfSlot("handleSignal()");
  if (slotIndex < 0) {
    delete relay;
    throw std::runtime_error("Internal error: handleSignal slot not found");
  }
  QMetaMethod slot = relayMeta->method(slotIndex);

  // Connect the signal to the relay's slot
  auto conn = QObject::connect(obj, signal, relay, slot);
  if (!conn) {
    delete relay;
    throw std::runtime_error("Failed to connect to signal: " + signalName.toStdString());
  }

  // Relay emits signalTriggered which we connect to our signalEmitted signal
  connect(relay, &SignalRelay::signalTriggered, this, &SignalMonitor::signalEmitted);

  // Watch for object destruction to auto-unsubscribe
  // Note: We use DirectConnection to ensure cleanup happens immediately
  connect(obj, &QObject::destroyed, this, &SignalMonitor::onSubscribedObjectDestroyed,
          Qt::DirectConnection);

  // Store subscription
  {
    QMutexLocker lock(&m_mutex);
    Subscription sub;
    sub.object = obj;
    sub.objectId = objectId;
    sub.signalName = signalName;
    sub.connection = conn;
    sub.relay = relay;
    m_subscriptions.insert(subId, sub);
  }

  qDebug() << "[QtMCP] Subscribed to" << objectId << "::" << signalName << "as" << subId;
  return subId;
}

void SignalMonitor::unsubscribe(const QString& subscriptionId) {
  QMutexLocker lock(&m_mutex);

  auto it = m_subscriptions.find(subscriptionId);
  if (it == m_subscriptions.end()) {
    qWarning() << "[QtMCP] Unsubscribe: subscription not found:" << subscriptionId;
    return;
  }

  // Disconnect the signal
  if (it->connection) {
    QObject::disconnect(it->connection);
  }

  // Delete the relay
  delete it->relay;

  qDebug() << "[QtMCP] Unsubscribed" << subscriptionId << "from" << it->objectId
           << "::" << it->signalName;

  m_subscriptions.erase(it);
}

void SignalMonitor::unsubscribeAll(const QString& objectId) {
  QMutexLocker lock(&m_mutex);

  QStringList toRemove;
  for (auto it = m_subscriptions.begin(); it != m_subscriptions.end(); ++it) {
    if (it->objectId == objectId) {
      if (it->connection) {
        QObject::disconnect(it->connection);
      }
      delete it->relay;
      toRemove.append(it.key());
    }
  }

  for (const QString& id : toRemove) {
    m_subscriptions.remove(id);
  }

  if (!toRemove.isEmpty()) {
    qDebug() << "[QtMCP] Unsubscribed all" << toRemove.size() << "subscriptions for" << objectId;
  }
}

void SignalMonitor::setLifecycleNotificationsEnabled(bool enabled) {
  QMutexLocker lock(&m_mutex);
  m_lifecycleEnabled = enabled;
  qDebug() << "[QtMCP] Lifecycle notifications" << (enabled ? "enabled" : "disabled");
}

bool SignalMonitor::lifecycleNotificationsEnabled() const {
  QMutexLocker lock(&m_mutex);
  return m_lifecycleEnabled;
}

int SignalMonitor::subscriptionCount() const {
  QMutexLocker lock(&m_mutex);
  return m_subscriptions.size();
}

void SignalMonitor::onObjectAdded(QObject* obj) {
  // Check if lifecycle notifications are enabled
  {
    QMutexLocker lock(&m_mutex);
    if (!m_lifecycleEnabled) {
      return;
    }
  }

  // Verify object still exists (queued signal delivery can be delayed)
  if (!obj) {
    return;
  }

  // Build notification
  QJsonObject notification;
  notification[QStringLiteral("event")] = QStringLiteral("created");
  notification[QStringLiteral("objectId")] = ObjectRegistry::instance()->objectId(obj);
  notification[QStringLiteral("className")] = QString::fromLatin1(obj->metaObject()->className());

  Q_EMIT objectCreated(notification);
}

void SignalMonitor::onObjectRemoved(QObject* obj) {
  // Check if lifecycle notifications are enabled, and find any cached objectId
  bool lifecycleEnabled;
  QString cachedObjectId;
  {
    QMutexLocker lock(&m_mutex);
    lifecycleEnabled = m_lifecycleEnabled;

    // Look up objectId from our cache. The onSubscribedObjectDestroyed method
    // (connected via DirectConnection) runs synchronously during object destruction
    // BEFORE this method (connected via QueuedConnection). When onSubscribedObjectDestroyed
    // cleans up subscriptions, it caches the objectId in m_destroyedObjectIds.
    auto it = m_destroyedObjectIds.find(obj);
    if (it != m_destroyedObjectIds.end()) {
      cachedObjectId = it.value();
      m_destroyedObjectIds.erase(it);
    }
  }

  // Clean up subscriptions for this object
  // Note: This may have already run via DirectConnection before we got here
  onSubscribedObjectDestroyed(obj);

  if (!lifecycleEnabled) {
    return;
  }

  // Build notification
  // Note: The object is being destroyed and has already been removed from
  // ObjectRegistry's ID cache. Use the cached ID if we have it, otherwise
  // the notification will have an empty objectId.
  QJsonObject notification;
  notification[QStringLiteral("event")] = QStringLiteral("destroyed");
  notification[QStringLiteral("objectId")] = cachedObjectId;

  Q_EMIT objectDestroyed(notification);
}

void SignalMonitor::onSubscribedObjectDestroyed(QObject* obj) {
  QMutexLocker lock(&m_mutex);

  QStringList toRemove;
  QString cachedObjectId;  // Cache for lifecycle notifications
  for (auto it = m_subscriptions.begin(); it != m_subscriptions.end(); ++it) {
    // Check by pointer since QPointer may already be null
    if (it->object.data() == obj || it->object.isNull()) {
      // Cache the objectId for onObjectRemoved (runs later via QueuedConnection)
      if (cachedObjectId.isEmpty()) {
        cachedObjectId = it->objectId;
      }
      // Don't disconnect - object is already being destroyed
      // Delete the relay (it's parented to this, but be explicit)
      delete it->relay;
      toRemove.append(it.key());
    }
  }

  // Store the cached objectId for retrieval by onObjectRemoved
  if (!cachedObjectId.isEmpty()) {
    m_destroyedObjectIds.insert(obj, cachedObjectId);
  }

  for (const QString& id : toRemove) {
    qDebug() << "[QtMCP] Auto-unsubscribed" << id << "due to object destruction";
    m_subscriptions.remove(id);
  }
}

}  // namespace qtmcp

// Include the moc file for SignalRelay (defined in this cpp file)
#include "signal_monitor.moc"
