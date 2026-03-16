// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <QObject>
#include <QString>

namespace qtPilot {

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
