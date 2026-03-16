// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <QMetaMethod>
#include <QMetaType>
#include <QtGlobal>

namespace qtPilot {
namespace compat {

/// Returns the QMetaType ID for the given type name.
/// Qt6: QMetaType::fromName(name).id()
/// Qt5: QMetaType::type(name)
inline int metaTypeIdFromName(const char* name) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  return QMetaType::fromName(name).id();
#else
  return QMetaType::type(name);
#endif
}

/// Returns the parameter type name for a QMetaMethod at given index.
/// Qt6: QMetaMethod::parameterTypeName(index)
/// Qt5: QMetaMethod::parameterTypes().at(index).constData()
inline const char* methodParameterTypeName(const QMetaMethod& method, int index) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  return method.parameterTypeName(index);
#else
  return method.parameterTypes().at(index).constData();
#endif
}

}  // namespace compat
}  // namespace qtPilot
