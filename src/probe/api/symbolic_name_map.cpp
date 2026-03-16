// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "api/symbolic_name_map.h"

#include "core/object_registry.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMutexLocker>

namespace qtPilot {

Q_GLOBAL_STATIC(SymbolicNameMap, s_symbolicNameMap)

SymbolicNameMap::SymbolicNameMap() {
  autoLoad();

  // Update stored paths when object IDs are refreshed.
  // SymbolicNameMap is not a QObject, so use a functor connection
  // via a QObject context that outlives us (the ObjectRegistry singleton).
  auto* registry = ObjectRegistry::instance();
  QObject::connect(registry, &ObjectRegistry::objectIdChanged, registry,
                   [this](QObject* /*obj*/, const QString& oldId, const QString& newId) {
                     QMutexLocker locker(&m_mutex);
                     for (auto it = m_nameMap.begin(); it != m_nameMap.end(); ++it) {
                       if (it.value() == oldId) {
                         it.value() = newId;
                       }
                     }
                   });
}

SymbolicNameMap* SymbolicNameMap::instance() {
  return s_symbolicNameMap();
}

QString SymbolicNameMap::resolve(const QString& symbolicName) const {
  QMutexLocker locker(&m_mutex);
  return m_nameMap.value(symbolicName);
}

void SymbolicNameMap::registerName(const QString& name, const QString& path) {
  QMutexLocker locker(&m_mutex);
  m_nameMap[name] = path;
}

void SymbolicNameMap::unregisterName(const QString& name) {
  QMutexLocker locker(&m_mutex);
  m_nameMap.remove(name);
}

QJsonObject SymbolicNameMap::allNames() const {
  QMutexLocker locker(&m_mutex);
  QJsonObject result;
  for (auto it = m_nameMap.constBegin(); it != m_nameMap.constEnd(); ++it) {
    result[it.key()] = it.value();
  }
  return result;
}

bool SymbolicNameMap::loadFromFile(const QString& filePath) {
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    qWarning() << "SymbolicNameMap: Failed to open" << filePath;
    return false;
  }

  QJsonParseError parseError;
  QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
  file.close();

  if (parseError.error != QJsonParseError::NoError) {
    qWarning() << "SymbolicNameMap: Parse error in" << filePath << ":" << parseError.errorString();
    return false;
  }

  if (!doc.isObject()) {
    qWarning() << "SymbolicNameMap: Expected JSON object in" << filePath;
    return false;
  }

  QMutexLocker locker(&m_mutex);
  m_nameMap.clear();

  QJsonObject obj = doc.object();
  for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
    if (it.value().isString()) {
      m_nameMap[it.key()] = it.value().toString();
    }
  }

  qDebug() << "SymbolicNameMap: Loaded" << m_nameMap.size() << "names from" << filePath;
  return true;
}

bool SymbolicNameMap::saveToFile(const QString& filePath) const {
  QMutexLocker locker(&m_mutex);

  QJsonObject obj;
  for (auto it = m_nameMap.constBegin(); it != m_nameMap.constEnd(); ++it) {
    obj[it.key()] = it.value();
  }

  QFile file(filePath);
  if (!file.open(QIODevice::WriteOnly)) {
    qWarning() << "SymbolicNameMap: Failed to write" << filePath;
    return false;
  }

  file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
  file.close();
  return true;
}

bool SymbolicNameMap::validateAll() const {
  QMutexLocker locker(&m_mutex);
  auto* registry = ObjectRegistry::instance();

  for (auto it = m_nameMap.constBegin(); it != m_nameMap.constEnd(); ++it) {
    if (!registry->findById(it.value())) {
      return false;
    }
  }
  return true;
}

QJsonArray SymbolicNameMap::validateNames() const {
  QMutexLocker locker(&m_mutex);
  auto* registry = ObjectRegistry::instance();
  QJsonArray results;

  for (auto it = m_nameMap.constBegin(); it != m_nameMap.constEnd(); ++it) {
    QJsonObject entry;
    entry[QStringLiteral("name")] = it.key();
    entry[QStringLiteral("path")] = it.value();
    entry[QStringLiteral("valid")] = (registry->findById(it.value()) != nullptr);
    results.append(entry);
  }
  return results;
}

void SymbolicNameMap::autoLoad() {
  // Check QTPILOT_NAME_MAP env var first
  QByteArray envPath = qgetenv("QTPILOT_NAME_MAP");
  if (!envPath.isEmpty()) {
    QString path = QString::fromUtf8(envPath);
    if (QFileInfo::exists(path)) {
      loadFromFile(path);
      return;
    }
  }

  // Fall back to qtPilot-names.json in CWD
  QString defaultPath = QStringLiteral("qtPilot-names.json");
  if (QFileInfo::exists(defaultPath)) {
    loadFromFile(defaultPath);
  }
  // No file found - start empty (no error)
}

}  // namespace qtPilot
