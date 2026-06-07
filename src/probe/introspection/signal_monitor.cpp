// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "introspection/signal_monitor.h"

#include "core/object_registry.h"
#include "introspection/variant_json.h"

#include <stdexcept>
#include <utility>

#include <QDebug>
#include <QGlobalStatic>
#include <QMetaMethod>
#include <QMetaType>
#include <QMutexLocker>
#include <QVariant>

namespace qtPilot {

/// @brief Helper that relays a monitored signal (with its arguments) to the
/// SignalMonitor.
///
/// Each subscription creates one SignalRelay. Rather than connecting to a
/// fixed parameterless slot (which discards arguments), the relay uses the
/// same technique as QSignalSpy: it has no moc-generated meta-object, and the
/// monitored signal is connected to a synthetic slot whose index is QObject's
/// methodCount(). When that slot is invoked, qt_metacall receives the raw
/// argument array, which we decode using the signal's parameter metatypes
/// (captured at subscribe time). The decoded arguments are converted to JSON
/// and delivered to the monitor on its own thread.
class SignalRelay : public QObject {
 public:
  SignalRelay(QString subId, QString objId, QString sigName, QList<int> argTypes,
              SignalMonitor* monitor, QObject* parent)
      : QObject(parent),
        m_subscriptionId(std::move(subId)),
        m_objectId(std::move(objId)),
        m_signalName(std::move(sigName)),
        m_argTypes(std::move(argTypes)),
        m_monitor(monitor) {}

  void setObjectId(const QString& id) { m_objectId = id; }

  /// @brief Synthetic-slot dispatch (QSignalSpy pattern). Index 0 (after
  /// QObject's own methods) is our monitored-signal receiver.
  int qt_metacall(QMetaObject::Call call, int methodId, void** args) override {
    methodId = QObject::qt_metacall(call, methodId, args);
    if (methodId < 0) {
      return methodId;
    }
    if (call == QMetaObject::InvokeMetaMethod) {
      if (methodId == 0) {
        deliver(args);
      }
      --methodId;
    }
    return methodId;
  }

 private:
  /// @brief Decode the void** argument array into JSON and hand it to the
  /// monitor. args[0] is the (unused) return-value slot; arguments begin at
  /// args[1], one per captured parameter metatype.
  void deliver(void** args) {
    QJsonArray jsonArgs;
    for (int i = 0; i < m_argTypes.size(); ++i) {
      const QMetaType metaType(m_argTypes.at(i));
      // An unregistered parameter type (no Q_DECLARE_METATYPE) yields an
      // invalid metatype; emit an explicit null so a dropped argument is not
      // mistaken for a genuine null value.
      if (!metaType.isValid()) {
        jsonArgs.append(QJsonValue());
        continue;
      }
      // The QVariant(QMetaType, const void*) constructor is Qt 6 only; Qt 5
      // uses the int-typeId overload.
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
      jsonArgs.append(variantToJson(QVariant(metaType, args[i + 1])));
#else
      jsonArgs.append(variantToJson(QVariant(metaType.id(), args[i + 1])));
#endif
    }

    QJsonObject notification;
    notification[QStringLiteral("subscriptionId")] = m_subscriptionId;
    notification[QStringLiteral("objectId")] = m_objectId;
    notification[QStringLiteral("signal")] = m_signalName;
    notification[QStringLiteral("arguments")] = jsonArgs;

    // Marshal to the monitor's thread before emitting (AutoConnection: direct
    // when the signal fires on the monitor's thread, queued otherwise).
    QMetaObject::invokeMethod(m_monitor, "deliverNotification", Qt::AutoConnection,
                              Q_ARG(QJsonObject, notification));
  }

  QString m_subscriptionId;
  QString m_objectId;
  QString m_signalName;
  QList<int> m_argTypes;
  SignalMonitor* m_monitor;
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

  qDebug() << "[qtPilot] SignalMonitor created";
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

  qDebug() << "[qtPilot] SignalMonitor destroyed";
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

  // Capture the signal's parameter metatypes so the relay can decode argument
  // values when the signal fires.
  QList<int> argTypes;
  argTypes.reserve(signal.parameterCount());
  for (int p = 0; p < signal.parameterCount(); ++p) {
    argTypes.append(signal.parameterType(p));
  }

  // Create a SignalRelay per subscription. It receives the signal through a
  // synthetic slot (index == QObject's methodCount) and decodes arguments via
  // qt_metacall — see the SignalRelay docs above.
  auto* relay = new SignalRelay(subId, objectId, signalName, argTypes, this, this);

  // Connect the signal to the relay's synthetic slot index.
  auto conn = QMetaObject::connect(obj, signal.methodIndex(), relay,
                                   QObject::staticMetaObject.methodCount());
  if (!conn) {
    delete relay;
    throw std::runtime_error("Failed to connect to signal: " + signalName.toStdString());
  }

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

  qDebug() << "[qtPilot] Subscribed to" << objectId << "::" << signalName << "as" << subId;
  return subId;
}

void SignalMonitor::unsubscribe(const QString& subscriptionId) {
  QMutexLocker lock(&m_mutex);

  auto it = m_subscriptions.find(subscriptionId);
  if (it == m_subscriptions.end()) {
    qWarning() << "[qtPilot] Unsubscribe: subscription not found:" << subscriptionId;
    return;
  }

  // Disconnect the signal
  if (it->connection) {
    QObject::disconnect(it->connection);
  }

  // Delete the relay
  delete it->relay;

  qDebug() << "[qtPilot] Unsubscribed" << subscriptionId << "from" << it->objectId
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
    qDebug() << "[qtPilot] Unsubscribed all" << toRemove.size() << "subscriptions for" << objectId;
  }
}

void SignalMonitor::setLifecycleNotificationsEnabled(bool enabled) {
  QMutexLocker lock(&m_mutex);
  m_lifecycleEnabled = enabled;
  qDebug() << "[qtPilot] Lifecycle notifications" << (enabled ? "enabled" : "disabled");
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
    qDebug() << "[qtPilot] Auto-unsubscribed" << id << "due to object destruction";
    m_subscriptions.remove(id);
  }
}

void SignalMonitor::deliverNotification(const QJsonObject& notification) {
  // Re-emit on the monitor's thread; the relay marshals here via invokeMethod.
  Q_EMIT signalEmitted(notification);
}

}  // namespace qtPilot
