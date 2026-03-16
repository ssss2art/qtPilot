// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "core/object_resolver.h"

#include "api/symbolic_name_map.h"
#include "core/object_registry.h"

namespace qtPilot {

// Static member initialization
QHash<int, QPointer<QObject>> ObjectResolver::s_numericIds;
QHash<QObject*, int> ObjectResolver::s_objectToNumericId;
std::atomic<int> ObjectResolver::s_nextId{1};

QObject* ObjectResolver::resolve(const QString& id) {
  if (id.isEmpty()) {
    return nullptr;
  }

  // 1. Numeric ID: starts with '#' or is all digits
  bool isNumeric = false;
  int numericId = -1;

  if (id.startsWith(QLatin1Char('#'))) {
    numericId = id.mid(1).toInt(&isNumeric);
  } else {
    numericId = id.toInt(&isNumeric);
  }

  if (isNumeric && numericId > 0) {
    QObject* obj = findByNumericId(numericId);
    if (obj) {
      return obj;
    }
  }

  // 2. Symbolic name lookup
  QString resolvedPath = SymbolicNameMap::instance()->resolve(id);
  if (!resolvedPath.isEmpty()) {
    QObject* obj = ObjectRegistry::instance()->findById(resolvedPath);
    if (obj) {
      return obj;
    }
  }

  // 3. Hierarchical path (direct registry lookup)
  return ObjectRegistry::instance()->findById(id);
}

int ObjectResolver::assignNumericId(QObject* obj) {
  if (!obj) {
    return -1;
  }

  // Check if already assigned
  auto it = s_objectToNumericId.constFind(obj);
  if (it != s_objectToNumericId.constEnd()) {
    return it.value();
  }

  int id = s_nextId.fetch_add(1, std::memory_order_relaxed);
  s_numericIds[id] = QPointer<QObject>(obj);
  s_objectToNumericId[obj] = id;
  return id;
}

QObject* ObjectResolver::findByNumericId(int numericId) {
  auto it = s_numericIds.constFind(numericId);
  if (it != s_numericIds.constEnd()) {
    return it.value().data();  // Returns nullptr if object was deleted
  }
  return nullptr;
}

void ObjectResolver::clearNumericIds() {
  s_numericIds.clear();
  s_objectToNumericId.clear();
  s_nextId.store(1, std::memory_order_relaxed);
}

int ObjectResolver::numericIdFor(QObject* obj) {
  if (!obj) {
    return -1;
  }
  return s_objectToNumericId.value(obj, -1);
}

}  // namespace qtPilot
