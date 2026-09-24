// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "meta_inspector.h"

#include "compat/compat_core.h"
#include "compat/compat_variant.h"
#include "core/object_resolver.h"
#include "variant_json.h"

#include <QMetaMethod>
#include <QMetaObject>
#include <QMetaProperty>
#include <QMetaType>
#include <QWidget>

namespace qtPilot {

namespace {

/// @brief Turn a JSON argument into a QObject pointer for a pointer parameter.
///
/// Only two shapes can produce a pointer the callee may safely dereference: an
/// explicit null, and an object id naming a live object of a compatible type.
/// Anything else - a number, a bool, an unregistered id - is a caller mistake,
/// and the alternative to rejecting it is handing the host application a
/// fabricated address to dereference.
std::expected<QObject*, MethodError> tryResolvePointerArgument(const QJsonValue& value, int typeId,
                                                               const QString& methodName,
                                                               int argIndex) {
  if (value.isNull() || value.isUndefined()) {
    return nullptr;
  }

  if (!value.isString()) {
    return std::unexpected(MethodError{
        MethodErrorKind::InvalidArgument, methodName,
        QStringLiteral("Argument %1 of '%2' is a pointer parameter, which takes an object id "
                       "string or null")
            .arg(argIndex)
            .arg(methodName)});
  }

  const QString objectId = value.toString();
  return ObjectResolver::resolveExpected(objectId)
      .transform_error([&](const ObjectResolver::ResolveError& err) {
        return MethodError{
            MethodErrorKind::InvalidArgument, methodName,
            QStringLiteral("Argument %1 of '%2' names an object that does not exist: '%3'")
                .arg(argIndex)
                .arg(methodName, err.id)};
      })
      .and_then([&](QObject* resolved) -> std::expected<QObject*, MethodError> {
        const QMetaObject* required = qtPilot::compat::metaObjectForType(typeId);
        if (required && !resolved->metaObject()->inherits(required)) {
          return std::unexpected(MethodError{
              MethodErrorKind::InvalidArgument, methodName,
              QStringLiteral("Argument %1 of '%2' resolved to a %3, which is not a %4")
                  .arg(argIndex)
                  .arg(methodName, QString::fromUtf8(resolved->metaObject()->className()),
                       QString::fromUtf8(required->className()))});
        }
        return resolved;
      });
}

}  // namespace

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

QJsonArray MetaInspector::listProperties(QObject* obj, bool declaredOnly,
                                         const QString& propertyName) {
  if (!obj) {
    return QJsonArray();
  }

  QJsonArray result;
  const QMetaObject* meta = obj->metaObject();

  int startIndex = 0;
  if (declaredOnly) {
    if (qobject_cast<QWidget*>(obj)) {
      startIndex = QWidget::staticMetaObject.propertyCount();
    } else {
      startIndex = QObject::staticMetaObject.propertyCount();
    }
  }

  // Iterate through properties (skipping base properties when declaredOnly is true)
  for (int i = startIndex; i < meta->propertyCount(); ++i) {
    QMetaProperty prop = meta->property(i);
    const QString propName = QString::fromLatin1(prop.name());
    if (!propertyName.isEmpty() && propName != propertyName) {
      continue;
    }

    QJsonObject propInfo;
    propInfo[QStringLiteral("name")] = propName;
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
    const QString dynName = QString::fromLatin1(name);
    if (!propertyName.isEmpty() && dynName != propertyName) {
      continue;
    }

    const QVariant value = obj->property(name.constData());

    QJsonObject propInfo;
    propInfo[QStringLiteral("name")] = dynName;
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

std::expected<QJsonValue, PropertyError> MetaInspector::getPropertyExpected(QObject* obj,
                                                                            const QString& name) {
  if (!obj) {
    return std::unexpected(PropertyError{PropertyErrorKind::NullObject, name,
                                         QStringLiteral("Cannot get property on null object")});
  }

  const QMetaObject* meta = obj->metaObject();
  int propIndex = meta->indexOfProperty(name.toLatin1().constData());

  if (propIndex < 0) {
    // Try dynamic property
    QVariant value = obj->property(name.toLatin1().constData());
    if (value.isValid()) {
      return variantToJson(value);
    }
    return std::unexpected(PropertyError{PropertyErrorKind::NotFound, name,
                                         QStringLiteral("Property not found: %1").arg(name)});
  }

  QMetaProperty prop = meta->property(propIndex);
  if (!prop.isReadable()) {
    return std::unexpected(PropertyError{PropertyErrorKind::NotReadable, name,
                                         QStringLiteral("Property not readable: %1").arg(name)});
  }

  return variantToJson(prop.read(obj));
}

QJsonValue MetaInspector::getProperty(QObject* obj, const QString& name) {
  auto res = getPropertyExpected(obj, name);
  if (!res) {
    throw std::runtime_error(res.error().message.toStdString());
  }
  return *res;
}

std::expected<void, PropertyError> MetaInspector::setPropertyExpected(QObject* obj,
                                                                      const QString& name,
                                                                      const QJsonValue& value) {
  if (!obj) {
    return std::unexpected(PropertyError{PropertyErrorKind::NullObject, name,
                                         QStringLiteral("Cannot set property on null object")});
  }

  const QMetaObject* meta = obj->metaObject();
  int propIndex = meta->indexOfProperty(name.toLatin1().constData());

  if (propIndex < 0) {
    // Dynamic properties: setProperty returns false if property didn't exist
    // before, but the property IS set. We verify by reading it back.
    QVariant var = jsonToVariant(value);
    QByteArray nameBytes = name.toLatin1();
    obj->setProperty(nameBytes.constData(), var);
    if (!obj->property(nameBytes.constData()).isValid()) {
      return std::unexpected(PropertyError{PropertyErrorKind::TypeMismatch, name,
                                           QStringLiteral("Property set failed: %1").arg(name)});
    }
    return {};
  }

  QMetaProperty prop = meta->property(propIndex);
  if (!prop.isWritable()) {
    return std::unexpected(PropertyError{PropertyErrorKind::ReadOnly, name,
                                         QStringLiteral("Property is read-only: %1").arg(name)});
  }

  // Convert JSON to appropriate type
  QVariant var = jsonToVariant(value, prop.userType());

  // Attempt type conversion if needed
  if (var.userType() != prop.userType() && !qtPilot::compat::variantConvert(var, prop.userType())) {
    return std::unexpected(PropertyError{PropertyErrorKind::TypeMismatch, name,
                                         QStringLiteral("Cannot convert value to type: %1")
                                             .arg(QString::fromLatin1(prop.typeName()))});
  }

  if (!prop.write(obj, var)) {
    return std::unexpected(PropertyError{PropertyErrorKind::TypeMismatch, name,
                                         QStringLiteral("Failed to write property: %1").arg(name)});
  }

  return {};
}

bool MetaInspector::setProperty(QObject* obj, const QString& name, const QJsonValue& value) {
  auto res = setPropertyExpected(obj, name, value);
  if (!res) {
    throw std::runtime_error(res.error().message.toStdString());
  }
  return true;
}

std::expected<PreparedInvocation, MethodError> MetaInspector::prepareInvocation(
    QObject* obj, const QString& methodName, const QJsonArray& args) {
  if (!obj) {
    return std::unexpected(MethodError{MethodErrorKind::NullObject, methodName,
                                       QStringLiteral("Cannot invoke method on null object")});
  }

  if (args.count() > 10) {
    return std::unexpected(
        MethodError{MethodErrorKind::TooManyArguments, methodName,
                    QStringLiteral("Too many arguments (max 10): %1").arg(methodName)});
  }

  const QMetaObject* meta = obj->metaObject();

  // Find method by name, matching argument count
  QMetaMethod foundMethod;
  for (int i = 0; i < meta->methodCount(); ++i) {
    QMetaMethod method = meta->method(i);
    if (QString::fromLatin1(method.name()) == methodName) {
      if (method.methodType() != QMetaMethod::Slot && method.methodType() != QMetaMethod::Method) {
        continue;
      }
      if (method.parameterCount() == args.count()) {
        foundMethod = method;
        break;
      }
    }
  }

  if (!foundMethod.isValid()) {
    return std::unexpected(MethodError{
        MethodErrorKind::NotFound, methodName,
        QStringLiteral("Method not found or wrong argument count: %1").arg(methodName)});
  }

  PreparedInvocation prepared;
  prepared.m_method = foundMethod;
  prepared.m_methodName = methodName;
  prepared.m_arguments.reserve(args.count());

  for (int i = 0; i < args.count(); ++i) {
    int paramType = foundMethod.parameterType(i);
    if (qtPilot::compat::isQObjectPointerType(paramType)) {
      auto ptrRes = tryResolvePointerArgument(args[i], paramType, methodName, i);
      if (!ptrRes) {
        return std::unexpected(ptrRes.error());
      }
      QObject* target = *ptrRes;
      if (target) {
        prepared.m_objectArguments.append(QPointer<QObject>(target));
      }
      prepared.m_arguments.append(qtPilot::compat::variantFromValue(paramType, &target));
      continue;
    }
    prepared.m_arguments.append(jsonToVariant(args[i], paramType));
  }

  return prepared;
}

std::expected<QJsonValue, MethodError> PreparedInvocation::invoke(QObject* obj) const {
  for (const QPointer<QObject>& argument : m_objectArguments) {
    if (argument.isNull()) {
      return std::unexpected(MethodError{
          MethodErrorKind::InvalidArgument, m_methodName,
          QStringLiteral("An object argument was destroyed before %1 ran").arg(m_methodName)});
    }
  }

  // Build QGenericArgument array - points into m_arguments, which outlives the call
  QGenericArgument genericArgs[10];
  for (int i = 0; i < m_arguments.count(); ++i) {
    genericArgs[i] =
        QGenericArgument(compat::methodParameterTypeName(m_method, i), m_arguments[i].constData());
  }

  // Prepare return value storage
  QVariant returnValue;
  QGenericReturnArgument returnArg;
  if (m_method.returnType() != QMetaType::Void) {
    returnValue = qtPilot::compat::emptyVariantOfType(m_method.returnType());
    returnArg = QGenericReturnArgument(m_method.typeName(), returnValue.data());
  }

  // Invoke (use Qt::AutoConnection for thread safety)
  bool ok = m_method.invoke(obj, Qt::AutoConnection, returnArg, genericArgs[0], genericArgs[1],
                            genericArgs[2], genericArgs[3], genericArgs[4], genericArgs[5],
                            genericArgs[6], genericArgs[7], genericArgs[8], genericArgs[9]);

  if (!ok) {
    return std::unexpected(
        MethodError{MethodErrorKind::InvocationFailed, m_methodName,
                    QStringLiteral("Method invocation failed: %1").arg(m_methodName)});
  }

  if (m_method.returnType() == QMetaType::Void) {
    return QJsonValue::Null;
  }

  return variantToJson(returnValue);
}

std::expected<QJsonValue, MethodError> MetaInspector::invokeMethodExpected(
    QObject* obj, const QString& methodName, const QJsonArray& args) {
  return prepareInvocation(obj, methodName, args)
      .and_then([obj](const PreparedInvocation& prepared) { return prepared.invoke(obj); });
}

QJsonValue MetaInspector::invokeMethod(QObject* obj, const QString& methodName,
                                       const QJsonArray& args) {
  auto res = invokeMethodExpected(obj, methodName, args);
  if (!res) {
    throw std::runtime_error(res.error().message.toStdString());
  }
  return *res;
}

}  // namespace qtPilot
