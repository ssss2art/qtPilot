// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "variant_json.h"

#include "compat/compat_core.h"
#include "compat/compat_variant.h"

#include <QColor>
#include <QDateTime>
#include <QMetaType>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QStringList>
#include <QUrl>

namespace qtPilot {

QJsonValue variantToJson(const QVariant& value) {
  if (!value.isValid()) {
    return QJsonValue();  // null
  }

  // Get the type ID for switching
  const int typeId = value.userType();

  // Handle basic types directly
  switch (typeId) {
    case QMetaType::Bool:
      return value.toBool();

    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Short:
    case QMetaType::UShort:
      return value.toLongLong();

    case QMetaType::Double:
    case QMetaType::Float:
      return value.toDouble();

    case QMetaType::QString:
      return value.toString();

    case QMetaType::QByteArray:
      return QString::fromLatin1(value.toByteArray().toBase64());

    case QMetaType::QChar:
      return QString(value.toChar());

    default:
      break;  // Handle complex types below
  }

  // Handle Qt geometric types explicitly (per RESEARCH.md pitfall #3)
  if (typeId == QMetaType::QPoint) {
    QPoint p = value.toPoint();
    return QJsonObject{{QStringLiteral("x"), p.x()}, {QStringLiteral("y"), p.y()}};
  }

  if (typeId == QMetaType::QPointF) {
    QPointF p = value.toPointF();
    return QJsonObject{{QStringLiteral("x"), p.x()}, {QStringLiteral("y"), p.y()}};
  }

  if (typeId == QMetaType::QSize) {
    QSize s = value.toSize();
    return QJsonObject{{QStringLiteral("width"), s.width()},
                       {QStringLiteral("height"), s.height()}};
  }

  if (typeId == QMetaType::QSizeF) {
    QSizeF s = value.toSizeF();
    return QJsonObject{{QStringLiteral("width"), s.width()},
                       {QStringLiteral("height"), s.height()}};
  }

  if (typeId == QMetaType::QRect) {
    QRect r = value.toRect();
    return QJsonObject{{QStringLiteral("x"), r.x()},
                       {QStringLiteral("y"), r.y()},
                       {QStringLiteral("width"), r.width()},
                       {QStringLiteral("height"), r.height()}};
  }

  if (typeId == QMetaType::QRectF) {
    QRectF r = value.toRectF();
    return QJsonObject{{QStringLiteral("x"), r.x()},
                       {QStringLiteral("y"), r.y()},
                       {QStringLiteral("width"), r.width()},
                       {QStringLiteral("height"), r.height()}};
  }

  if (typeId == QMetaType::QColor) {
    QColor c = value.value<QColor>();
    return QJsonObject{{QStringLiteral("r"), c.red()},
                       {QStringLiteral("g"), c.green()},
                       {QStringLiteral("b"), c.blue()},
                       {QStringLiteral("a"), c.alpha()}};
  }

  // Handle URL
  if (typeId == QMetaType::QUrl) {
    return value.toUrl().toString();
  }

  // Handle date/time types
  if (typeId == QMetaType::QDateTime) {
    return value.toDateTime().toString(Qt::ISODate);
  }

  if (typeId == QMetaType::QDate) {
    return value.toDate().toString(Qt::ISODate);
  }

  if (typeId == QMetaType::QTime) {
    return value.toTime().toString(Qt::ISODate);
  }

  // Handle list types
  if (typeId == QMetaType::QStringList) {
    QJsonArray arr;
    for (const QString& s : value.toStringList()) {
      arr.append(s);
    }
    return arr;
  }

  if (typeId == QMetaType::QVariantList) {
    QJsonArray arr;
    for (const QVariant& v : value.toList()) {
      arr.append(variantToJson(v));
    }
    return arr;
  }

  // Handle map types
  if (typeId == QMetaType::QVariantMap) {
    QJsonObject obj;
    const QVariantMap map = value.toMap();
    for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
      obj.insert(it.key(), variantToJson(it.value()));
    }
    return obj;
  }

  if (typeId == QMetaType::QVariantHash) {
    QJsonObject obj;
    const QVariantHash hash = value.toHash();
    for (auto it = hash.constBegin(); it != hash.constEnd(); ++it) {
      obj.insert(it.key(), variantToJson(it.value()));
    }
    return obj;
  }

  // For unknown types, try toString() and include type name
  // This ensures we can always return something useful
  QString strValue = value.toString();
  QString typeName = QString::fromLatin1(value.typeName());

  // If toString() returns a useful value, use structured fallback
  if (!strValue.isEmpty() && strValue != typeName) {
    return QJsonObject{{QStringLiteral("_type"), typeName}, {QStringLiteral("value"), strValue}};
  }

  // Last resort: just return the type name with empty value
  return QJsonObject{{QStringLiteral("_type"), typeName}, {QStringLiteral("value"), QJsonValue()}};
}

QVariant jsonToVariant(const QJsonValue& value, int targetTypeId) {
  // If no target type specified, infer from JSON type
  if (targetTypeId == QMetaType::UnknownType) {
    switch (value.type()) {
      case QJsonValue::Null:
      case QJsonValue::Undefined:
        return QVariant();

      case QJsonValue::Bool:
        return value.toBool();

      case QJsonValue::Double:
        return value.toDouble();

      case QJsonValue::String:
        return value.toString();

      case QJsonValue::Array: {
        QVariantList list;
        for (const QJsonValue& v : value.toArray()) {
          list.append(jsonToVariant(v));
        }
        return list;
      }

      case QJsonValue::Object: {
        QJsonObject obj = value.toObject();

        // Check for special typed object (from variantToJson fallback)
        if (obj.contains(QStringLiteral("_type")) && obj.contains(QStringLiteral("value"))) {
          QString typeName = obj[QStringLiteral("_type")].toString();
          int metaType = qtPilot::compat::metaTypeIdFromName(typeName.toLatin1().constData());
          if (metaType != QMetaType::UnknownType) {
            return jsonToVariant(obj[QStringLiteral("value")], metaType);
          }
        }

        // Check for geometry types by shape
        if (obj.contains(QStringLiteral("x")) && obj.contains(QStringLiteral("y"))) {
          if (obj.contains(QStringLiteral("width")) && obj.contains(QStringLiteral("height"))) {
            // Looks like a rect
            double x = obj[QStringLiteral("x")].toDouble();
            double y = obj[QStringLiteral("y")].toDouble();
            double w = obj[QStringLiteral("width")].toDouble();
            double h = obj[QStringLiteral("height")].toDouble();

            // Check if values are integers
            if (x == qRound(x) && y == qRound(y) && w == qRound(w) && h == qRound(h)) {
              return QRect(static_cast<int>(x), static_cast<int>(y), static_cast<int>(w),
                           static_cast<int>(h));
            }
            return QRectF(x, y, w, h);
          } else {
            // Looks like a point
            double x = obj[QStringLiteral("x")].toDouble();
            double y = obj[QStringLiteral("y")].toDouble();
            if (x == qRound(x) && y == qRound(y)) {
              return QPoint(static_cast<int>(x), static_cast<int>(y));
            }
            return QPointF(x, y);
          }
        }

        if (obj.contains(QStringLiteral("width")) && obj.contains(QStringLiteral("height"))) {
          // Looks like a size
          double w = obj[QStringLiteral("width")].toDouble();
          double h = obj[QStringLiteral("height")].toDouble();
          if (w == qRound(w) && h == qRound(h)) {
            return QSize(static_cast<int>(w), static_cast<int>(h));
          }
          return QSizeF(w, h);
        }

        if (obj.contains(QStringLiteral("r")) && obj.contains(QStringLiteral("g")) &&
            obj.contains(QStringLiteral("b"))) {
          // Looks like a color
          int r = obj[QStringLiteral("r")].toInt();
          int g = obj[QStringLiteral("g")].toInt();
          int b = obj[QStringLiteral("b")].toInt();
          int a = obj.contains(QStringLiteral("a")) ? obj[QStringLiteral("a")].toInt() : 255;
          return QColor(r, g, b, a);
        }

        // Generic object -> QVariantMap
        QVariantMap map;
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
          map.insert(it.key(), jsonToVariant(it.value()));
        }
        return map;
      }
      default:
        return QVariant();
    }
  }

  // Target type specified - convert accordingly
  switch (targetTypeId) {
    case QMetaType::Bool:
      return value.toBool();

    case QMetaType::Int:
      return static_cast<int>(value.toDouble());

    case QMetaType::UInt:
      return static_cast<uint>(value.toDouble());

    case QMetaType::LongLong:
      return static_cast<qlonglong>(value.toDouble());

    case QMetaType::ULongLong:
      return static_cast<qulonglong>(value.toDouble());

    case QMetaType::Double:
      return value.toDouble();

    case QMetaType::Float:
      return static_cast<float>(value.toDouble());

    case QMetaType::QString:
      return value.toString();

    case QMetaType::QPoint:
      if (value.isObject()) {
        QJsonObject obj = value.toObject();
        return QPoint(obj[QStringLiteral("x")].toInt(), obj[QStringLiteral("y")].toInt());
      }
      break;

    case QMetaType::QPointF:
      if (value.isObject()) {
        QJsonObject obj = value.toObject();
        return QPointF(obj[QStringLiteral("x")].toDouble(), obj[QStringLiteral("y")].toDouble());
      }
      break;

    case QMetaType::QSize:
      if (value.isObject()) {
        QJsonObject obj = value.toObject();
        return QSize(obj[QStringLiteral("width")].toInt(), obj[QStringLiteral("height")].toInt());
      }
      break;

    case QMetaType::QSizeF:
      if (value.isObject()) {
        QJsonObject obj = value.toObject();
        return QSizeF(obj[QStringLiteral("width")].toDouble(),
                      obj[QStringLiteral("height")].toDouble());
      }
      break;

    case QMetaType::QRect:
      if (value.isObject()) {
        QJsonObject obj = value.toObject();
        return QRect(obj[QStringLiteral("x")].toInt(), obj[QStringLiteral("y")].toInt(),
                     obj[QStringLiteral("width")].toInt(), obj[QStringLiteral("height")].toInt());
      }
      break;

    case QMetaType::QRectF:
      if (value.isObject()) {
        QJsonObject obj = value.toObject();
        return QRectF(obj[QStringLiteral("x")].toDouble(), obj[QStringLiteral("y")].toDouble(),
                      obj[QStringLiteral("width")].toDouble(),
                      obj[QStringLiteral("height")].toDouble());
      }
      break;

    case QMetaType::QColor:
      if (value.isObject()) {
        QJsonObject obj = value.toObject();
        return QColor(obj[QStringLiteral("r")].toInt(), obj[QStringLiteral("g")].toInt(),
                      obj[QStringLiteral("b")].toInt(),
                      obj.contains(QStringLiteral("a")) ? obj[QStringLiteral("a")].toInt() : 255);
      } else if (value.isString()) {
        // Support "#RRGGBB" string format
        return QColor(value.toString());
      }
      break;

    case QMetaType::QUrl:
      return QUrl(value.toString());

    case QMetaType::QDateTime:
      return QDateTime::fromString(value.toString(), Qt::ISODate);

    case QMetaType::QDate:
      return QDate::fromString(value.toString(), Qt::ISODate);

    case QMetaType::QTime:
      return QTime::fromString(value.toString(), Qt::ISODate);

    case QMetaType::QStringList:
      if (value.isArray()) {
        QStringList list;
        for (const QJsonValue& v : value.toArray()) {
          list.append(v.toString());
        }
        return list;
      }
      break;

    case QMetaType::QVariantList:
      if (value.isArray()) {
        QVariantList list;
        for (const QJsonValue& v : value.toArray()) {
          list.append(jsonToVariant(v));
        }
        return list;
      }
      break;

    case QMetaType::QVariantMap:
      if (value.isObject()) {
        QVariantMap map;
        QJsonObject obj = value.toObject();
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
          map.insert(it.key(), jsonToVariant(it.value()));
        }
        return map;
      }
      break;

    default:
      // Try QMetaType conversion for other types
      QVariant result = jsonToVariant(value);  // Infer type first
      if (qtPilot::compat::variantCanConvert(result, targetTypeId)) {
        qtPilot::compat::variantConvert(result, targetTypeId);
        return result;
      }
      break;
  }

  // Fallback: return as-is with inferred type
  return jsonToVariant(value);
}

QString variantTypeName(const QVariant& value) {
  if (!value.isValid()) {
    return QStringLiteral("Invalid");
  }

  const char* name = value.typeName();
  if (name) {
    return QString::fromLatin1(name);
  }

  return QStringLiteral("Unknown");
}

}  // namespace qtPilot
