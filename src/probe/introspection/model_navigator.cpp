// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "introspection/model_navigator.h"

#include "core/object_registry.h"
#include "introspection/variant_json.h"

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

  for (QObject* obj : objects) {
    // Verify the object is still alive — allObjects() returns a snapshot of
    // raw pointers, and objects may have been destroyed since the snapshot
    // was taken (e.g., Qt internal models destroyed during widget teardown).
    if (!registry->contains(obj))
      continue;

    auto* model = qobject_cast<QAbstractItemModel*>(obj);
    if (!model)
      continue;

    // Skip internal Qt models (className starts with "Q" and contains "Internal")
    const QString className = QString::fromLatin1(model->metaObject()->className());
    if (className.startsWith(QLatin1Char('Q')) && className.contains(QStringLiteral("Internal")))
      continue;

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

QJsonObject ModelNavigator::getModelData(QAbstractItemModel* model, int offset, int limit,
                                         const QList<int>& roles, int parentRow, int parentCol) {
  QJsonObject result;
  if (!model) {
    result[QStringLiteral("rows")] = QJsonArray();
    result[QStringLiteral("totalRows")] = 0;
    result[QStringLiteral("totalColumns")] = 0;
    result[QStringLiteral("offset")] = 0;
    result[QStringLiteral("limit")] = 0;
    result[QStringLiteral("hasMore")] = false;
    return result;
  }

  // Build parent QModelIndex for tree navigation
  QModelIndex parentIdx;
  if (parentRow >= 0) {
    parentIdx = model->index(parentRow, parentCol, QModelIndex());
  }

  const int totalRows = model->rowCount(parentIdx);
  const int totalCols = model->columnCount(parentIdx);

  // Smart pagination
  if (limit <= 0) {
    if (totalRows <= 100) {
      offset = 0;
      limit = totalRows;
    } else {
      limit = 100;
    }
  }

  // Clamp offset
  if (offset < 0)
    offset = 0;
  if (offset > totalRows)
    offset = totalRows;

  const int endRow = qMin(offset + limit, totalRows);

  // Default roles
  QList<int> effectiveRoles = roles;
  if (effectiveRoles.isEmpty()) {
    effectiveRoles.append(Qt::DisplayRole);
  }

  // Build role name map for output keys
  const QHash<int, QByteArray> modelRoleNames = model->roleNames();

  // Fetch data
  QJsonArray rowsArray;
  for (int row = offset; row < endRow; ++row) {
    QJsonObject rowObj;
    QJsonArray cellsArray;

    for (int col = 0; col < totalCols; ++col) {
      const QModelIndex idx = model->index(row, col, parentIdx);
      QJsonObject cellObj;

      for (int role : effectiveRoles) {
        const QVariant data = model->data(idx, role);
        // Determine role name for JSON key
        QString roleName;
        if (modelRoleNames.contains(role)) {
          roleName = QString::fromUtf8(modelRoleNames.value(role));
        } else {
          // Fallback to standard role name
          const auto& stdRoles = standardRoleNames();
          for (auto it = stdRoles.constBegin(); it != stdRoles.constEnd(); ++it) {
            if (it.value() == role) {
              roleName = it.key();
              break;
            }
          }
          if (roleName.isEmpty()) {
            roleName = QStringLiteral("role_") + QString::number(role);
          }
        }
        cellObj[roleName] = variantToJson(data);
      }

      cellsArray.append(cellObj);
    }

    // Check if row has children (for tree navigation)
    const QModelIndex firstColIdx = model->index(row, 0, parentIdx);
    rowObj[QStringLiteral("cells")] = cellsArray;
    rowObj[QStringLiteral("hasChildren")] = model->hasChildren(firstColIdx);
    rowsArray.append(rowObj);
  }

  result[QStringLiteral("rows")] = rowsArray;
  result[QStringLiteral("totalRows")] = totalRows;
  result[QStringLiteral("totalColumns")] = totalCols;
  result[QStringLiteral("offset")] = offset;
  result[QStringLiteral("limit")] = limit;
  result[QStringLiteral("hasMore")] = (endRow < totalRows);

  return result;
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
