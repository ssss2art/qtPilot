// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "models.h"

// ---------------------------------------------------------------------------
// TaskListModel
// ---------------------------------------------------------------------------

TaskListModel::TaskListModel(QObject* parent) : QAbstractListModel(parent) {
  m_tasks = {
      {QStringLiteral("Write the spec"), QStringLiteral("ada"), 1, true},
      {QStringLiteral("Review the probe"), QStringLiteral("grace"), 2, false},
      {QStringLiteral("Ship the release"), QStringLiteral("ada"), 3, false},
      {QStringLiteral("Fix the flaky test"), QStringLiteral("linus"), 2, true},
      {QStringLiteral("Update the changelog"), QStringLiteral("grace"), 1, false},
  };
}

int TaskListModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : m_tasks.size();
}

QVariant TaskListModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= m_tasks.size()) {
    return {};
  }
  const Task& task = m_tasks.at(index.row());
  switch (role) {
    case TitleRole:
    case Qt::DisplayRole:  // so a plain DisplayRole read still returns something
      return task.title;
    case OwnerRole:
      return task.owner;
    case PriorityRole:
      return task.priority;
    case DoneRole:
      return task.done;
    default:
      return {};
  }
}

QHash<int, QByteArray> TaskListModel::roleNames() const {
  return {
      {TitleRole, "title"},
      {OwnerRole, "owner"},
      {PriorityRole, "priority"},
      {DoneRole, "done"},
  };
}

// ---------------------------------------------------------------------------
// FileTreeModel
// ---------------------------------------------------------------------------

FileTreeModel::FileTreeModel(QObject* parent) : QAbstractItemModel(parent) {
  m_root = new Node{QStringLiteral("<root>"), 0, nullptr, {}};

  auto addChild = [](Node* parentNode, const QString& name, int size) {
    auto* node = new Node{name, size, parentNode, {}};
    parentNode->children.append(node);
    return node;
  };

  Node* src = addChild(m_root, QStringLiteral("src"), 0);
  addChild(src, QStringLiteral("main.cpp"), 1200);
  addChild(src, QStringLiteral("probe.cpp"), 4800);

  Node* docs = addChild(m_root, QStringLiteral("docs"), 0);
  addChild(docs, QStringLiteral("README.md"), 900);

  addChild(m_root, QStringLiteral("LICENSE"), 1100);
}

FileTreeModel::~FileTreeModel() {
  delete m_root;
}

FileTreeModel::Node* FileTreeModel::nodeFor(const QModelIndex& index) const {
  return index.isValid() ? static_cast<Node*>(index.internalPointer()) : m_root;
}

QModelIndex FileTreeModel::index(int row, int column, const QModelIndex& parent) const {
  if (!hasIndex(row, column, parent)) {
    return {};
  }
  Node* parentNode = nodeFor(parent);
  return createIndex(row, column, parentNode->children.at(row));
}

QModelIndex FileTreeModel::parent(const QModelIndex& child) const {
  if (!child.isValid()) {
    return {};
  }
  Node* parentNode = static_cast<Node*>(child.internalPointer())->parent;
  if (!parentNode || parentNode == m_root) {
    return {};
  }
  Node* grandParent = parentNode->parent;
  const int row = grandParent ? grandParent->children.indexOf(parentNode) : 0;
  return createIndex(row, 0, parentNode);
}

int FileTreeModel::rowCount(const QModelIndex& parent) const {
  if (parent.column() > 0) {
    return 0;
  }
  return nodeFor(parent)->children.size();
}

int FileTreeModel::columnCount(const QModelIndex& /*parent*/) const {
  return 1;
}

QVariant FileTreeModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid()) {
    return {};
  }
  Node* node = static_cast<Node*>(index.internalPointer());
  switch (role) {
    case NameRole:
    case Qt::DisplayRole:
      return node->name;
    case SizeRole:
      return node->size;
    default:
      return {};
  }
}

QHash<int, QByteArray> FileTreeModel::roleNames() const {
  return {
      {NameRole, "name"},
      {SizeRole, "size"},
  };
}

// ---------------------------------------------------------------------------
// LazyLogModel
// ---------------------------------------------------------------------------

LazyLogModel::LazyLogModel(QObject* parent) : QAbstractListModel(parent) {}

int LazyLogModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : m_loaded;
}

QVariant LazyLogModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= m_loaded) {
    return {};
  }
  const int row = index.row();
  switch (role) {
    case MessageRole:
    case Qt::DisplayRole:
      if (row == kSentinelRow) {
        return QStringLiteral("sentinel-needle");
      }
      return QStringLiteral("log entry %1").arg(row);
    case LevelRole:
      return (row % 3 == 0) ? QStringLiteral("error") : QStringLiteral("info");
    default:
      return {};
  }
}

QHash<int, QByteArray> LazyLogModel::roleNames() const {
  return {
      {MessageRole, "message"},
      {LevelRole, "level"},
  };
}

bool LazyLogModel::canFetchMore(const QModelIndex& parent) const {
  return !parent.isValid() && m_loaded < kTotalRows;
}

void LazyLogModel::fetchMore(const QModelIndex& parent) {
  if (parent.isValid()) {
    return;
  }
  const int remaining = kTotalRows - m_loaded;
  const int count = qMin(kBatchSize, remaining);
  if (count <= 0) {
    return;
  }
  beginInsertRows(QModelIndex(), m_loaded, m_loaded + count - 1);
  m_loaded += count;
  endInsertRows();
}
