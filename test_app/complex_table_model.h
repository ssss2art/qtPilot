// Copyright (c) 2024-2026 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <QAbstractTableModel>
#include <QList>
#include <QString>

struct CatalogItem {
  int id;
  QString name;
  QString category;
  double price;
  bool inStock;
};

/// @brief Custom QAbstractTableModel exposing multiple data columns and roles.
class ComplexTableModel : public QAbstractTableModel {
  Q_OBJECT

 public:
  enum CustomRoles {
    ItemIdRole = Qt::UserRole + 1,
    InStockRole,
    FormattedPriceRole,
  };

  explicit ComplexTableModel(QObject* parent = nullptr);

  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  int columnCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
  bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
  QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
  Qt::ItemFlags flags(const QModelIndex& index) const override;

  Q_INVOKABLE void addItem(int id, const QString& name, const QString& category, double price, bool inStock);
  Q_INVOKABLE void removeItem(int row);

 private:
  QList<CatalogItem> items_;
};
