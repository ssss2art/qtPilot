// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "core/probe.h"  // For QTPILOT_EXPORT

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QVariant>

namespace qtPilot {

/// @brief Convert QVariant to JSON value.
///
/// Handles common Qt types explicitly for proper JSON representation:
///   - bool, int, double, QString -> direct JSON equivalents
///   - QPoint/QPointF -> {"x": N, "y": N}
///   - QSize/QSizeF -> {"width": N, "height": N}
///   - QRect/QRectF -> {"x": N, "y": N, "width": N, "height": N}
///   - QColor -> {"r": N, "g": N, "b": N, "a": N}
///   - QStringList -> JSON array of strings
///   - QVariantList -> JSON array (recursive)
///   - QVariantMap -> JSON object (recursive)
///   - QUrl -> string representation
///   - QDateTime -> ISO 8601 string
///   - Unknown types -> {"_type": "TypeName", "value": "toString()"}
///
/// @param value The QVariant to convert.
/// @return JSON representation of the value.
QTPILOT_EXPORT QJsonValue variantToJson(const QVariant& value);

/// @brief Convert JSON value to QVariant.
///
/// Used for setProperty and invokeMethod arguments. If targetTypeId is
/// provided, attempts conversion to that type using QMetaType.
///
/// @param value The JSON value to convert.
/// @param targetTypeId Optional target type ID for explicit conversion.
///                     Use QMetaType::UnknownType to infer from JSON type.
/// @return QVariant containing the converted value.
QTPILOT_EXPORT QVariant jsonToVariant(const QJsonValue& value,
                                    int targetTypeId = QMetaType::UnknownType);

/// @brief Get a human-readable type name for a QVariant.
///
/// @param value The QVariant to get the type name for.
/// @return The type name string (e.g., "QString", "QPoint", "int").
QTPILOT_EXPORT QString variantTypeName(const QVariant& value);

}  // namespace qtPilot
