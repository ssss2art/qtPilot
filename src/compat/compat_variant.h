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

}  // namespace compat
}  // namespace qtPilot
