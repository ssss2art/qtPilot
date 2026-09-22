// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <QObject>
#include <QString>

namespace qtPilot {

/// @brief Strip the registration index the QML engine appends to a declared type.
///
/// A type declared in QML reaches the metaobject system as the declared name plus
/// a generated suffix: "Foo_QMLTYPE_58_QML_98", "Foo_QMLTYPE_58" or "Foo_QML_16".
/// Those numbers are registration indices -- they differ between two runs of the
/// same unchanged binary -- so they cannot be part of any name a caller writes
/// down. Everything else, including a compiled C++ name, is returned unchanged.
///
/// Declared outside the QTPILOT_HAS_QML guard on purpose: this is string work with
/// no Qt Quick dependency, and the registry needs it without taking one.
///
/// @param className The class name to strip.
/// @return The name the type was declared with.
inline QString declaredTypeName(const QString& className) {
  QString name = className;
  // Innermost last: "Foo_QMLTYPE_58_QML_98" sheds "_QML_98", then "_QMLTYPE_58".
  const auto shed = [&name](QLatin1String marker) {
    const int at = name.lastIndexOf(marker);
    if (at < 0 || at + marker.size() >= name.size()) {
      return;
    }
    for (int i = at + marker.size(); i < name.size(); ++i) {
      if (!name.at(i).isDigit()) {
        return;
      }
    }
    name.truncate(at);
  };
  shed(QLatin1String("_QML_"));
  shed(QLatin1String("_QMLTYPE_"));
  return name;
}

/// @brief Metadata extracted from a QML item.
struct QmlItemInfo {
  bool isQmlItem = false;
  QString qmlId;
  QString qmlFile;
  QString shortTypeName;
};

#ifdef QTPILOT_HAS_QML

/// @brief Extract QML metadata from a QObject.
///
/// If the object is a QQuickItem, extracts QML id (via QQmlContext::nameForObject),
/// source file URL, and short type name (className with "QQuick" prefix stripped).
///
/// @param obj The object to inspect.
/// @return QmlItemInfo with isQmlItem=true and metadata if QQuickItem, defaults otherwise.
QmlItemInfo inspectQmlItem(QObject* obj);

/// @brief Just the parts of QmlItemInfo an ID segment needs.
///
/// inspectQmlItem() additionally resolves the context's base URL and converts it
/// to a string. That is real work (QUrl::toString allocates) and an ID segment
/// never looks at it -- which matters because sibling disambiguation calls this
/// once per sibling, so a large Repeater multiplies the cost by its row count.
///
/// isQmlItem and shortTypeName are populated as in inspectQmlItem(); qmlFile is
/// always empty.
QmlItemInfo inspectQmlItemForSegment(QObject* obj);

/// @brief Strip the "QQuick" prefix from a Qt Quick class name.
///
/// For example, "QQuickRectangle" becomes "Rectangle".
/// If the class name does not start with "QQuick", it is returned unchanged.
///
/// @param className The class name to strip.
/// @return The stripped class name.
QString stripQmlPrefix(const QString& className);

/// @brief Quick check whether an object is a QQuickItem.
///
/// Uses qobject_cast<QQuickItem*> to test.
///
/// @param obj The object to check.
/// @return true if the object is a QQuickItem subclass.
bool isQmlItem(QObject* obj);

#else

/// @brief Stub: always returns default QmlItemInfo (isQmlItem=false).
inline QmlItemInfo inspectQmlItem(QObject* /*obj*/) {
  return QmlItemInfo{};
}

/// @brief Stub: always returns default QmlItemInfo (isQmlItem=false).
inline QmlItemInfo inspectQmlItemForSegment(QObject* /*obj*/) {
  return QmlItemInfo{};
}

/// @brief Stub: returns className unchanged.
inline QString stripQmlPrefix(const QString& className) {
  return className;
}

/// @brief Stub: always returns false.
inline bool isQmlItem(QObject* /*obj*/) {
  return false;
}

#endif  // QTPILOT_HAS_QML

}  // namespace qtPilot
