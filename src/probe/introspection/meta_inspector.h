// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "core/probe.h"  // For QTPILOT_EXPORT

#include <stdexcept>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QStringList>

namespace qtPilot {

/// @brief Utility class for QMetaObject introspection with JSON output.
///
/// Provides static methods to inspect QObjects via Qt's meta-object system:
///   - objectInfo(): Get detailed object information (OBJ-04)
///   - listProperties(): List all properties with values (OBJ-05)
///   - listMethods(): List all invokable methods (OBJ-08)
///   - listSignals(): List all signals (OBJ-10)
///
/// All methods return JSON-friendly output suitable for MCP protocol.
class QTPILOT_EXPORT MetaInspector {
 public:
  /// @brief Get detailed object info (OBJ-04).
  ///
  /// Returns a JSON object containing:
  ///   - className: The object's class name
  ///   - objectName: The object's objectName property
  ///   - superClasses: Array of ancestor class names
  ///   - visible: (if QWidget) whether the widget is visible
  ///   - enabled: (if QWidget) whether the widget is enabled
  ///
  /// @param obj The object to inspect.
  /// @return JSON object with object information.
  static QJsonObject objectInfo(QObject* obj);

  /// @brief List all properties (OBJ-05).
  ///
  /// Returns an array of property objects, each containing:
  ///   - name: Property name
  ///   - type: Property type name
  ///   - readable: Whether the property is readable
  ///   - writable: Whether the property is writable
  ///   - value: Current property value (via variantToJson)
  ///
  /// Properties include both the object's own properties and inherited ones.
  ///
  /// @param obj The object to inspect.
  /// @return JSON array of property information.
  static QJsonArray listProperties(QObject* obj);

  /// @brief List all invokable methods (OBJ-08).
  ///
  /// Returns an array of method objects for slots and Q_INVOKABLE methods:
  ///   - name: Method name (without parameters)
  ///   - signature: Full method signature
  ///   - returnType: Return type name (empty for void)
  ///   - parameterTypes: Array of parameter type names
  ///   - parameterNames: Array of parameter names (if available)
  ///   - access: "public", "protected", or "private"
  ///
  /// @param obj The object to inspect.
  /// @return JSON array of method information.
  static QJsonArray listMethods(QObject* obj);

  /// @brief List all signals (OBJ-10).
  ///
  /// Returns an array of signal objects, each containing:
  ///   - name: Signal name (without parameters)
  ///   - signature: Full signal signature
  ///   - parameterTypes: Array of parameter type names
  ///   - parameterNames: Array of parameter names (if available)
  ///
  /// @param obj The object to inspect.
  /// @return JSON array of signal information.
  static QJsonArray listSignals(QObject* obj);

  /// @brief Get inheritance chain.
  ///
  /// Returns a list of class names from the object's class up to QObject.
  /// Example: ["QPushButton", "QAbstractButton", "QWidget", "QObject"]
  ///
  /// @param obj The object to inspect.
  /// @return List of class names in inheritance order.
  static QStringList inheritanceChain(QObject* obj);

  /// @brief Get a single property value (OBJ-06).
  ///
  /// Retrieves the current value of a property by name. Supports both
  /// declared properties (via Q_PROPERTY) and dynamic properties.
  ///
  /// @param obj Target object.
  /// @param name Property name.
  /// @return JSON value of the property.
  /// @throws std::runtime_error if property not found or not readable.
  static QJsonValue getProperty(QObject* obj, const QString& name);

  /// @brief Set a property value (OBJ-07).
  ///
  /// Sets a property value with automatic type coercion. Supports both
  /// declared properties (via Q_PROPERTY) and dynamic properties.
  ///
  /// @param obj Target object.
  /// @param name Property name.
  /// @param value New value as JSON.
  /// @return true if the property was set successfully.
  /// @throws std::runtime_error if property not found or read-only.
  static bool setProperty(QObject* obj, const QString& name, const QJsonValue& value);

  /// @brief Invoke a method on an object (OBJ-09).
  ///
  /// Invokes a slot or Q_INVOKABLE method by name with JSON arguments.
  /// Finds the method by matching the name and argument count.
  ///
  /// @param obj Target object.
  /// @param methodName Method name (without signature/parameters).
  /// @param args Arguments as JSON array (max 10).
  /// @return Return value as JSON (null for void methods).
  /// @throws std::runtime_error if method not found or invocation fails.
  static QJsonValue invokeMethod(QObject* obj, const QString& methodName,
                                 const QJsonArray& args = QJsonArray());

 private:
  // Helper to convert QMetaMethod::Access to string
  static QString accessSpecifierToString(int access);

  // Helper to extract parameter type names from QMetaMethod
  static QJsonArray extractParameterTypes(const QMetaMethod& method);

  // Helper to extract parameter names from QMetaMethod
  static QJsonArray extractParameterNames(const QMetaMethod& method);
};

}  // namespace qtPilot
