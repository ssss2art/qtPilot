// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT
//
// Models for the QML test app. Each one targets a distinct capability of the
// qt.models.* probe API, so the app can prove that surface end to end:
//
//   TaskListModel  -- custom roleNames (the QML idiom native mode must resolve)
//   FileTreeModel  -- hierarchy, for parentPath navigation and recursive search
//   LazyLogModel   -- canFetchMore/fetchMore, for the lazy-aware code paths
//
// Deliberately tiny and fully deterministic: the row contents are asserted
// against by tests, so they must not vary between runs.

#pragma once

#include <QAbstractItemModel>
#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QVector>

/// @brief Flat list with custom roles — the common Qt Quick model shape.
class TaskListModel : public QAbstractListModel {
  Q_OBJECT

 public:
  enum Role {
    TitleRole = Qt::UserRole + 1,
    OwnerRole,
    PriorityRole,
    DoneRole,
  };
  Q_ENUM(Role)

  explicit TaskListModel(QObject* parent = nullptr);

  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

 private:
  struct Task {
    QString title;
    QString owner;
    int priority;
    bool done;
  };
  QVector<Task> m_tasks;
};

/// @brief Two-level tree — exercises parentPath and recursive search.
class FileTreeModel : public QAbstractItemModel {
  Q_OBJECT

 public:
  enum Role {
    NameRole = Qt::UserRole + 1,
    SizeRole,
  };
  Q_ENUM(Role)

  explicit FileTreeModel(QObject* parent = nullptr);
  ~FileTreeModel() override;

  QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
  QModelIndex parent(const QModelIndex& child) const override;
  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  int columnCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

 private:
  struct Node {
    QString name;
    int size = 0;
    Node* parent = nullptr;
    QList<Node*> children;
    ~Node() { qDeleteAll(children); }
  };
  Node* nodeFor(const QModelIndex& index) const;

  Node* m_root;
};

/// @brief Incrementally-populated list — exercises canFetchMore/fetchMore.
///
/// Starts with kInitialRows of kTotalRows visible. The probe must fetch the
/// remainder before reporting or searching beyond the initial window.
class LazyLogModel : public QAbstractListModel {
  Q_OBJECT

 public:
  static constexpr int kInitialRows = 5;
  static constexpr int kBatchSize = 25;
  // Larger than any view will render, so the model stays *partially* loaded at
  // rest. That is the whole point: a probe read or search must drive fetchMore
  // itself rather than riding on whatever the view happened to realise.
  static constexpr int kTotalRows = 500;
  // Sentinel far past the initial window; only a lazy-aware search finds it.
  static constexpr int kSentinelRow = 400;

  enum Role {
    MessageRole = Qt::UserRole + 1,
    LevelRole,
  };
  Q_ENUM(Role)

  explicit LazyLogModel(QObject* parent = nullptr);

  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;
  bool canFetchMore(const QModelIndex& parent) const override;
  void fetchMore(const QModelIndex& parent) override;

 private:
  int m_loaded = kInitialRows;
};
