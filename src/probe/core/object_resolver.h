// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "transport/jsonrpc_handler.h"  // For QTPILOT_EXPORT

#include <atomic>

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>

namespace qtPilot {

/// @brief Multi-style object ID resolver.
///
/// Resolves object identifiers in three formats:
/// 1. Numeric shorthand: "#N" or plain digits (session-scoped IDs)
/// 2. Symbolic names: Looked up via SymbolicNameMap
/// 3. Hierarchical paths: Passed directly to ObjectRegistry::findById()
///
/// Resolution is tried in the order above. The first successful match wins.
///
/// Numeric IDs are session-scoped: call clearNumericIds() on client
/// disconnect to prevent stale references.
class QTPILOT_EXPORT ObjectResolver {
 public:
  /// @brief Resolve an object identifier to a QObject pointer.
  ///
  /// Tries resolution in order:
  /// 1. Numeric ID (starts with '#' or is all digits)
  /// 2. Symbolic name (via SymbolicNameMap)
  /// 3. Hierarchical path (via ObjectRegistry::findById())
  ///
  /// @param id The object identifier string.
  /// @return The resolved QObject, or nullptr if not found.
  static QObject* resolve(const QString& id);

  /// @brief Assign a numeric shorthand ID to an object.
  /// @param obj The object to assign an ID to.
  /// @return The assigned numeric ID (monotonically increasing from 1).
  static int assignNumericId(QObject* obj);

  /// @brief Look up an object by its numeric ID.
  /// @param numericId The numeric ID to look up.
  /// @return The object, or nullptr if not found or deleted.
  static QObject* findByNumericId(int numericId);

  /// @brief Clear all numeric ID assignments.
  ///
  /// Call on client disconnect to reset session-scoped IDs.
  static void clearNumericIds();

  /// @brief Get the numeric ID for an object, if assigned.
  /// @param obj The object to look up.
  /// @return The numeric ID, or -1 if not assigned.
  static int numericIdFor(QObject* obj);

 private:
  static QHash<int, QPointer<QObject>> s_numericIds;
  static QHash<QObject*, int> s_objectToNumericId;
  static std::atomic<int> s_nextId;
};

}  // namespace qtPilot
