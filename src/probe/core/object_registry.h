// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "probe.h"  // For QTPILOT_EXPORT

#include <atomic>

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QRecursiveMutex>
#include <QSet>

// Forward declarations for hook callbacks (global functions)
void qtpilotAddObjectHook(QObject*);
void qtpilotRemoveObjectHook(QObject*);

namespace qtPilot {

/// @brief Registry that tracks all QObjects in the target application.
///
/// Uses Qt's private qtHookData hooks (AddQObject/RemoveQObject) to track
/// object creation and destruction. This is the same mechanism used by
/// KDAB's GammaRay, ensuring compatibility and proven reliability.
///
/// Thread Safety: All public methods are thread-safe. Hook callbacks may be
/// called from any thread, so the registry uses QRecursiveMutex for protection.
///
/// Usage: Call installObjectHooks() after QCoreApplication is created to
/// start tracking objects. Call uninstallObjectHooks() before shutdown.
///
/// Object IDs: Each tracked object gets a hierarchical ID in the format
/// "parent/child/grandchild" where segments prefer objectName, then text
/// property, then ClassName#N. IDs are computed lazily on first introspection
/// (objectId()) — not eagerly at registration — so injecting into apps that build
/// very large object graphs at startup stays cheap. Once computed, an ID is cached
/// and automatically refreshed when objectName changes, so it stays human-readable
/// as names are set post-construction. Stale IDs remain resolvable via an internal
/// alias map for backward compatibility.
class QTPILOT_EXPORT ObjectRegistry : public QObject {
  Q_OBJECT

 public:
  /// @brief Get the singleton instance.
  /// @return Pointer to the global ObjectRegistry instance.
  static ObjectRegistry* instance();

  /// @brief Enable/disable eager per-object tracking (IDs + objectAdded notifications).
  /// Off by default: until a client connects, registerObject() only records the object
  /// pointer and IDs are computed lazily (see objectId()). This keeps injection into apps
  /// that build large object graphs at startup O(1) per object instead of O(N) eager work.
  /// The probe flips this on when the first WebSocket client connects.
  void setClientConnected(bool connected);

  /// @brief Find object by objectName.
  /// @param name The objectName to search for.
  /// @param root Optional root object to search within (nullptr = all objects).
  /// @return The first matching object, or nullptr if not found.
  QObject* findByObjectName(const QString& name, QObject* root = nullptr);

  /// @brief Find all objects of a given class name.
  /// @param className The class name to search for (e.g., "QPushButton").
  /// @param root Optional root object to search within (nullptr = all objects).
  /// @return List of all matching objects.
  QList<QObject*> findAllByClassName(const QString& className, QObject* root = nullptr);

  /// @brief Get all tracked objects.
  /// @return List of all objects currently in the registry.
  QList<QObject*> allObjects();

  /// @brief Get the number of tracked objects.
  /// @return Object count.
  int objectCount() const;

  /// @brief Check if an object is tracked.
  /// @param obj The object to check.
  /// @return true if the object is in the registry.
  bool contains(QObject* obj) const;

  /// @brief Get the cached hierarchical ID for an object.
  ///
  /// Returns the current cached ID, which is automatically refreshed when
  /// objectName changes. If the object isn't tracked, generates ID on-the-fly.
  ///
  /// @param obj The object to get the ID for.
  /// @return The hierarchical ID string, or empty string if obj is null.
  QString objectId(QObject* obj);

  /// @brief Find an object by its hierarchical ID.
  ///
  /// Looks up the object in the cached ID-to-object map. This is O(1) for
  /// direct hits but will search the object tree if not found in cache.
  ///
  /// @param id The hierarchical ID (e.g., "mainWindow/central/submitBtn").
  /// @return The object, or nullptr if not found or object was deleted.
  QObject* findById(const QString& id);

  /// @brief Scan and register all existing objects in a tree.
  ///
  /// Used to capture objects created before hook installation.
  /// Call this after installObjectHooks() on each top-level object.
  /// @param root The root object to scan recursively.
  void scanExistingObjects(QObject* root);

  /// @brief Seed the registry from every root the object graph actually has.
  ///
  /// The creation hook only sees objects built after the probe attaches, so a
  /// host application's pre-existing graph must be walked once at startup. The
  /// roots are not just the application: a parentless top-level QWindow is not a
  /// QObject child of the app, and neither is a parentless top-level QWidget --
  /// which is the ordinary shape of a main window. Kept here rather than inline
  /// in Probe::initialize() so the root set is one definition and can be tested.
  void scanAllExistingObjects();

 signals:
  /// @brief Emitted when a new object is registered.
  /// @param obj The newly tracked object.
  /// @note Emitted on the main thread via QueuedConnection.
  void objectAdded(QObject* obj);

  /// @brief Emitted when an object is unregistered (about to be destroyed).
  /// @param obj The object being removed.
  /// @note Emitted on the main thread via QueuedConnection.
  void objectRemoved(QObject* obj);

  /// @brief Emitted when an object's cached ID is refreshed.
  /// @param obj The object whose ID changed.
  /// @param oldId The previous cached ID.
  /// @param newId The new cached ID.
  void objectIdChanged(QObject* obj, const QString& oldId, const QString& newId);

 private:
  // Hook callback friends - these are called from Qt's internal hooks
  // Note: These are global functions (not in namespace) for Qt hook compatibility
  friend void ::qtpilotAddObjectHook(QObject*);
  friend void ::qtpilotRemoveObjectHook(QObject*);

  /// @brief Register an object (called from hook).
  /// @param obj The object to register.
  void registerObject(QObject* obj);

  /// @brief Unregister an object (called from hook).
  /// @param obj The object to unregister.
  void unregisterObject(QObject* obj);

  /// @brief Recompute and update the cached ID for an object.
  /// Called when objectName changes or after construction completes.
  void refreshObjectId(QObject* obj);

  /// @brief Refresh IDs for all descendants of an object.
  /// Needed because child IDs include parent path segments.
  void refreshDescendantIds(QObject* obj);

  /// @brief Bounded, cycle-safe body of refreshDescendantIds().
  void refreshDescendantIdsImpl(QObject* obj, int depth);

  /// @brief Bounded, cycle-safe body of scanExistingObjects().
  void scanExistingObjectsImpl(QObject* root, QSet<QObject*>& visited, int depth);

  /// @brief Make an ID unique against m_idToObject, appending `~N` if needed.
  ///
  /// Caller must hold m_mutex. The suffix counter is monotonic per base ID, so a
  /// suffix freed by a destroyed object is never handed to a different object.
  QString allocateUniqueIdLocked(const QString& baseId, QObject* obj);

  /// @brief Lazily wire objectName-change auto-refresh for an object and its ancestors.
  /// Called from objectId() the first time an object is introspected, so never-queried
  /// objects cost nothing. Caller must hold m_mutex. Idempotent via m_nameTracked.
  void ensureNameTrackingLocked(QObject* obj);

  /// @brief Set of tracked objects.
  QSet<QObject*> m_objects;

  /// @brief Map from object pointer to its current hierarchical ID.
  /// Refreshed automatically when objectName changes post-construction.
  QHash<QObject*, QString> m_objectToId;

  /// @brief Map from hierarchical ID to object pointer.
  /// Uses QPointer to safely detect deleted objects.
  QHash<QString, QPointer<QObject>> m_idToObject;

  /// @brief Alias map from old (stale) IDs to current IDs.
  /// Ensures backward compatibility when clients hold stale IDs.
  QHash<QString, QString> m_oldToNewId;

  /// @brief Monotonic per-base-ID collision counter.
  /// Makes ID uniquification O(1) amortized instead of the previous O(k) linear
  /// probe. Apps that build large object graphs at startup (thousands of same-type
  /// sibling QObjects sharing a base ID) otherwise made registration O(N^2).
  QHash<QString, int> m_idCollisionCounter;

  /// @brief When false (no client connected yet), registerObject() defers all ID/
  /// notification work — only the object pointer is stored. Atomic because the QObject
  /// creation hook can fire from any thread.
  std::atomic<bool> m_clientConnected{false};

  /// @brief Objects whose objectName-change auto-refresh has been wired (see
  /// ensureNameTrackingLocked). Prevents duplicate connections when an object (or an
  /// ancestor shared by many children) is introspected more than once.
  QSet<QObject*> m_nameTracked;

  /// @brief Mutex for thread-safe access.
  /// Must be recursive because hook callbacks may nest.
  mutable QRecursiveMutex m_mutex;

 public:
  // Constructor/destructor are public for Q_GLOBAL_STATIC compatibility
  // Use instance() to get the singleton - do not construct directly
  ObjectRegistry();
  ~ObjectRegistry() override;
};

/// @brief Install Qt object lifecycle hooks.
///
/// Installs AddQObject and RemoveQObject hooks into Qt's qtHookData array.
/// Existing hooks are preserved via daisy-chaining for tool coexistence
/// (e.g., GammaRay can run alongside qtPilot).
///
/// Must be called AFTER QCoreApplication exists.
/// Should be called from Probe::initialize().
QTPILOT_EXPORT void installObjectHooks();

/// @brief Uninstall Qt object lifecycle hooks.
///
/// Restores previous hooks (or nullptr if there were none).
/// Should be called from Probe::shutdown().
QTPILOT_EXPORT void uninstallObjectHooks();

}  // namespace qtPilot
