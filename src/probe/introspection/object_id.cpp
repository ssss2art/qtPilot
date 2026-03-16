// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "introspection/object_id.h"

#include "core/object_registry.h"
#include "introspection/qml_inspector.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QMetaProperty>
#include <QWidget>

namespace qtPilot {

namespace {

/// @brief Sanitize a string for use in an ID segment.
/// Takes first 20 characters, replaces non-alphanumeric with underscores.
QString sanitizeForId(const QString& input) {
  QString result;
  result.reserve(qMin(input.length(), 20));

  for (int i = 0; i < qMin(input.length(), 20); ++i) {
    QChar ch = input.at(i);
    if (ch.isLetterOrNumber()) {
      result.append(ch);
    } else {
      result.append(QLatin1Char('_'));
    }
  }

  // Trim trailing underscores
  while (result.endsWith(QLatin1Char('_')) && result.length() > 1) {
    result.chop(1);
  }

  return result;
}

/// @brief Get the text property value if it exists.
/// Returns empty string if no text property or value is empty.
QString getTextProperty(QObject* obj) {
  if (!obj) {
    return QString();
  }

  // Check for "text" property via meta-object system
  const QMetaObject* meta = obj->metaObject();
  int textIndex = meta->indexOfProperty("text");
  if (textIndex >= 0) {
    QMetaProperty textProp = meta->property(textIndex);
    if (textProp.isReadable()) {
      QVariant value = textProp.read(obj);
      if (value.canConvert<QString>()) {
        return value.toString();
      }
    }
  }

  return QString();
}

/// @brief Count siblings of the same class that come before this object.
/// Returns -1 if this object is uniquely identifiable (only one of its class).
int getSiblingIndex(QObject* obj) {
  if (!obj) {
    return -1;
  }

  QObject* parent = obj->parent();
  if (!parent) {
    // For top-level objects, we can't easily determine siblings
    // without more context. Return -1 to indicate no disambiguation needed.
    return -1;
  }

  const char* targetClass = obj->metaObject()->className();
  QList<QObject*> children = parent->children();

  // Count siblings of the same exact class
  int sameClassCount = 0;
  int indexAmongSameClass = -1;

  for (int i = 0; i < children.size(); ++i) {
    QObject* child = children.at(i);
    const char* childClass = child->metaObject()->className();

    // Only count exact class matches (not subclasses)
    if (qstrcmp(childClass, targetClass) == 0) {
      if (child == obj) {
        indexAmongSameClass = sameClassCount;
      }
      sameClassCount++;
    }
  }

  // If there's only one object of this class, no disambiguation needed
  if (sameClassCount <= 1) {
    return -1;
  }

  // Return 1-based index for human readability
  return indexAmongSameClass + 1;
}

/// @brief Get all top-level objects (those without parents).
/// Uses QCoreApplication's children and other known roots.
QList<QObject*> getTopLevelObjects() {
  QList<QObject*> result;

  QCoreApplication* app = QCoreApplication::instance();
  if (app) {
    // Include the application object itself as a search root.
    // generateObjectId() walks up to QCoreApplication, so IDs start
    // with the app's segment (e.g., "QApplication/..."). The search
    // must begin from the app to match that first segment.
    result.append(app);
  }

  return result;
}

/// @brief Match a single ID segment against an object.
bool matchesSegment(QObject* obj, const QString& segment) {
  if (!obj) {
    return false;
  }

  // First, check if segment matches objectName
  if (!obj->objectName().isEmpty() && obj->objectName() == segment) {
    return true;
  }

  // Check if segment matches text_* pattern
  if (segment.startsWith(QLatin1String("text_"))) {
    QString text = getTextProperty(obj);
    if (!text.isEmpty()) {
      QString expectedSegment = QStringLiteral("text_") + sanitizeForId(text);
      if (segment == expectedSegment) {
        return true;
      }
    }
  }

  // Check if segment matches ClassName or ClassName#N pattern
  QString className = QString::fromLatin1(obj->metaObject()->className());

  // Direct class name match (when unique)
  if (segment == className) {
    return true;
  }

  // ClassName#N pattern
  if (segment.startsWith(className + QLatin1Char('#'))) {
    QString indexStr = segment.mid(className.length() + 1);
    bool ok = false;
    int index = indexStr.toInt(&ok);
    if (ok && index > 0) {
      int actualIndex = getSiblingIndex(obj);
      if (actualIndex == index) {
        return true;
      }
    }
  }

  return false;
}

/// @brief Find object by path segments starting from a list of candidates.
QObject* findBySegments(const QStringList& segments, int segmentIndex,
                        const QList<QObject*>& candidates) {
  if (segmentIndex >= segments.size()) {
    return nullptr;
  }

  const QString& segment = segments.at(segmentIndex);
  bool isLastSegment = (segmentIndex == segments.size() - 1);

  for (QObject* obj : candidates) {
    if (matchesSegment(obj, segment)) {
      if (isLastSegment) {
        return obj;
      }
      // Continue searching in children
      QObject* found = findBySegments(segments, segmentIndex + 1, obj->children());
      if (found) {
        return found;
      }
    }
  }

  return nullptr;
}

/// @brief Serialize object tree recursively.
QJsonObject serializeTreeRecursive(QObject* obj, int maxDepth, int currentDepth) {
  QJsonObject result = serializeObjectInfo(obj);

  // Check depth limit
  if (maxDepth >= 0 && currentDepth >= maxDepth) {
    return result;
  }

  // Add children
  QList<QObject*> children = obj->children();
  if (!children.isEmpty()) {
    QJsonArray childArray;
    for (QObject* child : children) {
      childArray.append(serializeTreeRecursive(child, maxDepth, currentDepth + 1));
    }
    result[QLatin1String("children")] = childArray;
  }

  return result;
}

}  // namespace

QString generateIdSegment(QObject* obj) {
  if (!obj) {
    return QString();
  }

#ifdef QTPILOT_HAS_QML
  // Priority 0 (QML only): QML id takes highest priority
  QmlItemInfo qmlInfo = inspectQmlItem(obj);
  if (qmlInfo.isQmlItem && !qmlInfo.qmlId.isEmpty()) {
    return qmlInfo.qmlId;
  }
#endif

  // Priority 1: objectName (if set and non-empty)
  QString name = obj->objectName();
  if (!name.isEmpty()) {
    return name;
  }

  // Priority 2: text property (if exists and non-empty)
  QString text = getTextProperty(obj);
  if (!text.isEmpty()) {
    return QStringLiteral("text_") + sanitizeForId(text);
  }

  // Priority 3: ClassName (or short QML type name) with optional disambiguation suffix
#ifdef QTPILOT_HAS_QML
  // For QML items without a QML id or objectName, use short type name
  // (e.g., "Rectangle" instead of "QQuickRectangle")
  QString typeName = qmlInfo.isQmlItem ? qmlInfo.shortTypeName
                                       : QString::fromLatin1(obj->metaObject()->className());
#else
  QString typeName = QString::fromLatin1(obj->metaObject()->className());
#endif

  int siblingIndex = getSiblingIndex(obj);

  if (siblingIndex > 0) {
    return typeName + QLatin1Char('#') + QString::number(siblingIndex);
  }

  return typeName;
}

QString generateObjectId(QObject* obj) {
  if (!obj) {
    return QString();
  }

  // Build path from root to object
  QStringList segments;
  QObject* current = obj;

  while (current) {
    segments.prepend(generateIdSegment(current));
    current = current->parent();
  }

  return segments.join(QLatin1Char('/'));
}

QObject* findByObjectId(const QString& id, QObject* root) {
  if (id.isEmpty()) {
    return nullptr;
  }

  QStringList segments = id.split(QLatin1Char('/'), Qt::SkipEmptyParts);
  if (segments.isEmpty()) {
    return nullptr;
  }

  QList<QObject*> searchRoots;
  if (root) {
    searchRoots.append(root);
  } else {
    searchRoots = getTopLevelObjects();
  }

  return findBySegments(segments, 0, searchRoots);
}

QJsonObject serializeObjectInfo(QObject* obj) {
  QJsonObject result;

  if (!obj) {
    return result;
  }

  result[QLatin1String("id")] = ObjectRegistry::instance()->objectId(obj);
  result[QLatin1String("className")] = QString::fromLatin1(obj->metaObject()->className());

  QString objectName = obj->objectName();
  if (!objectName.isEmpty()) {
    result[QLatin1String("objectName")] = objectName;
  }

  // Widget-specific properties
  QWidget* widget = qobject_cast<QWidget*>(obj);
  if (widget) {
    result[QLatin1String("visible")] = widget->isVisible();

    QJsonObject geometry;
    QRect geom = widget->geometry();
    geometry[QLatin1String("x")] = geom.x();
    geometry[QLatin1String("y")] = geom.y();
    geometry[QLatin1String("width")] = geom.width();
    geometry[QLatin1String("height")] = geom.height();
    result[QLatin1String("geometry")] = geometry;
  }

  // Include text property if present
  QString text = getTextProperty(obj);
  if (!text.isEmpty()) {
    result[QLatin1String("text")] = text;
  }

#ifdef QTPILOT_HAS_QML
  // QML-specific metadata
  QmlItemInfo qmlInfo = inspectQmlItem(obj);
  if (qmlInfo.isQmlItem) {
    result[QLatin1String("isQmlItem")] = true;
    if (!qmlInfo.qmlId.isEmpty()) {
      result[QLatin1String("qmlId")] = qmlInfo.qmlId;
    }
    if (!qmlInfo.qmlFile.isEmpty()) {
      result[QLatin1String("qmlFile")] = qmlInfo.qmlFile;
    }
    result[QLatin1String("qmlTypeName")] = qmlInfo.shortTypeName;
  }
#endif

  return result;
}

QJsonObject serializeObjectTree(QObject* root, int maxDepth) {
  if (!root) {
    // Serialize all top-level objects
    QJsonObject result;
    result[QLatin1String("id")] = QString();
    result[QLatin1String("className")] = QStringLiteral("Root");

    QList<QObject*> topLevel = getTopLevelObjects();
    if (!topLevel.isEmpty()) {
      QJsonArray childArray;
      for (QObject* obj : topLevel) {
        childArray.append(serializeTreeRecursive(obj, maxDepth, 0));
      }
      result[QLatin1String("children")] = childArray;
    }

    return result;
  }

  return serializeTreeRecursive(root, maxDepth, 0);
}

}  // namespace qtPilot
