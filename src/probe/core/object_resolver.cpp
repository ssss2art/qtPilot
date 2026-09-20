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
QMutex ObjectResolver::s_mutex;

namespace {

std::expected<QObject*, ObjectResolver::ResolveError> resolveNumeric(const QString& id) {
  bool isNumeric = false;
  int numericId = -1;

  if (id.startsWith(QLatin1Char('#'))) {
    numericId = id.mid(1).toInt(&isNumeric);
  } else {
    numericId = id.toInt(&isNumeric);
  }

  if (!isNumeric || numericId <= 0) {
    return std::unexpected(ObjectResolver::ResolveError{
        ObjectResolver::ResolveErrorKind::NotFound,
        id,
        QStringLiteral("Not a valid numeric ID: %1").arg(id),
    });
  }

  return ObjectResolver::findByNumericIdExpected(numericId).transform_error(
      [&](const QString& err) {
        return ObjectResolver::ResolveError{
            ObjectResolver::ResolveErrorKind::NotFound,
            id,
            err,
        };
      });
}

std::expected<QObject*, ObjectResolver::ResolveError> resolveSymbolic(const QString& id) {
  return SymbolicNameMap::instance()
      ->resolveExpected(id)
      .transform_error([&id](const QString& err) {
        return ObjectResolver::ResolveError{
            ObjectResolver::ResolveErrorKind::NotFound,
            id,
            err,
        };
      })
      .and_then([&id](const QString& resolvedPath)
                    -> std::expected<QObject*, ObjectResolver::ResolveError> {
        return ObjectRegistry::instance()
            ->findByIdExpected(resolvedPath)
            .transform_error([&id, &resolvedPath](const QString&) {
              return ObjectResolver::ResolveError{
                  ObjectResolver::ResolveErrorKind::NotFound,
                  id,
                  QStringLiteral("Symbolic name '%1' mapped to '%2' but object was not found")
                      .arg(id, resolvedPath),
              };
            });
      });
}

std::expected<QObject*, ObjectResolver::ResolveError> resolveHierarchical(const QString& id) {
  auto res = ObjectRegistry::instance()->findByIdExpected(id);
  if (res.has_value()) {
    return *res;
  }
  return std::unexpected(ObjectResolver::ResolveError{
      ObjectResolver::ResolveErrorKind::NotFound,
      id,
      res.error(),
  });
}

}  // namespace

std::expected<QObject*, ObjectResolver::ResolveError> ObjectResolver::resolveExpected(
    const QString& id) {
  if (id.isEmpty()) {
    return std::unexpected(ResolveError{
        ResolveErrorKind::EmptyId,
        id,
        QStringLiteral("Object identifier is empty"),
    });
  }

  return resolveNumeric(id)
      .or_else([&id](const auto&) { return resolveSymbolic(id); })
      .or_else([&id](const auto&) { return resolveHierarchical(id); });
}

QObject* ObjectResolver::resolve(const QString& id) {
  auto res = resolveExpected(id);
  return res.has_value() ? *res : nullptr;
}

int ObjectResolver::assignNumericId(QObject* obj) {
  if (!obj) {
    return -1;
  }

  QMutexLocker locker(&s_mutex);

  // Check if already assigned and still valid
  auto it = s_objectToNumericId.constFind(obj);
  if (it != s_objectToNumericId.constEnd()) {
    int existingId = it.value();
    auto numIt = s_numericIds.constFind(existingId);
    if (numIt != s_numericIds.constEnd() && numIt.value().data() == obj) {
      return existingId;
    }
    s_objectToNumericId.erase(it);
  }

  int id = s_nextId.fetch_add(1, std::memory_order_relaxed);
  s_numericIds[id] = QPointer<QObject>(obj);
  s_objectToNumericId[obj] = id;

  // Clean up automatically when the object is destroyed
  QObject::connect(obj, &QObject::destroyed, [obj, id]() {
    QMutexLocker locker(&s_mutex);
    s_objectToNumericId.remove(obj);
    s_numericIds.remove(id);
  });

  return id;
}

QObject* ObjectResolver::findByNumericId(int numericId) {
  auto res = findByNumericIdExpected(numericId);
  return res.has_value() ? *res : nullptr;
}

std::expected<QObject*, QString> ObjectResolver::findByNumericIdExpected(int numericId) {
  if (numericId <= 0) {
    return std::unexpected(QStringLiteral("Numeric ID must be positive: %1").arg(numericId));
  }
  QMutexLocker locker(&s_mutex);
  auto it = s_numericIds.constFind(numericId);
  if (it != s_numericIds.constEnd()) {
    if (QObject* obj = it.value().data()) {
      return obj;
    }
  }
  return std::unexpected(
      QStringLiteral("Numeric object ID #%1 not found or object deleted").arg(numericId));
}

void ObjectResolver::clearNumericIds() {
  QMutexLocker locker(&s_mutex);
  s_numericIds.clear();
  s_objectToNumericId.clear();
  s_nextId.store(1, std::memory_order_relaxed);
}

int ObjectResolver::numericIdFor(QObject* obj) {
  if (!obj) {
    return -1;
  }
  QMutexLocker locker(&s_mutex);
  return s_objectToNumericId.value(obj, -1);
}

}  // namespace qtPilot
