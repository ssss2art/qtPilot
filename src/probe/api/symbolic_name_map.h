// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "transport/jsonrpc_handler.h"  // For QTPILOT_EXPORT

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QMutex>
#include <QString>

namespace qtPilot {

/// @brief Squish-style symbolic name map for object aliases.
///
/// Maps human-readable symbolic names to hierarchical object paths.
/// Names can be loaded from a JSON file or registered at runtime.
///
/// Thread Safety: All public methods are thread-safe (mutex-protected).
///
/// Auto-loading: At construction, checks QTPILOT_NAME_MAP env var for a
/// file path, then falls back to "qtPilot-names.json" in CWD. If neither
/// exists, starts with an empty map (no error).
class QTPILOT_EXPORT SymbolicNameMap {
 public:
  SymbolicNameMap();
  ~SymbolicNameMap() = default;

  /// @brief Get the singleton instance.
  static SymbolicNameMap* instance();

  /// @brief Resolve a symbolic name to a hierarchical path.
  /// @param symbolicName The name to look up.
  /// @return The hierarchical path, or empty string if not found.
  QString resolve(const QString& symbolicName) const;

  /// @brief Register a name-to-path mapping.
  /// @param name The symbolic name.
  /// @param path The hierarchical object path.
  /// @note Overwrites existing mapping if name already exists.
  void registerName(const QString& name, const QString& path);

  /// @brief Remove a name mapping.
  /// @param name The symbolic name to remove.
  void unregisterName(const QString& name);

  /// @brief Get all name-to-path mappings as a JSON object.
  /// @return JSON object with {"name": "path", ...} entries.
  QJsonObject allNames() const;

  /// @brief Load mappings from a JSON file.
  ///
  /// File format: {"name1": "path1", "name2": "path2", ...}
  /// Clears existing map before loading.
  ///
  /// @param filePath Path to the JSON file.
  /// @return true on success, false on failure (file not found, parse error).
  bool loadFromFile(const QString& filePath);

  /// @brief Save current mappings to a JSON file.
  /// @param filePath Path to save the JSON file.
  /// @return true on success, false on failure.
  bool saveToFile(const QString& filePath) const;

  /// @brief Check if all symbolic names resolve to existing objects.
  /// @return true if all names resolve, false if any are stale.
  bool validateAll() const;

  /// @brief Validate each name and return detailed results.
  /// @return Array of {"name": ..., "path": ..., "valid": true/false}.
  QJsonArray validateNames() const;

 private:
  QHash<QString, QString> m_nameMap;
  mutable QMutex m_mutex;

  /// @brief Try to auto-load a name map file.
  void autoLoad();
};

}  // namespace qtPilot
