// Copyright (c) 2024 QtMCP Contributors
// SPDX-License-Identifier: MIT

#include "core/object_registry.h"

#include "introspection/object_id.h"

#include <atomic>

#include <QCoreApplication>
#include <QDebug>
#include <QGlobalStatic>
#include <QMutexLocker>

// Qt private header for hook access
#include <private/qhooks_p.h>

namespace {

// Store previous callbacks for daisy-chaining (preserve GammaRay coexistence)
QHooks::AddQObjectCallback g_previousAddCallback = nullptr;
QHooks::RemoveQObjectCallback g_previousRemoveCallback = nullptr;

// Flag to track if hooks are installed
bool g_hooksInstalled = false;

// Flag to indicate singleton is being created (guards against re-entry)
// Using std::atomic instead of thread_local to avoid TLS issues with injected DLLs
std::atomic<bool> g_singletonCreating{false};

}  // namespace

// Hook callbacks - these are called by Qt for every QObject creation/destruction
// They must be minimal and thread-safe.

void qtmcpAddObjectHook(QObject* obj) {
  // Guard against re-entry during ObjectRegistry singleton creation
  // When the singleton is being created, skip registration to avoid recursion
  if (g_singletonCreating.load(std::memory_order_acquire)) {
    // Chain to previous callback only
    if (g_previousAddCallback) {
      g_previousAddCallback(obj);
    }
    return;
  }

  // Register the object
  qtmcp::ObjectRegistry::instance()->registerObject(obj);

  // Chain to previous callback (e.g., GammaRay)
  if (g_previousAddCallback) {
    g_previousAddCallback(obj);
  }
}

void qtmcpRemoveObjectHook(QObject* obj) {
  // Guard against re-entry during singleton creation
  if (g_singletonCreating.load(std::memory_order_acquire)) {
    if (g_previousRemoveCallback) {
      g_previousRemoveCallback(obj);
    }
    return;
  }

  // Unregister the object
  qtmcp::ObjectRegistry::instance()->unregisterObject(obj);

  // Chain to previous callback
  if (g_previousRemoveCallback) {
    g_previousRemoveCallback(obj);
  }
}

namespace qtmcp {

// Thread-safe singleton storage using Q_GLOBAL_STATIC
Q_GLOBAL_STATIC(ObjectRegistry, s_objectRegistryInstance)

ObjectRegistry* ObjectRegistry::instance() {
  // Set flag before accessing singleton (which may trigger creation)
  // This guards against re-entry from hook callbacks during construction
  bool wasCreating = g_singletonCreating.exchange(true, std::memory_order_acq_rel);

  ObjectRegistry* inst = s_objectRegistryInstance();

  // Only clear flag if we were the one who set it
  if (!wasCreating) {
    g_singletonCreating.store(false, std::memory_order_release);
  }

  return inst;
}

ObjectRegistry::ObjectRegistry() : QObject(nullptr) {
  // Log creation for debugging - use fprintf to avoid potential qDebug issues
  // during singleton initialization
  fprintf(stderr, "[QtMCP] ObjectRegistry created\n");
}

ObjectRegistry::~ObjectRegistry() {
  // CRITICAL: Uninstall hooks before destroying the registry
  // Otherwise, object destructions during our destruction will call
  // into unregisterObject on a partially-destroyed object
  uninstallObjectHooks();

  fprintf(stderr, "[QtMCP] ObjectRegistry destroyed\n");
}

void ObjectRegistry::registerObject(QObject* obj) {
  if (!obj) {
    return;
  }

  // Don't register during destruction
  if (s_objectRegistryInstance.isDestroyed()) {
    return;
  }

  {
    QMutexLocker lock(&m_mutex);
    m_objects.insert(obj);

    // Generate and cache the hierarchical ID
    QString id = generateObjectId(obj);

    // Handle potential ID collision by appending unique suffix
    // This can happen if hierarchy changes or two objects have identical paths
    if (m_idToObject.contains(id)) {
      // Check if existing entry is for a deleted object
      QObject* existing = m_idToObject.value(id).data();
      if (existing && existing != obj) {
        // Collision with live object - append suffix
        int suffix = 1;
        QString uniqueId;
        do {
          uniqueId = id + QStringLiteral("~") + QString::number(suffix++);
        } while (m_idToObject.contains(uniqueId));
        id = uniqueId;
      }
    }

    m_objectToId.insert(obj, id);
    m_idToObject.insert(id, QPointer<QObject>(obj));
  }

  // Emit signal on main thread to avoid threading issues with slots
  // Use QueuedConnection to ensure the signal is delivered asynchronously
  // Skip if no event loop (e.g., during shutdown)
  if (QCoreApplication::instance()) {
    QMetaObject::invokeMethod(
        this,
        [this, obj]() {
          // Double-check object still exists before emitting
          // (it could have been destroyed between hook and signal delivery)
          {
            QMutexLocker lock(&m_mutex);
            if (!m_objects.contains(obj)) {
              return;
            }
          }
          // Refresh ID now that construction is complete — objectName
          // and parent are likely set by this point
          refreshObjectId(obj);

          // Connect to objectNameChanged to refresh cached ID when name
          // changes post-construction. Done here (on main thread, after
          // construction) rather than in the hook callback to avoid
          // thread safety issues with cross-thread signal connections.
          connect(obj, &QObject::objectNameChanged, this, [this, obj]() {
            QMutexLocker lock(&m_mutex);
            if (!m_objects.contains(obj)) {
              return;
            }
            lock.unlock();
            refreshObjectId(obj);
            refreshDescendantIds(obj);
          }, Qt::QueuedConnection);

          emit objectAdded(obj);
        },
        Qt::QueuedConnection);
  }
}

void ObjectRegistry::unregisterObject(QObject* obj) {
  if (!obj) {
    return;
  }

  // Don't try to modify the set during destruction - it may already be
  // in an inconsistent state
  if (s_objectRegistryInstance.isDestroyed()) {
    return;
  }

  {
    QMutexLocker lock(&m_mutex);
    m_objects.remove(obj);

    // Remove from ID maps using cached ID (don't regenerate)
    QString id = m_objectToId.take(obj);
    if (!id.isEmpty()) {
      m_idToObject.remove(id);

      // Clean up alias entries that point to this object's current ID
      auto aliasIt = m_oldToNewId.begin();
      while (aliasIt != m_oldToNewId.end()) {
        if (aliasIt.value() == id || aliasIt.key() == id) {
          aliasIt = m_oldToNewId.erase(aliasIt);
        } else {
          ++aliasIt;
        }
      }
    }
  }

  // Emit signal on main thread (skip if no event loop or during shutdown)
  if (QCoreApplication::instance()) {
    QMetaObject::invokeMethod(
        this, [this, obj]() { emit objectRemoved(obj); }, Qt::QueuedConnection);
  }
}

QObject* ObjectRegistry::findByObjectName(const QString& name, QObject* root) {
  QMutexLocker lock(&m_mutex);

  if (root) {
    // Search within root's subtree
    if (root->objectName() == name) {
      return root;
    }
    // Use Qt's built-in recursive search
    QList<QObject*> matches = root->findChildren<QObject*>(name, Qt::FindChildrenRecursively);
    return matches.isEmpty() ? nullptr : matches.first();
  }

  // Search all tracked objects
  for (QObject* obj : std::as_const(m_objects)) {
    if (obj && obj->objectName() == name) {
      return obj;
    }
  }
  return nullptr;
}

QList<QObject*> ObjectRegistry::findAllByClassName(const QString& className, QObject* root) {
  QMutexLocker lock(&m_mutex);
  QList<QObject*> result;

  if (root) {
    // Search within root's subtree
    if (QString::fromLatin1(root->metaObject()->className()) == className) {
      result.append(root);
    }
    for (QObject* child : root->children()) {
      result.append(findAllByClassName(className, child));
    }
    return result;
  }

  // Search all tracked objects
  for (QObject* obj : std::as_const(m_objects)) {
    if (obj && QString::fromLatin1(obj->metaObject()->className()) == className) {
      result.append(obj);
    }
  }
  return result;
}

QList<QObject*> ObjectRegistry::allObjects() {
  QMutexLocker lock(&m_mutex);
  return m_objects.values();
}

int ObjectRegistry::objectCount() const {
  QMutexLocker lock(&m_mutex);
  return m_objects.size();
}

bool ObjectRegistry::contains(QObject* obj) const {
  QMutexLocker lock(&m_mutex);
  return m_objects.contains(obj);
}

QString ObjectRegistry::objectId(QObject* obj) {
  if (!obj) {
    return QString();
  }

  QMutexLocker lock(&m_mutex);

  // Return cached ID if available
  auto it = m_objectToId.constFind(obj);
  if (it != m_objectToId.constEnd()) {
    return it.value();
  }

  // Object not in cache (shouldn't happen if hooks are working)
  // Generate ID on-the-fly but don't cache (object may not be tracked)
  return generateObjectId(obj);
}

QObject* ObjectRegistry::findById(const QString& id) {
  if (id.isEmpty()) {
    return nullptr;
  }

  QMutexLocker lock(&m_mutex);

  // Look up in cached map first
  auto it = m_idToObject.constFind(id);
  if (it != m_idToObject.constEnd()) {
    QObject* obj = it.value().data();
    if (obj) {
      return obj;
    }
    // Object was deleted but entry remains - clean it up
    m_idToObject.remove(id);
  }

  // Check if this is a stale ID that was refreshed
  auto aliasIt = m_oldToNewId.constFind(id);
  if (aliasIt != m_oldToNewId.constEnd()) {
    auto newIt = m_idToObject.constFind(aliasIt.value());
    if (newIt != m_idToObject.constEnd()) {
      QObject* obj = newIt.value().data();
      if (obj) {
        return obj;
      }
    }
  }

  // Fall back to tree search using object_id module
  // This handles cases where ID wasn't cached (e.g., manual search)
  return findByObjectId(id);
}

void ObjectRegistry::refreshObjectId(QObject* obj) {
  if (!obj) {
    return;
  }

  QString oldId;
  QString newId;

  {
    QMutexLocker lock(&m_mutex);
    if (!m_objects.contains(obj)) {
      return;
    }

    oldId = m_objectToId.value(obj);
    newId = generateObjectId(obj);

    if (oldId == newId) {
      return;  // No change
    }

    // Handle collision on the new ID
    if (m_idToObject.contains(newId)) {
      QObject* existing = m_idToObject.value(newId).data();
      if (existing && existing != obj) {
        int suffix = 1;
        QString uniqueId;
        do {
          uniqueId = newId + QStringLiteral("~") + QString::number(suffix++);
        } while (m_idToObject.contains(uniqueId));
        newId = uniqueId;
      }
    }

    // Update maps
    m_idToObject.remove(oldId);
    m_objectToId.insert(obj, newId);
    m_idToObject.insert(newId, QPointer<QObject>(obj));

    // Store alias for backward compatibility. Also update any existing
    // aliases that pointed to oldId so we don't need chain-following.
    if (!oldId.isEmpty()) {
      for (auto it = m_oldToNewId.begin(); it != m_oldToNewId.end(); ++it) {
        if (it.value() == oldId) {
          it.value() = newId;
        }
      }
      m_oldToNewId.insert(oldId, newId);
    }
  }

  // Emit outside the lock to avoid deadlocks in connected slots
  if (QCoreApplication::instance()) {
    emit objectIdChanged(obj, oldId, newId);
  }
}

void ObjectRegistry::refreshDescendantIds(QObject* obj) {
  if (!obj) {
    return;
  }

  for (QObject* child : obj->children()) {
    refreshObjectId(child);
    refreshDescendantIds(child);
  }
}

void ObjectRegistry::scanExistingObjects(QObject* root) {
  if (!root) {
    return;
  }

  // Register this object if not already tracked
  {
    QMutexLocker lock(&m_mutex);
    if (!m_objects.contains(root)) {
      m_objects.insert(root);

      // Generate and cache ID for scanned object
      QString id = generateObjectId(root);

      // Handle potential collision (same logic as registerObject)
      if (m_idToObject.contains(id)) {
        QObject* existing = m_idToObject.value(id).data();
        if (existing && existing != root) {
          int suffix = 1;
          QString uniqueId;
          do {
            uniqueId = id + QStringLiteral("~") + QString::number(suffix++);
          } while (m_idToObject.contains(uniqueId));
          id = uniqueId;
        }
      }

      m_objectToId.insert(root, id);
      m_idToObject.insert(id, QPointer<QObject>(root));

      // Connect objectNameChanged for future name changes.
      // scanExistingObjects runs on the main thread during Probe::initialize(),
      // so we can connect directly without the queued indirection used by
      // registerObject().
      connect(root, &QObject::objectNameChanged, this, [this, root]() {
        QMutexLocker lk(&m_mutex);
        if (!m_objects.contains(root)) {
          return;
        }
        lk.unlock();
        refreshObjectId(root);
        refreshDescendantIds(root);
      }, Qt::QueuedConnection);

      // Don't emit signal for pre-existing objects to avoid noise
      // during initialization
    }
  }

  // Recursively process children
  for (QObject* child : root->children()) {
    scanExistingObjects(child);
  }
}

void installObjectHooks() {
  if (g_hooksInstalled) {
    qWarning() << "[QtMCP] Object hooks already installed";
    return;
  }

  // Verify hook version compatibility
  // qtHookData[QHooks::HookDataVersion] contains the version number
  quintptr hookVersion = qtHookData[QHooks::HookDataVersion];
  if (hookVersion < 1) {
    qWarning() << "[QtMCP] qtHookData version too old:" << hookVersion;
    return;
  }

  qDebug() << "[QtMCP] Installing object hooks (qtHookData version:" << hookVersion << ")";

  // Save existing callbacks for daisy-chaining
  g_previousAddCallback =
      reinterpret_cast<QHooks::AddQObjectCallback>(qtHookData[QHooks::AddQObject]);
  g_previousRemoveCallback =
      reinterpret_cast<QHooks::RemoveQObjectCallback>(qtHookData[QHooks::RemoveQObject]);

  if (g_previousAddCallback) {
    qDebug() << "[QtMCP] Daisy-chaining to existing AddQObject hook";
  }
  if (g_previousRemoveCallback) {
    qDebug() << "[QtMCP] Daisy-chaining to existing RemoveQObject hook";
  }

  // Install our callbacks
  qtHookData[QHooks::AddQObject] = reinterpret_cast<quintptr>(&qtmcpAddObjectHook);
  qtHookData[QHooks::RemoveQObject] = reinterpret_cast<quintptr>(&qtmcpRemoveObjectHook);

  g_hooksInstalled = true;
  qDebug() << "[QtMCP] Object hooks installed successfully";
}

void uninstallObjectHooks() {
  if (!g_hooksInstalled) {
    return;
  }

  qDebug() << "[QtMCP] Uninstalling object hooks";

  // Restore previous callbacks (or nullptr)
  qtHookData[QHooks::AddQObject] = reinterpret_cast<quintptr>(g_previousAddCallback);
  qtHookData[QHooks::RemoveQObject] = reinterpret_cast<quintptr>(g_previousRemoveCallback);

  g_previousAddCallback = nullptr;
  g_previousRemoveCallback = nullptr;
  g_hooksInstalled = false;

  qDebug() << "[QtMCP] Object hooks uninstalled";
}

}  // namespace qtmcp
