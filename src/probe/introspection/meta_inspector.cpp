// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "meta_inspector.h"

#include "compat/compat_core.h"
#include "compat/compat_variant.h"
#include "variant_json.h"

#include <QMetaMethod>
#include <QMetaObject>
#include <QMetaProperty>
#include <QWidget>

namespace qtPilot {

QJsonObject MetaInspector::objectInfo(QObject* obj) {
  if (!obj) {
    return QJsonObject();
  }

  QJsonObject info;
  info[QStringLiteral("className")] = QString::fromLatin1(obj->metaObject()->className());
  info[QStringLiteral("objectName")] = obj->objectName();
  info[QStringLiteral("superClasses")] = QJsonArray::fromStringList(inheritanceChain(obj));

  // Add widget-specific info
  if (auto* widget = qobject_cast<QWidget*>(obj)) {
    info[QStringLiteral("visible")] = widget->isVisible();
    info[QStringLiteral("enabled")] = widget->isEnabled();
    // Note: Geometry will be added via separate function in Plan 05 (UI Interaction)
  }

  return info;
}

QJsonArray MetaInspector::listProperties(QObject* obj) {
  if (!obj) {
    return QJsonArray();
  }

  QJsonArray result;
  const QMetaObject* meta = obj->metaObject();

  // Iterate through all properties (including inherited from QObject)
  // Using 0 instead of propertyOffset() to include QObject base properties
  // like objectName, which are often useful
  for (int i = 0; i < meta->propertyCount(); ++i) {
    QMetaProperty prop = meta->property(i);

    QJsonObject propInfo;
    propInfo[QStringLiteral("name")] = QString::fromLatin1(prop.name());
    propInfo[QStringLiteral("type")] = QString::fromLatin1(prop.typeName());
    propInfo[QStringLiteral("readable")] = prop.isReadable();
    propInfo[QStringLiteral("writable")] = prop.isWritable();
    propInfo[QStringLiteral("dynamic")] = false;

    // Notify signal name lets callers know what to subscribe to for change
    // notifications instead of polling. Empty when the property has none.
    if (prop.hasNotifySignal()) {
      propInfo[QStringLiteral("notifySignal")] = QString::fromLatin1(prop.notifySignal().name());
    } else {
      propInfo[QStringLiteral("notifySignal")] = QString();
    }

    // Include current value if readable
    QVariant value;
    if (prop.isReadable()) {
      value = prop.read(obj);
      propInfo[QStringLiteral("value")] = variantToJson(value);
    } else {
      propInfo[QStringLiteral("value")] = QJsonValue();
    }

    // Enum/flag properties otherwise serialize as opaque integers. Surface the
    // symbolic key(s) and the full set of valid keys so callers can read and
    // set them by name (e.g. alignment 132 -> "AlignLeft|AlignVCenter").
    if (prop.isEnumType() || prop.isFlagType()) {
      const QMetaEnum me = prop.enumerator();
      if (me.isValid()) {
        const int intValue = value.toInt();
        const char* keyStr = me.valueToKey(intValue);
        const QByteArray key =
            prop.isFlagType() ? me.valueToKeys(intValue) : QByteArray(keyStr ? keyStr : "");
        propInfo[QStringLiteral("enumKey")] = QString::fromLatin1(key);
        propInfo[QStringLiteral("isFlag")] = prop.isFlagType();
        QJsonArray keys;
        for (int k = 0; k < me.keyCount(); ++k) {
          const char* keyName = me.key(k);
          keys.append(QString::fromLatin1(keyName ? keyName : ""));
        }
        propInfo[QStringLiteral("enumKeys")] = keys;
      }
    }

    result.append(propInfo);
  }

  // Dynamic properties (set via QObject::setProperty with a name not declared
  // as a Q_PROPERTY) are not part of the static meta-object. They are widely
  // used for QSS styling hooks (e.g. setProperty("status", "error") driving a
  // [status="error"] selector), so surface them too, flagged dynamic.
  const QList<QByteArray> dynamicNames = obj->dynamicPropertyNames();
  for (const QByteArray& name : dynamicNames) {
    const QVariant value = obj->property(name.constData());

    QJsonObject propInfo;
    propInfo[QStringLiteral("name")] = QString::fromLatin1(name);
    const char* typeName = value.typeName();
    propInfo[QStringLiteral("type")] = QString::fromLatin1(typeName ? typeName : "");
    propInfo[QStringLiteral("readable")] = true;
    propInfo[QStringLiteral("writable")] = true;
    propInfo[QStringLiteral("dynamic")] = true;
    propInfo[QStringLiteral("value")] = variantToJson(value);

    result.append(propInfo);
  }

  return result;
}

QJsonArray MetaInspector::listMethods(QObject* obj) {
  if (!obj) {
    return QJsonArray();
  }

  QJsonArray result;
  const QMetaObject* meta = obj->metaObject();

  // Iterate through all methods
  for (int i = 0; i < meta->methodCount(); ++i) {
    QMetaMethod method = meta->method(i);

    // Filter for Slot or Invokable methods only (not signals or constructors)
    if (method.methodType() != QMetaMethod::Slot &&
        method.methodType() != QMetaMethod::Method) {  // Q_INVOKABLE is Method type
      continue;
    }

    QJsonObject methodInfo;
    methodInfo[QStringLiteral("name")] = QString::fromLatin1(method.name());
    methodInfo[QStringLiteral("signature")] = QString::fromLatin1(method.methodSignature());

    // Return type (empty string for void)
    QString returnType = QString::fromLatin1(method.typeName());
    methodInfo[QStringLiteral("returnType")] = returnType;

    // Parameter types and names
    methodInfo[QStringLiteral("parameterTypes")] = extractParameterTypes(method);
    methodInfo[QStringLiteral("parameterNames")] = extractParameterNames(method);

    // Access specifier
    methodInfo[QStringLiteral("access")] = accessSpecifierToString(method.access());

    result.append(methodInfo);
  }

  return result;
}

QJsonArray MetaInspector::listSignals(QObject* obj) {
  if (!obj) {
    return QJsonArray();
  }

  QJsonArray result;
  const QMetaObject* meta = obj->metaObject();

  // Iterate through all methods looking for signals
  for (int i = 0; i < meta->methodCount(); ++i) {
    QMetaMethod method = meta->method(i);

    // Filter for Signal methods only
    if (method.methodType() != QMetaMethod::Signal) {
      continue;
    }

    QJsonObject signalInfo;
    signalInfo[QStringLiteral("name")] = QString::fromLatin1(method.name());
    signalInfo[QStringLiteral("signature")] = QString::fromLatin1(method.methodSignature());
    signalInfo[QStringLiteral("parameterTypes")] = extractParameterTypes(method);
    signalInfo[QStringLiteral("parameterNames")] = extractParameterNames(method);

    result.append(signalInfo);
  }

  return result;
}

QStringList MetaInspector::inheritanceChain(QObject* obj) {
  QStringList chain;

  if (!obj) {
    return chain;
  }

  const QMetaObject* meta = obj->metaObject();
  while (meta) {
    chain.append(QString::fromLatin1(meta->className()));
    meta = meta->superClass();
  }

  return chain;
}

QString MetaInspector::accessSpecifierToString(int access) {
  switch (access) {
    case QMetaMethod::Private:
      return QStringLiteral("private");
    case QMetaMethod::Protected:
      return QStringLiteral("protected");
    case QMetaMethod::Public:
    default:
      return QStringLiteral("public");
  }
}

QJsonArray MetaInspector::extractParameterTypes(const QMetaMethod& method) {
  QJsonArray types;
  for (const QByteArray& type : method.parameterTypes()) {
    types.append(QString::fromLatin1(type));
  }
  return types;
}

QJsonArray MetaInspector::extractParameterNames(const QMetaMethod& method) {
  QJsonArray names;
  for (const QByteArray& name : method.parameterNames()) {
    names.append(QString::fromLatin1(name));
  }
  return names;
}

QJsonValue MetaInspector::getProperty(QObject* obj, const QString& name) {
  if (!obj) {
    throw std::runtime_error("Cannot get property on null object");
  }

  const QMetaObject* meta = obj->metaObject();
  int propIndex = meta->indexOfProperty(name.toLatin1().constData());

  if (propIndex < 0) {
    // Try dynamic property
    QVariant value = obj->property(name.toLatin1().constData());
    if (value.isValid()) {
      return variantToJson(value);
    }
    throw std::runtime_error("Property not found: " + name.toStdString());
  }

  QMetaProperty prop = meta->property(propIndex);
  if (!prop.isReadable()) {
    throw std::runtime_error("Property not readable: " + name.toStdString());
  }

  return variantToJson(prop.read(obj));
}

bool MetaInspector::setProperty(QObject* obj, const QString& name, const QJsonValue& value) {
  if (!obj) {
    throw std::runtime_error("Cannot set property on null object");
  }

  const QMetaObject* meta = obj->metaObject();
  int propIndex = meta->indexOfProperty(name.toLatin1().constData());

  if (propIndex < 0) {
    // Dynamic properties: setProperty returns false if property didn't exist
    // before, but the property IS set. We verify by reading it back.
    QVariant var = jsonToVariant(value);
    QByteArray nameBytes = name.toLatin1();
    obj->setProperty(nameBytes.constData(), var);
    // Verify the property was actually set
    return obj->property(nameBytes.constData()).isValid();
  }

  QMetaProperty prop = meta->property(propIndex);
  if (!prop.isWritable()) {
    throw std::runtime_error("Property is read-only: " + name.toStdString());
  }

  // Convert JSON to appropriate type
  QVariant var = jsonToVariant(value, prop.userType());

  // Attempt type conversion if needed
  if (var.userType() != prop.userType() && !qtPilot::compat::variantConvert(var, prop.userType())) {
    throw std::runtime_error("Cannot convert value to type: " +
                             QString::fromLatin1(prop.typeName()).toStdString());
  }

  return prop.write(obj, var);
}

QJsonValue MetaInspector::invokeMethod(QObject* obj, const QString& methodName,
                                       const QJsonArray& args) {
  if (!obj) {
    throw std::runtime_error("Cannot invoke method on null object");
  }

  if (args.count() > 10) {
    throw std::runtime_error("Too many arguments (max 10): " + methodName.toStdString());
  }

  const QMetaObject* meta = obj->metaObject();

  // Find method by name, matching argument count
  QMetaMethod foundMethod;
  for (int i = 0; i < meta->methodCount(); ++i) {
    QMetaMethod method = meta->method(i);
    if (QString::fromLatin1(method.name()) == methodName) {
      // Check if slot or invokable
      if (method.methodType() != QMetaMethod::Slot && method.methodType() != QMetaMethod::Method) {
        continue;
      }

      // Check argument count
      if (method.parameterCount() == args.count()) {
        foundMethod = method;
        break;
      }
    }
  }

  if (!foundMethod.isValid()) {
    throw std::runtime_error("Method not found or wrong argument count: " +
                             methodName.toStdString());
  }

  // Build arguments - must keep QVariants alive during invocation
  QList<QVariant> variantArgs;
  variantArgs.reserve(args.count());

  for (int i = 0; i < args.count(); ++i) {
    int paramType = foundMethod.parameterType(i);
    QVariant var = jsonToVariant(args[i], paramType);
    variantArgs.append(var);
  }

  // Build QGenericArgument array - points into variantArgs data
  QGenericArgument genericArgs[10];
  for (int i = 0; i < variantArgs.count(); ++i) {
    genericArgs[i] = QGenericArgument(compat::methodParameterTypeName(foundMethod, i),
                                      variantArgs[i].constData());
  }

  // Prepare return value storage
  QVariant returnValue;
  QGenericReturnArgument returnArg;
  if (foundMethod.returnType() != QMetaType::Void) {
    returnValue = qtPilot::compat::emptyVariantOfType(foundMethod.returnType());
    returnArg = QGenericReturnArgument(foundMethod.typeName(), returnValue.data());
  }

  // Invoke (use Qt::AutoConnection for thread safety)
  bool ok = foundMethod.invoke(obj, Qt::AutoConnection, returnArg, genericArgs[0], genericArgs[1],
                               genericArgs[2], genericArgs[3], genericArgs[4], genericArgs[5],
                               genericArgs[6], genericArgs[7], genericArgs[8], genericArgs[9]);

  if (!ok) {
    throw std::runtime_error("Method invocation failed: " + methodName.toStdString());
  }

  if (foundMethod.returnType() == QMetaType::Void) {
    return QJsonValue::Null;
  }

  return variantToJson(returnValue);
}

}  // namespace qtPilot
