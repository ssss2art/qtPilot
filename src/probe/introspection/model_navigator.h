// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <QAbstractItemModel>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace qtPilot {

/// @brief Utility class for QAbstractItemModel discovery, data retrieval,
/// and view-to-model resolution.
///
/// Purely static utility class (no QObject inheritance). Discovers models
/// via ObjectRegistry, retrieves data with smart pagination, resolves
/// view objectIds to their underlying model, and handles role name lookup.
///
/// Smart pagination: models with <=100 rows return all data by default;
/// larger models return the first 100 rows per page.
class ModelNavigator {
 public:
  /// @brief Discover all QAbstractItemModel instances via ObjectRegistry.
  ///
  /// Iterates ObjectRegistry::allObjects(), casts each to QAbstractItemModel*.
  /// Skips internal Qt models (className containing "Internal").
  ///
  /// @return JSON array of model info objects, each with:
  ///   objectId, className, rowCount, columnCount, roleNames
  static QJsonArray listModels();

  /// @brief Get detailed info about a specific model.
  ///
  /// @param model The model to inspect.
  /// @return JSON object with rowCount, columnCount, roleNames,
  ///         hasChildren, className.
  static QJsonObject getModelInfo(QAbstractItemModel* model);

  /// @brief Retrieve model data with smart pagination.
  ///
  /// Smart pagination rules:
  /// - If limit <= 0 and totalRows <= 100: return all rows
  /// - If limit <= 0 and totalRows > 100: return first 100 rows
  /// - Otherwise: use provided offset/limit
  ///
  /// @param model The model to read data from.
  /// @param offset Row offset (default 0).
  /// @param limit Max rows to return (-1 = auto: all if <100, else 100).
  /// @param roles List of role IDs to fetch (empty = Qt::DisplayRole only).
  /// @param parentRow Parent row for tree navigation (-1 = root).
  /// @param parentCol Parent column for tree navigation (default 0).
  /// @return JSON object with rows, totalRows, totalColumns, offset,
  ///         limit, hasMore.
  static QJsonObject getModelData(QAbstractItemModel* model, int offset = 0, int limit = -1,
                                  const QList<int>& roles = {}, int parentRow = -1,
                                  int parentCol = 0);

  /// @brief Resolve a QObject to its underlying QAbstractItemModel.
  ///
  /// Resolution order:
  /// 1. qobject_cast<QAbstractItemModel*>(obj) - direct model
  /// 2. qobject_cast<QAbstractItemView*>(obj) - widget view, call view->model()
  /// 3. obj->property("model") - QML view fallback
  ///
  /// @param obj The object to resolve (model or view).
  /// @return The resolved model, or nullptr if resolution fails.
  static QAbstractItemModel* resolveModel(QObject* obj);

  /// @brief Resolve a role name string to its integer role ID.
  ///
  /// Checks model->roleNames() first, then standard Qt roles
  /// (display, edit, decoration, toolTip, etc.).
  ///
  /// @param model The model for custom role lookup.
  /// @param roleName The role name to resolve (e.g., "display", "edit").
  /// @return The role ID, or -1 if not found.
  static int resolveRoleName(QAbstractItemModel* model, const QString& roleName);

  /// @brief Get all role names for a model as JSON.
  ///
  /// Converts model->roleNames() QHash<int,QByteArray> to a JSON object
  /// with string keys (role ID) and string values (role name).
  ///
  /// @param model The model to get role names from.
  /// @return JSON object: {"0": "display", "1": "decoration", ...}
  static QJsonObject getRoleNames(QAbstractItemModel* model);

 private:
  ModelNavigator() = delete;  // Purely static, no instantiation
};

}  // namespace qtPilot
