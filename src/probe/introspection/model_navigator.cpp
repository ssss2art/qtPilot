// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "introspection/model_navigator.h"

#include "core/object_registry.h"
#include "introspection/variant_json.h"

#include <ranges>

#include <QAbstractItemView>
#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>

namespace qtPilot {

// Standard Qt role name lookup table
static const QHash<QString, int>& standardRoleNames() {
  static const QHash<QString, int> roles = {
      {QStringLiteral("display"), Qt::DisplayRole},
      {QStringLiteral("decoration"), Qt::DecorationRole},
      {QStringLiteral("edit"), Qt::EditRole},
      {QStringLiteral("toolTip"), Qt::ToolTipRole},
      {QStringLiteral("statusTip"), Qt::StatusTipRole},
      {QStringLiteral("whatsThis"), Qt::WhatsThisRole},
      {QStringLiteral("font"), Qt::FontRole},
      {QStringLiteral("textAlignment"), Qt::TextAlignmentRole},
      {QStringLiteral("background"), Qt::BackgroundRole},
      {QStringLiteral("foreground"), Qt::ForegroundRole},
      {QStringLiteral("checkState"), Qt::CheckStateRole},
      {QStringLiteral("sizeHint"), Qt::SizeHintRole},
  };
  return roles;
}

QJsonArray ModelNavigator::listModels() {
  QJsonArray result;
  auto* registry = ObjectRegistry::instance();
  const auto objects = registry->allObjects();

  auto validModelObjects = objects | std::views::filter([registry](QObject* obj) {
                             if (!obj || !registry->contains(obj))
                               return false;
                             auto* model = qobject_cast<QAbstractItemModel*>(obj);
                             if (!model)
                               return false;
                             const QString className =
                                 QString::fromLatin1(model->metaObject()->className());
                             return !(className.startsWith(QLatin1Char('Q')) &&
                                      className.contains(QStringLiteral("Internal")));
                           });

  for (QObject* obj : validModelObjects) {
    auto* model = static_cast<QAbstractItemModel*>(obj);
    const QString className = QString::fromLatin1(model->metaObject()->className());
    QJsonObject info;
    info[QStringLiteral("objectId")] = registry->objectId(obj);
    info[QStringLiteral("className")] = className;
    info[QStringLiteral("rowCount")] = model->rowCount();
    info[QStringLiteral("columnCount")] = model->columnCount();
    info[QStringLiteral("roleNames")] = getRoleNames(model);
    result.append(info);
  }

  return result;
}

QJsonObject ModelNavigator::getModelInfo(QAbstractItemModel* model) {
  QJsonObject info;
  if (!model)
    return info;

  info[QStringLiteral("className")] = QString::fromLatin1(model->metaObject()->className());
  info[QStringLiteral("rowCount")] = model->rowCount();
  info[QStringLiteral("columnCount")] = model->columnCount();
  info[QStringLiteral("hasChildren")] = model->hasChildren(QModelIndex());
  info[QStringLiteral("roleNames")] = getRoleNames(model);

  return info;
}

QJsonObject ModelNavigator::getModelData(QAbstractItemModel* model, const QList<int>& parentPath,
                                         int offset, int limit, const QList<int>& roles) {
  QJsonObject result;
  // Echo the parent path first so all early-return shapes match.
  QJsonArray parentArr;
  for (int seg : parentPath)
    parentArr.append(seg);
  result[QStringLiteral("parent")] = parentArr;

  auto fillEmpty = [&](int off, int lim) {
    result[QStringLiteral("rows")] = QJsonArray();
    result[QStringLiteral("totalRows")] = 0;
    result[QStringLiteral("totalColumns")] = 0;
    result[QStringLiteral("offset")] = off;
    result[QStringLiteral("limit")] = lim;
    result[QStringLiteral("hasMore")] = false;
  };
  if (!model) {
    fillEmpty(0, 0);
    return result;
  }

  // Resolve parent. Invalid path → empty result (handler emits kInvalidParentPath).
  QModelIndex parentIdx = pathToIndex(model, parentPath);
  if (!parentPath.isEmpty() && !parentIdx.isValid()) {
    fillEmpty(0, 0);
    return result;
  }

  ensureFetched(model, parentIdx);
  const int totalRows = model->rowCount(parentIdx);
  const int totalCols = model->columnCount(parentIdx);

  // Smart pagination.
  if (limit <= 0) {
    if (totalRows <= 100) {
      offset = 0;
      limit = totalRows;
    } else {
      limit = 100;
    }
  }
  if (offset < 0)
    offset = 0;
  if (offset > totalRows)
    offset = totalRows;
  const int endRow = qMin(offset + limit, totalRows);

  QList<int> effective = roles;
  if (effective.isEmpty())
    effective.append(Qt::DisplayRole);

  QJsonArray rowsArr;
  for (int row = offset; row < endRow; ++row) {
    const QModelIndex idx = model->index(row, 0, parentIdx);
    rowsArr.append(indexToRowData(model, idx, effective));
  }

  result[QStringLiteral("rows")] = rowsArr;
  result[QStringLiteral("totalRows")] = totalRows;
  result[QStringLiteral("totalColumns")] = totalCols;
  result[QStringLiteral("offset")] = offset;
  result[QStringLiteral("limit")] = limit;
  result[QStringLiteral("hasMore")] = (endRow < totalRows);
  return result;
}

void ModelNavigator::ensureFetched(QAbstractItemModel* model, const QModelIndex& parentIdx) {
  if (!model)
    return;
  // Drain fetchMore loop — some models fetch in batches.
  while (model->canFetchMore(parentIdx)) {
    model->fetchMore(parentIdx);
  }
}

std::expected<QModelIndex, int> ModelNavigator::pathToIndexExpected(QAbstractItemModel* model,
                                                                    const QList<int>& path) {
  if (!model) {
    return std::unexpected(0);
  }
  QModelIndex current;  // invalid = root
  for (int i = 0; i < path.size(); ++i) {
    ensureFetched(model, current);
    const int row = path[i];
    if (row < 0 || row >= model->rowCount(current)) {
      return std::unexpected(i);
    }
    current = model->index(row, 0, current);
    if (!current.isValid()) {
      return std::unexpected(i);
    }
  }
  return current;
}

QModelIndex ModelNavigator::pathToIndex(QAbstractItemModel* model, const QList<int>& path,
                                        int* outFailedSegment) {
  auto res = pathToIndexExpected(model, path);
  if (!res) {
    if (outFailedSegment) {
      *outFailedSegment = res.error();
    }
    return {};
  }
  return *res;
}

std::expected<QModelIndex, int> ModelNavigator::textPathToIndexExpected(QAbstractItemModel* model,
                                                                        const QStringList& itemPath,
                                                                        int role, int matchColumn) {
  if (!model) {
    return std::unexpected(0);
  }
  QModelIndex current;  // invalid = root
  for (int i = 0; i < itemPath.size(); ++i) {
    ensureFetched(model, current);
    const QString& wanted = itemPath[i];
    const int rows = model->rowCount(current);
    bool matched = false;
    for (int row = 0; row < rows; ++row) {
      const QModelIndex cell = model->index(row, matchColumn, current);
      if (!cell.isValid())
        continue;
      if (model->data(cell, role).toString() == wanted) {
        current = model->index(row, 0, current);  // row identity in column 0
        matched = true;
        break;
      }
    }
    if (!matched) {
      return std::unexpected(i);
    }
  }
  return current;
}

QModelIndex ModelNavigator::textPathToIndex(QAbstractItemModel* model, const QStringList& itemPath,
                                            int matchColumn, int* outFailedSegment) {
  auto res = textPathToIndexExpected(model, itemPath, Qt::DisplayRole, matchColumn);
  if (!res) {
    if (outFailedSegment) {
      *outFailedSegment = res.error();
    }
    return {};
  }
  return *res;
}

std::expected<void, QString> ModelNavigator::compileFindOptionsExpected(FindOptions& opts) {
  if (opts.match != MatchMode::Regex) {
    return {};
  }
  opts.compiledRegex.setPattern(opts.value);
  opts.compiledRegex.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
  if (!opts.compiledRegex.isValid()) {
    return std::unexpected(opts.compiledRegex.errorString());
  }
  return {};
}

bool ModelNavigator::compileFindOptions(FindOptions& opts, QString* outError) {
  auto res = compileFindOptionsExpected(opts);
  if (!res) {
    if (outError) {
      *outError = res.error();
    }
    return false;
  }
  return true;
}

namespace {
bool matchesValue(const QString& cell, const ModelNavigator::FindOptions& opts) {
  switch (opts.match) {
    case ModelNavigator::MatchMode::Exact:
      return cell.compare(opts.value, Qt::CaseInsensitive) == 0;
    case ModelNavigator::MatchMode::Contains:
      return cell.contains(opts.value, Qt::CaseInsensitive);
    case ModelNavigator::MatchMode::StartsWith:
      return cell.startsWith(opts.value, Qt::CaseInsensitive);
    case ModelNavigator::MatchMode::EndsWith:
      return cell.endsWith(opts.value, Qt::CaseInsensitive);
    case ModelNavigator::MatchMode::Regex:
      return opts.compiledRegex.match(cell).hasMatch();
    default:
      return false;
  }
}
}  // namespace

bool ModelNavigator::findRecursive(QAbstractItemModel* model, const QModelIndex& parentIdx,
                                   FindOptions& opts, QJsonArray& outMatches) {
  if (!model)
    return false;
  ensureFetched(model, parentIdx);
  const int rows = model->rowCount(parentIdx);
  for (int row = 0; row < rows; ++row) {
    const QModelIndex cellIdx = model->index(row, opts.column, parentIdx);
    if (cellIdx.isValid()) {
      const QString cellText = model->data(cellIdx, opts.role).toString();
      if (matchesValue(cellText, opts)) {
        const QModelIndex rowIdx = model->index(row, 0, parentIdx);
        outMatches.append(indexToRowData(model, rowIdx, {opts.role}));
        if (opts.maxHits >= 0 && outMatches.size() >= opts.maxHits)
          return true;
      }
    }
    const QModelIndex childParent = model->index(row, 0, parentIdx);
    if (model->hasChildren(childParent)) {
      if (findRecursive(model, childParent, opts, outMatches))
        return true;
    }
  }
  return false;
}

QAbstractItemModel* ModelNavigator::resolveModel(QObject* obj) {
  if (!obj)
    return nullptr;

  // 1. Direct model cast
  auto* model = qobject_cast<QAbstractItemModel*>(obj);
  if (model)
    return model;

  // 2. Widget view — QAbstractItemView::model()
  auto* view = qobject_cast<QAbstractItemView*>(obj);
  if (view)
    return view->model();

  // 3. QML view fallback — property("model")
  const QVariant modelProp = obj->property("model");
  if (modelProp.isValid()) {
    QObject* modelObj = modelProp.value<QObject*>();
    if (modelObj) {
      auto* resolvedModel = qobject_cast<QAbstractItemModel*>(modelObj);
      if (resolvedModel)
        return resolvedModel;
    }
  }

  return nullptr;
}

std::expected<QAbstractItemModel*, QString> ModelNavigator::resolveModelExpected(QObject* obj) {
  if (!obj) {
    return std::unexpected(QStringLiteral("Target object is null"));
  }
  auto* model = resolveModel(obj);
  if (!model) {
    return std::unexpected(
        QStringLiteral(
            "Object '%1' of type '%2' is neither a QAbstractItemModel nor has an underlying model")
            .arg(obj->objectName(), QString::fromUtf8(obj->metaObject()->className())));
  }
  return model;
}

int ModelNavigator::resolveRoleName(QAbstractItemModel* model, const QString& roleName) {
  if (!model)
    return -1;

  // 1. Check model's custom role names
  const QHash<int, QByteArray> modelRoles = model->roleNames();
  const QByteArray nameBytes = roleName.toUtf8();
  for (auto it = modelRoles.constBegin(); it != modelRoles.constEnd(); ++it) {
    if (it.value() == nameBytes)
      return it.key();
  }

  // 2. Check standard Qt roles
  const auto& stdRoles = standardRoleNames();
  auto it = stdRoles.constFind(roleName);
  if (it != stdRoles.constEnd())
    return it.value();

  return -1;
}

std::expected<int, QString> ModelNavigator::resolveRoleNameExpected(QAbstractItemModel* model,
                                                                    const QString& roleName) {
  if (!model) {
    return std::unexpected(QStringLiteral("Model is null"));
  }
  int role = resolveRoleName(model, roleName);
  if (role < 0) {
    return std::unexpected(QStringLiteral("Unknown role name: '%1'").arg(roleName));
  }
  return role;
}

QJsonObject ModelNavigator::indexToRowData(QAbstractItemModel* model, const QModelIndex& index,
                                           const QList<int>& roles) {
  QJsonObject row;
  if (!model || !index.isValid())
    return row;

  // Build path by walking up to root.
  QJsonArray pathArr;
  for (QModelIndex walk = index; walk.isValid(); walk = walk.parent()) {
    pathArr.prepend(walk.row());
  }
  row[QStringLiteral("path")] = pathArr;

  // Effective roles.
  QList<int> effective = roles;
  if (effective.isEmpty())
    effective.append(Qt::DisplayRole);

  const QHash<int, QByteArray> modelRoleNames = model->roleNames();
  const QModelIndex parent = index.parent();
  const int colCount = model->columnCount(parent);

  QJsonArray cells;
  for (int col = 0; col < colCount; ++col) {
    const QModelIndex cellIdx = model->index(index.row(), col, parent);
    QJsonObject cell;
    for (int role : effective) {
      QString roleName;
      if (modelRoleNames.contains(role)) {
        roleName = QString::fromUtf8(modelRoleNames.value(role));
      } else {
        const auto& std = standardRoleNames();
        for (auto it = std.constBegin(); it != std.constEnd(); ++it) {
          if (it.value() == role) {
            roleName = it.key();
            break;
          }
        }
        if (roleName.isEmpty())
          roleName = QStringLiteral("role_") + QString::number(role);
      }
      cell[roleName] = variantToJson(model->data(cellIdx, role));
    }
    cells.append(cell);
  }
  row[QStringLiteral("cells")] = cells;
  row[QStringLiteral("hasChildren")] = model->hasChildren(model->index(index.row(), 0, parent));
  return row;
}

QJsonObject ModelNavigator::getRoleNames(QAbstractItemModel* model) {
  QJsonObject result;
  if (!model)
    return result;

  const QHash<int, QByteArray> roles = model->roleNames();
  for (auto it = roles.constBegin(); it != roles.constEnd(); ++it) {
    result[QString::number(it.key())] = QString::fromUtf8(it.value());
  }

  return result;
}

}  // namespace qtPilot
