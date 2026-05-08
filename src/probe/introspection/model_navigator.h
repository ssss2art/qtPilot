// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "core/probe.h"  // For QTPILOT_EXPORT

#include <QAbstractItemModel>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QRegularExpression>
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
class QTPILOT_EXPORT ModelNavigator {
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

  /// @brief Retrieve model data at `parentPath`, with smart pagination and role filtering.
  ///
  /// Smart pagination rules (applied only when limit <= 0):
  /// - totalRows <= 100: return all rows
  /// - totalRows > 100: return first 100 rows
  /// Rows returned carry a `path` field relative to the model root.
  /// ensureFetched is called at every level of the path walk and at the
  /// destination parent before counting rows.
  ///
  /// @param model The model to read data from.
  /// @param parentPath Row path to the parent node; empty = root.
  /// @param offset Row offset among children of `parentPath`.
  /// @param limit Max rows; -1 = auto.
  /// @param roles Role IDs; empty = Qt::DisplayRole only.
  /// @return JSON object with parent, rows, totalRows, totalColumns, offset, limit, hasMore.
  static QJsonObject getModelData(QAbstractItemModel* model, const QList<int>& parentPath,
                                  int offset = 0, int limit = -1, const QList<int>& roles = {});

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

  /// @brief Force-load lazy children at parentIdx until canFetchMore returns false.
  ///
  /// Qt lazy models (e.g., QFileSystemModel) may report rowCount=0 until
  /// fetchMore is called. This helper drains the fetch queue so subsequent
  /// rowCount/index calls see the full set of children.
  /// @param model The model whose children to fetch.
  /// @param parentIdx The parent index. Default (invalid) = root.
  static void ensureFetched(QAbstractItemModel* model,
                            const QModelIndex& parentIdx = QModelIndex());

  /// @brief Walk a row path, calling ensureFetched at each level.
  ///
  /// Each segment indexes into column 0 of the current parent. Returns an
  /// invalid QModelIndex for the root (empty path). If any segment is out
  /// of range, returns an invalid index and (if outFailedSegment is set)
  /// writes the 0-based index of the failing segment.
  /// @param model The model to walk.
  /// @param path  Row indices, one per tree level.
  /// @param outFailedSegment Optional output: index of first failing segment.
  /// @return QModelIndex for the target node, or invalid on failure.
  static QModelIndex pathToIndex(QAbstractItemModel* model, const QList<int>& path,
                                 int* outFailedSegment = nullptr);

  /// @brief Walk a text path, matching each segment against cell display text.
  ///
  /// Matching is exact and case-sensitive. First matching row at each level
  /// is taken. Returns a row-identity QModelIndex (column 0). If no match at
  /// some level, returns invalid; (if set) writes segment index to output.
  /// @param model The model to walk.
  /// @param itemPath Display text at each level.
  /// @param matchColumn Column whose display data is matched against each segment.
  /// @param outFailedSegment Optional output: first failing segment index.
  static QModelIndex textPathToIndex(QAbstractItemModel* model, const QStringList& itemPath,
                                     int matchColumn, int* outFailedSegment = nullptr);

  /// @brief Convert a QModelIndex to a {path, cells, hasChildren} JSON row.
  ///
  /// `path` is built by walking up via parent(). `cells` contains one entry
  /// per column at the index's level, each carrying the requested roles.
  /// Does not fetchMore — caller is responsible if iterating children.
  /// @param model The model.
  /// @param index The index to serialize.
  /// @param roles Role IDs to fetch per cell. Empty → Qt::DisplayRole only.
  static QJsonObject indexToRowData(QAbstractItemModel* model, const QModelIndex& index,
                                    const QList<int>& roles = {});

  /// Match modes for findRecursive.
  enum class MatchMode { Exact, Contains, StartsWith, EndsWith, Regex };

  /// Options bag for findRecursive.
  struct FindOptions {
    QString value;
    int column = 0;
    int role = Qt::DisplayRole;
    MatchMode match = MatchMode::Contains;
    int maxHits = 10;  // -1 = unlimited
    // Compiled regex lives on the options struct so the walker reuses it.
    QRegularExpression compiledRegex;
  };

  /// @brief Pre-compile regex (if applicable) and return false on bad pattern.
  /// @param opts Options to compile. `opts.compiledRegex` is updated in place.
  /// @param outError Optional error message on failure.
  static bool compileFindOptions(FindOptions& opts, QString* outError = nullptr);

  /// @brief Depth-first search of `parent`'s subtree for rows whose cell at
  /// `opts.column` matches `opts.value` under `opts.role` using `opts.match`.
  /// Calls ensureFetched before every rowCount/index. Appends row data (via
  /// indexToRowData) to `outMatches`. Returns true if truncated at maxHits.
  /// @param model The model.
  /// @param parentIdx Starting parent (invalid = root).
  /// @param opts Find options (regex must be pre-compiled via compileFindOptions).
  /// @param outMatches JSON array to append matches to.
  /// @return true if the walk was truncated by maxHits; false if it ran to completion.
  static bool findRecursive(QAbstractItemModel* model, const QModelIndex& parentIdx,
                            FindOptions& opts, QJsonArray& outMatches);

 private:
  ModelNavigator() = delete;  // Purely static, no instantiation
};

}  // namespace qtPilot
