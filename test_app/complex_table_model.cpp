// Copyright (c) 2024-2026 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "complex_table_model.h"

ComplexTableModel::ComplexTableModel(QObject* parent) : QAbstractTableModel(parent) {
  // Pre-populate with diverse generic catalog items
  items_ = {
      {101, QStringLiteral("Sensor Alpha"), QStringLiteral("Sensors"), 49.99, true},
      {102, QStringLiteral("Controller Beta"), QStringLiteral("Control"), 299.50, true},
      {103, QStringLiteral("Actuator Gamma"), QStringLiteral("Mechanical"), 149.00, false},
      {104, QStringLiteral("Display Delta"), QStringLiteral("UI"), 89.95, true},
      {105, QStringLiteral("Power Unit Epsilon"), QStringLiteral("Power"), 199.99, true},
  };
}

int ComplexTableModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid())
    return 0;
  return static_cast<int>(items_.size());
}

int ComplexTableModel::columnCount(const QModelIndex& parent) const {
  if (parent.isValid())
    return 0;
  return 5;  // ID, Name, Category, Price, InStock
}

QVariant ComplexTableModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(items_.size())) {
    return QVariant();
  }

  const CatalogItem& item = items_[index.row()];

  if (role == Qt::DisplayRole || role == Qt::EditRole) {
    switch (index.column()) {
      case 0:
        return item.id;
      case 1:
        return item.name;
      case 2:
        return item.category;
      case 3:
        return item.price;
      case 4:
        return item.inStock ? QStringLiteral("Yes") : QStringLiteral("No");
      default:
        break;
    }
  } else if (role == Qt::CheckStateRole && index.column() == 4) {
    return item.inStock ? Qt::Checked : Qt::Unchecked;
  } else if (role == ItemIdRole) {
    return item.id;
  } else if (role == InStockRole) {
    return item.inStock;
  } else if (role == FormattedPriceRole) {
    return QStringLiteral("$%1").arg(item.price, 0, 'f', 2);
  }

  return QVariant();
}

bool ComplexTableModel::setData(const QModelIndex& index, const QVariant& value, int role) {
  if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(items_.size())) {
    return false;
  }

  CatalogItem& item = items_[index.row()];
  bool modified = false;

  if (role == Qt::EditRole) {
    switch (index.column()) {
      case 1:
        item.name = value.toString();
        modified = true;
        break;
      case 2:
        item.category = value.toString();
        modified = true;
        break;
      case 3:
        item.price = value.toDouble();
        modified = true;
        break;
      default:
        break;
    }
  } else if (role == Qt::CheckStateRole && index.column() == 4) {
    item.inStock = (value.toInt() == Qt::Checked);
    modified = true;
  }

  if (modified) {
    emit dataChanged(index, index, {role, Qt::DisplayRole});
    return true;
  }
  return false;
}

QVariant ComplexTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
  if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
    switch (section) {
      case 0:
        return QStringLiteral("ID");
      case 1:
        return QStringLiteral("Name");
      case 2:
        return QStringLiteral("Category");
      case 3:
        return QStringLiteral("Price ($)");
      case 4:
        return QStringLiteral("In Stock");
      default:
        break;
    }
  }
  return QAbstractTableModel::headerData(section, orientation, role);
}

Qt::ItemFlags ComplexTableModel::flags(const QModelIndex& index) const {
  if (!index.isValid())
    return Qt::NoItemFlags;
  Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
  if (index.column() == 1 || index.column() == 2 || index.column() == 3) {
    f |= Qt::ItemIsEditable;
  }
  if (index.column() == 4) {
    f |= Qt::ItemIsUserCheckable;
  }
  return f;
}

void ComplexTableModel::addItem(int id, const QString& name, const QString& category, double price,
                                bool inStock) {
  int row = static_cast<int>(items_.size());
  beginInsertRows(QModelIndex(), row, row);
  items_.append({id, name, category, price, inStock});
  endInsertRows();
}

void ComplexTableModel::removeItem(int row) {
  if (row < 0 || row >= static_cast<int>(items_.size()))
    return;
  beginRemoveRows(QModelIndex(), row, row);
  items_.removeAt(row);
  endRemoveRows();
}
