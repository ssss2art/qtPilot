// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "core/object_registry.h"

#include "introspection/object_id.h"

#include <atomic>
#include <cstring>  // std::strcmp (not transitively included on Qt5/gcc)

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

// Set once the singleton has been fully constructed. After that, instance() takes a fast
// path that never touches g_singletonCreating. This is critical for correctness, not just
// speed: the object hooks skip (un)registration while g_singletonCreating is true, so if
// instance() toggled that flag on *every* call, a QObject destroyed on another thread
// during any instance() call would have its unregisterObject skipped — leaving a dangling
// pointer in m_objects that later crashes findAllByClassName. Only the genuine first
// construction needs the guard.
std::atomic<bool> g_singletonConstructed{false};

// Returns true if `meta` is `className` or derives from it. Walking the
// superclass chain makes className queries subclass-aware (e.g. searching
// "QPushButton" matches a custom MyButton : QPushButton), matching the
// documented behaviour of the search API.
bool metaInheritsClassName(const QMetaObject* meta, const QByteArray& className) {
  const char* target = className.constData();
  for (const QMetaObject* m = meta; m != nullptr; m = m->superClass()) {
    if (m->className() && std::strcmp(m->className(), target) == 0) {
      return true;
    }
  }
  return false;
}

// Helper to recursively search subtree for objects matching className (subclass-aware)
// Accumulates matches into the reference parameter to avoid temporary QList allocations.
void findAllByClassNameHelper(const QByteArray& className, QObject* root, QList<QObject*>& result,
                              int depth = 0) {
  if (!root || depth > qtPilot::kMaxEffectiveDepth) {
    return;
  }
  if (metaInheritsClassName(root->metaObject(), className)) {
    result.append(root);
  }
  // effectiveChildren(), not children(): a root-scoped search has to mean the
  // same thing as "under this root in the tree". Walking QObject children alone
  // stopped dead at every QML delegate, so a scoped search returned nothing for
  // objects qt.objects.tree happily listed under that very root.
  const QList<QObject*> children = qtPilot::effectiveChildren(root);
  for (QObject* child : children) {
    findAllByClassNameHelper(className, child, result, depth + 1);
  }
}

/// @brief Recursive objectName search over the effective hierarchy.
///
/// Replaces QObject::findChildren(), which cannot be taught about visual
/// parentage and therefore could not see a named QML delegate.
QObject* findByObjectNameHelper(QObject* root, const QString& name, int depth = 0) {
  if (!root || depth > qtPilot::kMaxEffectiveDepth) {
    return nullptr;
  }
  const QList<QObject*> children = qtPilot::effectiveChildren(root);
  for (QObject* child : children) {
    if (!child) {
      continue;
    }
    if (child->objectName() == name) {
      return child;
    }
    if (QObject* found = findByObjectNameHelper(child, name, depth + 1)) {
      return found;
    }
  }
  return nullptr;
}

}  // namespace

// Hook callbacks - these are called by Qt for every QObject creation/destruction
// They must be minimal and thread-safe.

void qtpilotAddObjectHook(QObject* obj) {
  try {
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
    qtPilot::ObjectRegistry::instance()->registerObject(obj);

    // Chain to previous callback (e.g., GammaRay)
    if (g_previousAddCallback) {
      g_previousAddCallback(obj);
    }
  } catch (const std::exception& e) {
    fprintf(stderr, "[qtPilot] Exception caught in qtpilotAddObjectHook: %s\n", e.what());
  } catch (...) {
    fprintf(stderr, "[qtPilot] Unknown exception caught in qtpilotAddObjectHook\n");
  }
}

void qtpilotRemoveObjectHook(QObject* obj) {
  try {
    // Guard against re-entry during singleton creation
    if (g_singletonCreating.load(std::memory_order_acquire)) {
      if (g_previousRemoveCallback) {
        g_previousRemoveCallback(obj);
      }
      return;
    }

    // Unregister the object
    qtPilot::ObjectRegistry::instance()->unregisterObject(obj);

    // Chain to previous callback
    if (g_previousRemoveCallback) {
      g_previousRemoveCallback(obj);
    }
  } catch (const std::exception& e) {
    fprintf(stderr, "[qtPilot] Exception caught in qtpilotRemoveObjectHook: %s\n", e.what());
  } catch (...) {
    fprintf(stderr, "[qtPilot] Unknown exception caught in qtpilotRemoveObjectHook\n");
  }
}

namespace qtPilot {

// Thread-safe singleton storage using Q_GLOBAL_STATIC
Q_GLOBAL_STATIC(ObjectRegistry, s_objectRegistryInstance)

ObjectRegistry* ObjectRegistry::instance() {
  // Fast path: once constructed, never touch g_singletonCreating again. Toggling it on
  // every call opens a race where a QObject destroyed on another thread mid-call has its
  // unregisterObject skipped (the remove hook honors g_singletonCreating), stranding a
  // dangling pointer in m_objects.
  if (g_singletonConstructed.load(std::memory_order_acquire)) {
    return s_objectRegistryInstance();
  }

  // First construction: guard against re-entry from hook callbacks (the ObjectRegistry is
  // itself a QObject, so constructing it fires the AddQObject hook, which calls instance()).
  bool wasCreating = g_singletonCreating.exchange(true, std::memory_order_acq_rel);

  ObjectRegistry* inst = s_objectRegistryInstance();

  // Only clear/publish if we were the one who set it (the outermost, real constructor).
  if (!wasCreating) {
    g_singletonConstructed.store(true, std::memory_order_release);
    g_singletonCreating.store(false, std::memory_order_release);
  }

  return inst;
}

void ObjectRegistry::setClientConnected(bool connected) {
  // No mutex needed: this is a plain atomic flag read by registerObject() on any thread.
  m_clientConnected.store(connected, std::memory_order_relaxed);
}

ObjectRegistry::ObjectRegistry() : QObject(nullptr) {
  // Log creation for debugging - use fprintf to avoid potential qDebug issues
  // during singleton initialization
  fprintf(stderr, "[qtPilot] ObjectRegistry created\n");
}

ObjectRegistry::~ObjectRegistry() {
  // CRITICAL: Uninstall hooks before destroying the registry
  // Otherwise, object destructions during our destruction will call
  // into unregisterObject on a partially-destroyed object
  uninstallObjectHooks();

  fprintf(stderr, "[qtPilot] ObjectRegistry destroyed\n");
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
  }

  // Defer all ID computation and notifications until a client is actually connected.
  // Until then the pull-based native API reads m_objects directly and objectId()
  // computes IDs lazily on demand, so injection stays O(1) per object even when the
  // target builds a very large object graph at startup (thousands of QObjects). Once a
  // client connects, setClientConnected(true) flips this and newly created objects get
  // the full eager treatment so live push notifications remain correct.
  if (!m_clientConnected.load(std::memory_order_relaxed)) {
    return;
  }

  // A client is connected: push a live objectAdded notification. The hierarchical ID and
  // objectName-change tracking are established lazily on first objectId() query (see
  // objectId()), so nothing else is done here. Posted via QueuedConnection because the
  // hook fires mid-construction — objectName/parent may not be set yet, and slots must run
  // on the main thread.
  if (QCoreApplication::instance()) {
    // QPointer safely detects destruction before the queued lambda runs; a raw-pointer
    // m_objects check is insufficient because a freed address can be reused.
    QPointer<QObject> weak(obj);
    QMetaObject::invokeMethod(
        this,
        [this, weak]() {
          QObject* obj = weak.data();
          if (!obj) {
            return;  // Object was destroyed before this lambda ran
          }
          {
            QMutexLocker lock(&m_mutex);
            if (!m_objects.contains(obj)) {
              return;
            }
          }
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
    m_nameTracked.remove(obj);

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
    return findByObjectNameHelper(root, name);
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
  const QByteArray classNameBytes = className.toLatin1();

  if (root) {
    findAllByClassNameHelper(classNameBytes, root, result);
    return result;
  }

  // Search all tracked objects (subclass-aware)
  for (QObject* obj : std::as_const(m_objects)) {
    if (obj && metaInheritsClassName(obj->metaObject(), classNameBytes)) {
      result.append(obj);
    }
  }
  return result;
}

void ObjectRegistry::ensureNameTrackingLocked(QObject* obj) {
  // Caller holds m_mutex. Walk from obj up to the root, wiring an objectNameChanged
  // refresh on each ancestor not yet tracked. The full walk (rather than breaking at the
  // first tracked ancestor) keeps tracking correct across reparenting; depth is small, so
  // the cost is negligible and paid only once per object across its lifetime.
  // effectiveParent(), not parent(): a delegate's cached ID embeds its VISUAL
  // ancestors' segments, so those are the ancestors whose renames must trigger a
  // refresh. The QObject-only walk terminated immediately at any delegate.
  int depth = 0;
  for (QObject* node = obj; node != nullptr && depth <= kMaxEffectiveDepth;
       node = effectiveParent(node), ++depth) {
    if (m_nameTracked.contains(node)) {
      continue;
    }
    m_nameTracked.insert(node);

    QObject* target = node;
    // QueuedConnection: the slot runs on the registry's (main) thread and re-locks the
    // mutex, so it must not run synchronously inside this locked section.
    connect(
        target, &QObject::objectNameChanged, this,
        [this, target]() {
          {
            QMutexLocker lock(&m_mutex);
            if (!m_objects.contains(target)) {
              return;
            }
          }
          refreshObjectId(target);
          refreshDescendantIds(target);
        },
        Qt::QueuedConnection);
  }
}

QList<QObject*> ObjectRegistry::allObjects() {
  QMutexLocker lock(&m_mutex);
  return m_objects.values();
}

int ObjectRegistry::objectCount() const {
  QMutexLocker lock(&m_mutex);
  // qsizetype -> int, stated explicitly for the stricter iOS warning set.
  return static_cast<int>(m_objects.size());
}

bool ObjectRegistry::contains(QObject* obj) const {
  QMutexLocker lock(&m_mutex);
  return m_objects.contains(obj);
}

QString ObjectRegistry::allocateUniqueIdLocked(const QString& baseId, QObject* obj) {
  // Caller holds m_mutex.
  //
  // One definition, because the three copies this replaces had drifted into two
  // different algorithms: one used the persistent counter below, the other two
  // restarted at 1 and probed upward. Restarting is wrong twice over -- it is
  // O(N) per collision (so O(N^2) to register N colliding siblings, paid on the
  // startup scan), and it REUSES a suffix once the previous holder is destroyed,
  // so a stale id a client is still holding silently starts resolving to a
  // different object. The counter is monotonic per base id and never reused.
  if (baseId.isEmpty()) {
    return baseId;
  }
  const QObject* existing = m_idToObject.value(baseId).data();
  if (!m_idToObject.contains(baseId) || !existing || existing == obj) {
    return baseId;
  }

  int& next = m_idCollisionCounter[baseId];
  QString uniqueId;
  do {
    uniqueId = baseId + QStringLiteral("~") + QString::number(++next);
  } while (m_idToObject.contains(uniqueId));
  return uniqueId;
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

  // Not cached yet. With deferred registration, tracked objects don't get an ID until
  // first inspected — so compute it now (construction is complete, so objectName/parent
  // are stable) and cache it. Caching here is what keeps findById() correct: a client can
  // only ever hold an ID we handed out, and handing one out populates m_idToObject.
  QString id = generateObjectId(obj);

  // Only cache IDs for objects we actually track; untracked objects get a transient ID.
  if (!m_objects.contains(obj)) {
    return id;
  }

  id = allocateUniqueIdLocked(id, obj);

  m_objectToId.insert(obj, id);
  m_idToObject.insert(id, QPointer<QObject>(obj));

  // Now that this object is being introspected, keep its cached ID fresh if objectName
  // changes post-construction — for the object itself and its ancestors (a child's ID
  // embeds its parents' path segments). Done lazily here so never-queried objects cost
  // nothing at registration time.
  ensureNameTrackingLocked(obj);
  return id;
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

    newId = allocateUniqueIdLocked(newId, obj);

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
  IdGenerationScope idScope;
  refreshDescendantIdsImpl(obj, 0);
}

void ObjectRegistry::refreshDescendantIdsImpl(QObject* obj, int depth) {
  if (!obj || depth > kMaxEffectiveDepth) {
    return;
  }

  // effectiveChildren() to match scanExistingObjects() and generateObjectId().
  // The invariant worth stating: any walk that PRODUCES ids and any walk that
  // INVALIDATES them must enumerate children the same way, or a rename leaves
  // permanently stale cached ids behind with no alias to redirect them.
  const QList<QObject*> children = effectiveChildren(obj);
  for (QObject* child : children) {
    refreshObjectId(child);
    refreshDescendantIdsImpl(child, depth + 1);
  }
}

void ObjectRegistry::scanExistingObjects(QObject* root) {
  // One scope for the whole scan: this runs on the application's main thread during
  // Probe::initialize() and generates an id for every pre-existing object, which is
  // the single largest sibling-scan workload in the probe.
  IdGenerationScope idScope;
  QSet<QObject*> visited;
  scanExistingObjectsImpl(root, visited, 0);
}

void ObjectRegistry::scanExistingObjectsImpl(QObject* root, QSet<QObject*>& visited, int depth) {
  if (!root || depth > kMaxEffectiveDepth) {
    return;
  }
  // The visited set is what makes this terminate. The effective hierarchy is not
  // guaranteed acyclic (see kMaxEffectiveDepth), and the tracked-object check
  // below only skips REGISTRATION -- it never stopped the descent, so a cycle
  // would recurse until the host application's stack overflowed. It also stops a
  // diamond from being walked twice.
  if (visited.contains(root)) {
    return;
  }
  visited.insert(root);

  // Register this object if not already tracked
  {
    QMutexLocker lock(&m_mutex);
    if (!m_objects.contains(root)) {
      m_objects.insert(root);

      // Generate and cache ID for scanned object
      QString id = generateObjectId(root);

      id = allocateUniqueIdLocked(id, root);

      m_objectToId.insert(root, id);
      m_idToObject.insert(id, QPointer<QObject>(root));

      // Connect objectNameChanged for future name changes.
      // scanExistingObjects runs on the main thread during Probe::initialize(),
      // so we can connect directly without the queued indirection used by
      // registerObject().
      connect(
          root, &QObject::objectNameChanged, this,
          [this, root]() {
            QMutexLocker lk(&m_mutex);
            if (!m_objects.contains(root)) {
              return;
            }
            lk.unlock();
            refreshObjectId(root);
            refreshDescendantIds(root);
          },
          Qt::QueuedConnection);

      // Don't emit signal for pre-existing objects to avoid noise
      // during initialization
    }
  }

  // Recursively process children.
  //
  // effectiveChildren() rather than children(): objects already built by the
  // time the probe starts are only ever found by this scan, and QML delegates
  // hang off a visual parent with no QObject parent. Walking QObject children
  // alone left every pre-existing delegate untracked, which is worse than
  // merely absent from the tree -- objectId() hands an untracked object a
  // TRANSIENT id that it deliberately does not cache, so the id a client got
  // back from a hit test could never be resolved again.
  const QList<QObject*> children = effectiveChildren(root);
  for (QObject* child : children) {
    scanExistingObjectsImpl(child, visited, depth + 1);
  }
}

void installObjectHooks() {
  if (g_hooksInstalled) {
    qWarning() << "[qtPilot] Object hooks already installed";
    return;
  }

  // Verify hook version compatibility
  // qtHookData[QHooks::HookDataVersion] contains the version number
  quintptr hookVersion = qtHookData[QHooks::HookDataVersion];
  if (hookVersion < 1) {
    qWarning() << "[qtPilot] qtHookData version too old:" << hookVersion;
    return;
  }

  qDebug() << "[qtPilot] Installing object hooks (qtHookData version:" << hookVersion << ")";

  // Save existing callbacks for daisy-chaining
  g_previousAddCallback =
      reinterpret_cast<QHooks::AddQObjectCallback>(qtHookData[QHooks::AddQObject]);
  g_previousRemoveCallback =
      reinterpret_cast<QHooks::RemoveQObjectCallback>(qtHookData[QHooks::RemoveQObject]);

  if (g_previousAddCallback) {
    qDebug() << "[qtPilot] Daisy-chaining to existing AddQObject hook";
  }
  if (g_previousRemoveCallback) {
    qDebug() << "[qtPilot] Daisy-chaining to existing RemoveQObject hook";
  }

  // Install our callbacks
  qtHookData[QHooks::AddQObject] = reinterpret_cast<quintptr>(&qtpilotAddObjectHook);
  qtHookData[QHooks::RemoveQObject] = reinterpret_cast<quintptr>(&qtpilotRemoveObjectHook);

  g_hooksInstalled = true;
  qDebug() << "[qtPilot] Object hooks installed successfully";
}

void uninstallObjectHooks() {
  if (!g_hooksInstalled) {
    return;
  }

  qDebug() << "[qtPilot] Uninstalling object hooks";

  // Restore previous callbacks (or nullptr)
  qtHookData[QHooks::AddQObject] = reinterpret_cast<quintptr>(g_previousAddCallback);
  qtHookData[QHooks::RemoveQObject] = reinterpret_cast<quintptr>(g_previousRemoveCallback);

  g_previousAddCallback = nullptr;
  g_previousRemoveCallback = nullptr;
  g_hooksInstalled = false;

  qDebug() << "[qtPilot] Object hooks uninstalled";
}

}  // namespace qtPilot
