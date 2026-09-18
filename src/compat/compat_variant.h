// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <QVariant>
#include <QtGlobal>

namespace qtPilot {
namespace compat {

/// Returns the type ID of a QVariant.
/// Uses userType() which works on both Qt5 and Qt6.
inline int variantTypeId(const QVariant& v) {
  return v.userType();
}

/// Checks if a QVariant can be converted to the given type ID.
/// Qt6: v.canConvert(QMetaType(typeId))
/// Qt5: v.canConvert(typeId)
inline bool variantCanConvert(const QVariant& v, int typeId) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  return v.canConvert(QMetaType(typeId));
#else
  return v.canConvert(typeId);
#endif
}

/// Converts a QVariant in-place to the given type ID.
/// Qt6: v.convert(QMetaType(typeId))
/// Qt5: v.convert(typeId)
inline bool variantConvert(QVariant& v, int typeId) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  return v.convert(QMetaType(typeId));
#else
  return v.convert(typeId);
#endif
}

/// Creates an empty QVariant of the given type ID.
/// Qt6: QVariant(QMetaType(typeId))
/// Qt5: QVariant(typeId, nullptr)
inline QVariant emptyVariantOfType(int typeId) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  return QVariant(QMetaType(typeId));
#else
  return QVariant(typeId, nullptr);
#endif
}

/// Creates a QVariant of the given type ID from a value pointer.
/// Qt6: QVariant(QMetaType(typeId), copy)
/// Qt5: QVariant(typeId, copy)
inline QVariant variantFromValue(int typeId, const void* copy) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  return QVariant(QMetaType(typeId), copy);
#else
  return QVariant(typeId, copy);
#endif
}

/// Whether the type ID is a pointer to a QObject-derived type.
/// Qt6: QMetaType(typeId).flags()
/// Qt5: QMetaType::typeFlags(typeId)
inline bool isQObjectPointerType(int typeId) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  return QMetaType(typeId).flags().testFlag(QMetaType::PointerToQObject);
#else
  return QMetaType::typeFlags(typeId).testFlag(QMetaType::PointerToQObject);
#endif
}

/// The QMetaObject a pointer type points at, or nullptr when it has none.
/// Qt6: QMetaType(typeId).metaObject()
/// Qt5: QMetaType(typeId).metaObject() exists too, but is reached through the
/// int-constructed instance rather than a static.
inline const QMetaObject* metaObjectForType(int typeId) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  return QMetaType(typeId).metaObject();
#else
  return QMetaType(typeId).metaObject();
#endif
}

}  // namespace compat
}  // namespace qtPilot
