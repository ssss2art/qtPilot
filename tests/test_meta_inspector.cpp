// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "common/qt_matchers.h"
#include "core/object_registry.h"
#include "introspection/meta_inspector.h"
#include "introspection/variant_json.h"

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QPoint>
#include <QPushButton>
#include <QRect>
#include <QSize>
#include <QWidget>
#include <QtTest>

using namespace qtPilot;
using namespace qtPilot::test;

/// @brief Test helper Q_GADGET exposing introspectable sub-properties.
class TestGadget {
  Q_GADGET
  Q_PROPERTY(int width MEMBER width)
  Q_PROPERTY(QString label MEMBER label)

 public:
  int width = 0;
  QString label;
};
Q_DECLARE_METATYPE(TestGadget)

/// @brief Test helper class with custom properties and signals
class TestObject : public QObject {
  Q_OBJECT
  Q_PROPERTY(int intValue READ intValue WRITE setIntValue NOTIFY intValueChanged)
  Q_PROPERTY(QString stringValue READ stringValue WRITE setStringValue)
  Q_PROPERTY(bool readOnly READ readOnly CONSTANT)
  Q_PROPERTY(Color color READ color WRITE setColor)

 public:
  enum Color { Red, Green, Blue };
  Q_ENUM(Color)

  explicit TestObject(QObject* parent = nullptr) : QObject(parent) {}

  int intValue() const { return m_intValue; }
  void setIntValue(int value) {
    if (m_intValue != value) {
      m_intValue = value;
      emit intValueChanged(value);
    }
  }

  QString stringValue() const { return m_stringValue; }
  void setStringValue(const QString& value) { m_stringValue = value; }

  bool readOnly() const { return true; }

  Color color() const { return m_color; }
  void setColor(Color c) { m_color = c; }

 public slots:
  void doSomething() {}
  int addNumbers(int a, int b) { return a + b; }

  /// Takes a pointer, like the app-side helpers a caller drives by object id.
  /// Dereferencing whatever the caller sent is how the probe used to crash its
  /// host, so these are the methods the pointer-argument tests aim at.
  QString describeObject(QObject* target) const {
    return target ? QString::fromUtf8(target->metaObject()->className()) : QStringLiteral("<null>");
  }
  int countChildren(QObject* target) const {
    return target ? static_cast<int>(target->children().size()) : -1;
  }

 signals:
  void intValueChanged(int newValue);
  void customSignal(const QString& message, int code);

 private:
  int m_intValue = 42;
  QString m_stringValue = QStringLiteral("test");
  Color m_color = Green;
};

/// @brief Test suite for MetaInspector and variant_json utilities
class TestMetaInspector : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase();
  void cleanupTestCase();

  // Variant conversion tests
  void testVariantToJsonBool();
  void testVariantToJsonNumbers();
  void testVariantToJsonString();
  void testVariantToJsonPoint();
  void testVariantToJsonSize();
  void testVariantToJsonRect();
  void testVariantToJsonColor();
  void testVariantToJsonList();
  void testVariantToJsonMap();
  void testVariantToJsonUnknown();

  // JSON to variant tests
  void testJsonToVariantBasic();
  void testJsonToVariantGeometry();
  void testJsonToVariantColor();
  void testJsonToVariantRoundTrip();

  // MetaInspector tests
  void testObjectInfo();
  void testObjectInfoWidget();
  void testListProperties();
  void testListPropertiesWidget();
  void testListMethods();
  void testListSignals();
  void testInheritanceChain();
  void testNullObject();

  // Property operations (OBJ-06, OBJ-07)
  void testGetPropertyString();
  void testGetPropertyInt();
  void testGetPropertyNotFound();
  void testSetPropertyString();
  void testSetPropertyInt();
  void testSetPropertyReadOnly();
  void testSetPropertyTypeCoercion();
  void testDynamicProperty();
  void testEnumPropertyMetadata();
  void testNotifySignalMetadata();
  void testVariantToJsonGadget();
  void testVariantToJsonQObjectRef();

  // Method invocation (OBJ-09)
  void testInvokeVoidMethod();
  void testInvokeMethodWithArgs();
  void testInvokeMethodWithReturnValue();
  void testInvokeMethodNotFound();
  void testInvokeMethodWrongArgCount();
  void testInvokePointerArgRejectsNumber();
  void testInvokePointerArgRejectsBool();
  void testInvokePointerArgRejectsArbitraryString();
  void testInvokePointerArgAcceptsNull();
  void testInvokePointerArgResolvesRegisteredObjectId();
  void testInvokePointerArgRejectsUnknownObjectId();
  void testMonadicPropertyOperations();
  void testMonadicMethodInvocation();

 private:
  QApplication* m_app = nullptr;
};

void TestMetaInspector::initTestCase() {
  // Create QApplication for widget tests
  static int argc = 1;
  static char* argv[] = {const_cast<char*>("test_meta_inspector")};
  m_app = new QApplication(argc, argv);
}

void TestMetaInspector::cleanupTestCase() {
  delete m_app;
  m_app = nullptr;
}

// ============================================================================
// Variant to JSON tests
// ============================================================================

void TestMetaInspector::testVariantToJsonBool() {
  QEXPECT_THAT(variantToJson(QVariant(true)), Eq(QJsonValue(true)));
  QEXPECT_THAT(variantToJson(QVariant(false)), Eq(QJsonValue(false)));
}

void TestMetaInspector::testVariantToJsonNumbers() {
  // Integer types
  QEXPECT_THAT(variantToJson(QVariant(42)), Eq(QJsonValue(42)));
  QEXPECT_THAT(variantToJson(QVariant(-17)), Eq(QJsonValue(-17)));

  // Floating point
  QEXPECT_THAT(variantToJson(QVariant(3.14)), Eq(QJsonValue(3.14)));
  QEXPECT_THAT(variantToJson(QVariant(float(2.5))), Eq(QJsonValue(2.5)));
}

void TestMetaInspector::testVariantToJsonString() {
  QEXPECT_THAT(variantToJson(QVariant(QStringLiteral("hello"))),
               Eq(QJsonValue(QStringLiteral("hello"))));
  QEXPECT_THAT(variantToJson(QVariant(QString())), Eq(QJsonValue(QString())));
}

void TestMetaInspector::testVariantToJsonPoint() {
  QJsonValue result = variantToJson(QVariant(QPoint(10, 20)));
  QEXPECT_THAT(result.isObject(), IsTrue());

  QJsonObject obj = result.toObject();
  QEXPECT_THAT(obj, HasJsonField("x", 10));
  QEXPECT_THAT(obj, HasJsonField("y", 20));

  // QPointF
  result = variantToJson(QVariant(QPointF(1.5, 2.5)));
  obj = result.toObject();
  QEXPECT_THAT(obj, HasJsonField("x", 1.5));
  QEXPECT_THAT(obj, HasJsonField("y", 2.5));
}

void TestMetaInspector::testVariantToJsonSize() {
  QJsonValue result = variantToJson(QVariant(QSize(100, 50)));
  QEXPECT_THAT(result.isObject(), IsTrue());

  QJsonObject obj = result.toObject();
  QEXPECT_THAT(obj, HasJsonField("width", 100));
  QEXPECT_THAT(obj, HasJsonField("height", 50));

  // QSizeF
  result = variantToJson(QVariant(QSizeF(10.5, 20.5)));
  obj = result.toObject();
  QEXPECT_THAT(obj, HasJsonField("width", 10.5));
  QEXPECT_THAT(obj, HasJsonField("height", 20.5));
}

void TestMetaInspector::testVariantToJsonRect() {
  QJsonValue result = variantToJson(QVariant(QRect(0, 0, 100, 50)));
  QEXPECT_THAT(result.isObject(), IsTrue());

  QJsonObject obj = result.toObject();
  QEXPECT_THAT(obj, HasJsonField("x", 0));
  QEXPECT_THAT(obj, HasJsonField("y", 0));
  QEXPECT_THAT(obj, HasJsonField("width", 100));
  QEXPECT_THAT(obj, HasJsonField("height", 50));

  // QRectF
  result = variantToJson(QVariant(QRectF(1.5, 2.5, 10.5, 20.5)));
  obj = result.toObject();
  QEXPECT_THAT(obj, HasJsonField("x", 1.5));
  QEXPECT_THAT(obj, HasJsonField("y", 2.5));
  QEXPECT_THAT(obj, HasJsonField("width", 10.5));
  QEXPECT_THAT(obj, HasJsonField("height", 20.5));
}

void TestMetaInspector::testVariantToJsonColor() {
  QJsonValue result = variantToJson(QVariant::fromValue(QColor(255, 0, 0)));
  QEXPECT_THAT(result.isObject(), IsTrue());

  QJsonObject obj = result.toObject();
  QEXPECT_THAT(obj, HasJsonField("r", 255));
  QEXPECT_THAT(obj, HasJsonField("g", 0));
  QEXPECT_THAT(obj, HasJsonField("b", 0));
  QEXPECT_THAT(obj, HasJsonField("a", 255));

  // With alpha
  result = variantToJson(QVariant::fromValue(QColor(0, 255, 0, 128)));
  obj = result.toObject();
  QEXPECT_THAT(obj, HasJsonField("r", 0));
  QEXPECT_THAT(obj, HasJsonField("g", 255));
  QEXPECT_THAT(obj, HasJsonField("b", 0));
  QEXPECT_THAT(obj, HasJsonField("a", 128));
}

void TestMetaInspector::testVariantToJsonList() {
  // QStringList
  QStringList strings = {QStringLiteral("one"), QStringLiteral("two"), QStringLiteral("three")};
  QJsonValue result = variantToJson(QVariant(strings));
  QEXPECT_THAT(result.isArray(), IsTrue());

  QJsonArray arr = result.toArray();
  QEXPECT_THAT(arr, JsonArraySize(3));
  QEXPECT_THAT(arr[0], QStrEq("one"));
  QEXPECT_THAT(arr[1], QStrEq("two"));
  QEXPECT_THAT(arr[2], QStrEq("three"));

  // QVariantList
  QVariantList list = {1, QStringLiteral("mixed"), true};
  result = variantToJson(QVariant(list));
  QEXPECT_THAT(result.isArray(), IsTrue());

  arr = result.toArray();
  QEXPECT_THAT(arr, JsonArraySize(3));
  QEXPECT_THAT(arr[0].toInt(), Eq(1));
  QEXPECT_THAT(arr[1], QStrEq("mixed"));
  QEXPECT_THAT(arr[2].toBool(), IsTrue());
}

void TestMetaInspector::testVariantToJsonMap() {
  QVariantMap map;
  map[QStringLiteral("name")] = QStringLiteral("test");
  map[QStringLiteral("value")] = 42;
  map[QStringLiteral("enabled")] = true;

  QJsonValue result = variantToJson(QVariant(map));
  QEXPECT_THAT(result.isObject(), IsTrue());

  QJsonObject obj = result.toObject();
  QEXPECT_THAT(obj, HasJsonField("name", "test"));
  QEXPECT_THAT(obj, HasJsonField("value", 42));
  QEXPECT_THAT(obj, HasJsonField("enabled", true));
}

void TestMetaInspector::testVariantToJsonUnknown() {
  // QFont is an unknown type that should fall back to structured output
  QFont font(QStringLiteral("Arial"), 12);
  QJsonValue result = variantToJson(QVariant::fromValue(font));
  QEXPECT_THAT(result.isObject(), IsTrue());

  QJsonObject obj = result.toObject();
  QEXPECT_THAT(obj, HasJsonField("_type", "QFont"));
  // value field should exist (may be string representation)
  QEXPECT_THAT(obj, HasJsonField("value"));
}

// ============================================================================
// JSON to Variant tests
// ============================================================================

void TestMetaInspector::testJsonToVariantBasic() {
  // Bool
  QEXPECT_THAT(jsonToVariant(QJsonValue(true)).toBool(), IsTrue());
  QEXPECT_THAT(jsonToVariant(QJsonValue(false)).toBool(), IsFalse());

  // Number
  QEXPECT_THAT(jsonToVariant(QJsonValue(42)).toDouble(), Eq(42.0));
  QEXPECT_THAT(jsonToVariant(QJsonValue(3.14)).toDouble(), Eq(3.14));

  // String
  QEXPECT_THAT(jsonToVariant(QJsonValue(QStringLiteral("hello"))).toString(), QStrEq("hello"));

  // Null
  QEXPECT_THAT(jsonToVariant(QJsonValue()).isValid(), IsFalse());
}

void TestMetaInspector::testJsonToVariantGeometry() {
  // Point-like object
  QJsonObject pointObj;
  pointObj[QStringLiteral("x")] = 10;
  pointObj[QStringLiteral("y")] = 20;
  QVariant result = jsonToVariant(pointObj);
  QEXPECT_THAT(result.toPoint(), Eq(QPoint(10, 20)));

  // Rect-like object
  QJsonObject rectObj;
  rectObj[QStringLiteral("x")] = 5;
  rectObj[QStringLiteral("y")] = 10;
  rectObj[QStringLiteral("width")] = 100;
  rectObj[QStringLiteral("height")] = 50;
  result = jsonToVariant(rectObj);
  QEXPECT_THAT(result.toRect(), Eq(QRect(5, 10, 100, 50)));

  // Size-like object
  QJsonObject sizeObj;
  sizeObj[QStringLiteral("width")] = 640;
  sizeObj[QStringLiteral("height")] = 480;
  result = jsonToVariant(sizeObj);
  QEXPECT_THAT(result.toSize(), Eq(QSize(640, 480)));
}

void TestMetaInspector::testJsonToVariantColor() {
  // RGB object
  QJsonObject colorObj;
  colorObj[QStringLiteral("r")] = 255;
  colorObj[QStringLiteral("g")] = 128;
  colorObj[QStringLiteral("b")] = 0;
  QVariant result = jsonToVariant(colorObj);
  QColor color = result.value<QColor>();
  QEXPECT_THAT(color.red(), Eq(255));
  QEXPECT_THAT(color.green(), Eq(128));
  QEXPECT_THAT(color.blue(), Eq(0));

  // With alpha
  colorObj[QStringLiteral("a")] = 200;
  result = jsonToVariant(colorObj);
  color = result.value<QColor>();
  QEXPECT_THAT(color.alpha(), Eq(200));

  // From string (with target type)
  result = jsonToVariant(QJsonValue(QStringLiteral("#FF0000")), QMetaType::QColor);
  color = result.value<QColor>();
  QEXPECT_THAT(color.red(), Eq(255));
  QEXPECT_THAT(color.green(), Eq(0));
  QEXPECT_THAT(color.blue(), Eq(0));
}

void TestMetaInspector::testJsonToVariantRoundTrip() {
  // Test round-trip for various types
  QPoint origPoint(50, 75);
  QVariant result = jsonToVariant(variantToJson(QVariant(origPoint)));
  QEXPECT_THAT(result.toPoint(), Eq(origPoint));

  QSize origSize(800, 600);
  result = jsonToVariant(variantToJson(QVariant(origSize)));
  QEXPECT_THAT(result.toSize(), Eq(origSize));

  QRect origRect(10, 20, 100, 200);
  result = jsonToVariant(variantToJson(QVariant(origRect)));
  QEXPECT_THAT(result.toRect(), Eq(origRect));

  QColor origColor(128, 64, 32, 255);
  result = jsonToVariant(variantToJson(QVariant::fromValue(origColor)));
  QEXPECT_THAT(result.value<QColor>(), Eq(origColor));
}

// ============================================================================
// MetaInspector tests
// ============================================================================

void TestMetaInspector::testObjectInfo() {
  TestObject obj;
  obj.setObjectName(QStringLiteral("testObj"));

  QJsonObject info = MetaInspector::objectInfo(&obj);

  QEXPECT_THAT(info, HasJsonField("className", "TestObject"));
  QEXPECT_THAT(info, HasJsonField("objectName", "testObj"));

  QJsonArray superClasses = info[QStringLiteral("superClasses")].toArray();
  QEXPECT_THAT(superClasses.size(), Ge(2));
  QEXPECT_THAT(superClasses[0], QStrEq("TestObject"));
  QEXPECT_THAT(superClasses[1], QStrEq("QObject"));
}

void TestMetaInspector::testObjectInfoWidget() {
  QPushButton button(QStringLiteral("Click Me"));
  button.setObjectName(QStringLiteral("submitBtn"));
  button.setEnabled(true);

  QJsonObject info = MetaInspector::objectInfo(&button);

  QEXPECT_THAT(info, HasJsonField("className", "QPushButton"));
  QEXPECT_THAT(info, HasJsonField("objectName", "submitBtn"));
  QEXPECT_THAT(info, HasJsonField("visible"));
  QEXPECT_THAT(info, HasJsonField("enabled", true));
}

void TestMetaInspector::testListProperties() {
  TestObject obj;
  obj.setIntValue(123);
  obj.setStringValue(QStringLiteral("hello"));
  // Dynamic property (not declared as Q_PROPERTY) — e.g. a QSS styling hook.
  obj.setProperty("status", QStringLiteral("error"));

  QJsonArray props = MetaInspector::listProperties(&obj);

  // Should have at least our 3 custom properties + objectName from QObject
  QEXPECT_THAT(props.size(), Ge(4));

  // Find our intValue property
  bool foundIntValue = false;
  bool foundStringValue = false;
  bool foundReadOnly = false;
  bool foundDynamicStatus = false;

  for (const QJsonValue& val : props) {
    QJsonObject prop = val.toObject();
    QString name = prop[QStringLiteral("name")].toString();

    if (name == QStringLiteral("intValue")) {
      foundIntValue = true;
      QEXPECT_THAT(prop, HasJsonField("type", "int"));
      QEXPECT_THAT(prop, HasJsonField("readable", true));
      QEXPECT_THAT(prop, HasJsonField("writable", true));
      QEXPECT_THAT(prop, HasJsonField("value", 123));
      // Statically-declared properties are flagged dynamic=false.
      QEXPECT_THAT(prop, HasJsonField("dynamic", false));
    } else if (name == QStringLiteral("stringValue")) {
      foundStringValue = true;
      QEXPECT_THAT(prop, HasJsonField("value", "hello"));
    } else if (name == QStringLiteral("readOnly")) {
      foundReadOnly = true;
      QEXPECT_THAT(prop, HasJsonField("writable", false));
      QEXPECT_THAT(prop, HasJsonField("value", true));
    } else if (name == QStringLiteral("status")) {
      // Dynamic properties are surfaced, flagged dynamic=true, read/write.
      foundDynamicStatus = true;
      QEXPECT_THAT(prop, HasJsonField("dynamic", true));
      QEXPECT_THAT(prop, HasJsonField("readable", true));
      QEXPECT_THAT(prop, HasJsonField("writable", true));
      QEXPECT_THAT(prop, HasJsonField("value", "error"));
    }
  }

  QEXPECT_THAT(foundIntValue, IsTrue());
  QEXPECT_THAT(foundStringValue, IsTrue());
  QEXPECT_THAT(foundReadOnly, IsTrue());
  QEXPECT_THAT(foundDynamicStatus, IsTrue());
}

void TestMetaInspector::testListPropertiesWidget() {
  QPushButton button(QStringLiteral("Test Button"));

  QJsonArray props = MetaInspector::listProperties(&button);

  // Should have many properties from QPushButton, QAbstractButton, QWidget
  QEXPECT_THAT(props.size(), Ge(10));

  // Find text and enabled properties
  bool foundText = false;
  bool foundEnabled = false;
  bool foundVisible = false;

  for (const QJsonValue& val : props) {
    QJsonObject prop = val.toObject();
    QString name = prop[QStringLiteral("name")].toString();

    if (name == QStringLiteral("text")) {
      foundText = true;
      QEXPECT_THAT(prop, HasJsonField("value", "Test Button"));
    } else if (name == QStringLiteral("enabled")) {
      foundEnabled = true;
    } else if (name == QStringLiteral("visible")) {
      foundVisible = true;
    }
  }

  QEXPECT_THAT(foundText, IsTrue());
  QEXPECT_THAT(foundEnabled, IsTrue());
  QEXPECT_THAT(foundVisible, IsTrue());
}

void TestMetaInspector::testListMethods() {
  TestObject obj;

  QJsonArray methods = MetaInspector::listMethods(&obj);

  // Should have at least our 2 custom slots + inherited deleteLater
  QEXPECT_THAT(methods.size(), Ge(3));

  bool foundDoSomething = false;
  bool foundAddNumbers = false;

  for (const QJsonValue& val : methods) {
    QJsonObject method = val.toObject();
    QString name = method[QStringLiteral("name")].toString();

    if (name == QStringLiteral("doSomething")) {
      foundDoSomething = true;
      QEXPECT_THAT(method, HasJsonField("signature", "doSomething()"));
      QEXPECT_THAT(method, HasJsonField("access", "public"));
    } else if (name == QStringLiteral("addNumbers")) {
      foundAddNumbers = true;
      QEXPECT_THAT(method, HasJsonField("signature", "addNumbers(int,int)"));
      QEXPECT_THAT(method, HasJsonField("returnType", "int"));

      QJsonArray paramTypes = method[QStringLiteral("parameterTypes")].toArray();
      QEXPECT_THAT(paramTypes, JsonArraySize(2));
      QEXPECT_THAT(paramTypes[0], QStrEq("int"));
      QEXPECT_THAT(paramTypes[1], QStrEq("int"));
    }
  }

  QEXPECT_THAT(foundDoSomething, IsTrue());
  QEXPECT_THAT(foundAddNumbers, IsTrue());
}

void TestMetaInspector::testListSignals() {
  TestObject obj;

  QJsonArray signalList = MetaInspector::listSignals(&obj);

  // Should have at least our 2 custom signals + destroyed/objectNameChanged from QObject
  QEXPECT_THAT(signalList.size(), Ge(4));

  bool foundIntValueChanged = false;
  bool foundCustomSignal = false;

  for (const QJsonValue& val : signalList) {
    QJsonObject sig = val.toObject();
    QString name = sig[QStringLiteral("name")].toString();

    if (name == QStringLiteral("intValueChanged")) {
      foundIntValueChanged = true;
      QEXPECT_THAT(sig, HasJsonField("signature", "intValueChanged(int)"));

      QJsonArray paramTypes = sig[QStringLiteral("parameterTypes")].toArray();
      QEXPECT_THAT(paramTypes, JsonArraySize(1));
      QEXPECT_THAT(paramTypes[0], QStrEq("int"));
    } else if (name == QStringLiteral("customSignal")) {
      foundCustomSignal = true;
      QEXPECT_THAT(sig, HasJsonField("signature", "customSignal(QString,int)"));

      QJsonArray paramTypes = sig[QStringLiteral("parameterTypes")].toArray();
      QEXPECT_THAT(paramTypes, JsonArraySize(2));
      QEXPECT_THAT(paramTypes[0], QStrEq("QString"));
      QEXPECT_THAT(paramTypes[1], QStrEq("int"));
    }
  }

  QEXPECT_THAT(foundIntValueChanged, IsTrue());
  QEXPECT_THAT(foundCustomSignal, IsTrue());
}

void TestMetaInspector::testInheritanceChain() {
  QPushButton button;

  QStringList chain = MetaInspector::inheritanceChain(&button);

  // QPushButton -> QAbstractButton -> QWidget -> QObject
  QEXPECT_THAT(chain.size(), Ge(4));
  QEXPECT_THAT(chain[0], QStrEq("QPushButton"));
  QEXPECT_THAT(chain[1], QStrEq("QAbstractButton"));
  QEXPECT_THAT(chain[2], QStrEq("QWidget"));
  QEXPECT_THAT(chain[3], QStrEq("QObject"));
}

void TestMetaInspector::testNullObject() {
  // All methods should handle nullptr gracefully
  QEXPECT_THAT(MetaInspector::objectInfo(nullptr).isEmpty(), IsTrue());
  QEXPECT_THAT(MetaInspector::listProperties(nullptr).isEmpty(), IsTrue());
  QEXPECT_THAT(MetaInspector::listMethods(nullptr).isEmpty(), IsTrue());
  QEXPECT_THAT(MetaInspector::listSignals(nullptr).isEmpty(), IsTrue());
  QEXPECT_THAT(MetaInspector::inheritanceChain(nullptr).isEmpty(), IsTrue());
}

// ============================================================================
// Property Get/Set tests (OBJ-06, OBJ-07)
// ============================================================================

void TestMetaInspector::testGetPropertyString() {
  QPushButton button(QStringLiteral("Hello Button"));
  QJsonValue result = MetaInspector::getProperty(&button, QStringLiteral("text"));
  QEXPECT_THAT(result.toString(), QStrEq("Hello Button"));
}

void TestMetaInspector::testGetPropertyInt() {
  TestObject obj;
  obj.setIntValue(123);
  QJsonValue result = MetaInspector::getProperty(&obj, QStringLiteral("intValue"));
  QEXPECT_THAT(result.toInt(), Eq(123));
}

void TestMetaInspector::testGetPropertyNotFound() {
  TestObject obj;
  bool exceptionThrown = false;
  try {
    MetaInspector::getProperty(&obj, QStringLiteral("nonExistentProperty"));
  } catch (const std::runtime_error& e) {
    exceptionThrown = true;
    QString msg = QString::fromStdString(e.what());
    QEXPECT_THAT(msg, QStrContains("not found"));
  }
  QEXPECT_THAT(exceptionThrown, IsTrue());
}

void TestMetaInspector::testSetPropertyString() {
  QPushButton button;
  bool success = MetaInspector::setProperty(&button, QStringLiteral("text"),
                                            QJsonValue(QStringLiteral("New Text")));
  QEXPECT_THAT(success, IsTrue());
  QEXPECT_THAT(button.text(), QStrEq("New Text"));
}

void TestMetaInspector::testSetPropertyInt() {
  TestObject obj;
  bool success = MetaInspector::setProperty(&obj, QStringLiteral("intValue"), QJsonValue(999));
  QEXPECT_THAT(success, IsTrue());
  QEXPECT_THAT(obj.intValue(), Eq(999));
}

void TestMetaInspector::testSetPropertyReadOnly() {
  TestObject obj;
  bool exceptionThrown = false;
  try {
    MetaInspector::setProperty(&obj, QStringLiteral("readOnly"), QJsonValue(false));
  } catch (const std::runtime_error& e) {
    exceptionThrown = true;
    QString msg = QString::fromStdString(e.what());
    QEXPECT_THAT(msg, QStrContains("read-only"));
  }
  QEXPECT_THAT(exceptionThrown, IsTrue());
}

void TestMetaInspector::testSetPropertyTypeCoercion() {
  TestObject obj;
  // Set int property using a JSON double (should coerce)
  bool success = MetaInspector::setProperty(&obj, QStringLiteral("intValue"), QJsonValue(42.0));
  QEXPECT_THAT(success, IsTrue());
  QEXPECT_THAT(obj.intValue(), Eq(42));
}

void TestMetaInspector::testDynamicProperty() {
  QObject obj;

  // Set dynamic property
  bool success = MetaInspector::setProperty(&obj, QStringLiteral("dynamicProp"),
                                            QJsonValue(QStringLiteral("dynamic value")));
  QEXPECT_THAT(success, IsTrue());

  // Get dynamic property
  QJsonValue result = MetaInspector::getProperty(&obj, QStringLiteral("dynamicProp"));
  QEXPECT_THAT(result.toString(), QStrEq("dynamic value"));
}

void TestMetaInspector::testEnumPropertyMetadata() {
  TestObject obj;
  obj.setColor(TestObject::Green);

  QJsonArray props = MetaInspector::listProperties(&obj);

  bool foundColor = false;
  for (const QJsonValue& val : props) {
    QJsonObject prop = val.toObject();
    if (prop[QStringLiteral("name")].toString() != QStringLiteral("color")) {
      continue;
    }
    foundColor = true;
    // Symbolic key for the current value, not just the raw int.
    QEXPECT_THAT(prop, HasJsonField("enumKey", "Green"));
    QEXPECT_THAT(prop, HasJsonField("isFlag", false));
    // Full set of valid keys is surfaced so callers can set by name.
    QJsonArray keys = prop[QStringLiteral("enumKeys")].toArray();
    QEXPECT_THAT(keys, JsonArraySize(3));
    QEXPECT_THAT(keys, JsonArrayContains(QJsonValue("Red")));
    QEXPECT_THAT(keys, JsonArrayContains(QJsonValue("Green")));
    QEXPECT_THAT(keys, JsonArrayContains(QJsonValue("Blue")));
  }
  QEXPECT_THAT(foundColor, IsTrue());
}

void TestMetaInspector::testNotifySignalMetadata() {
  TestObject obj;
  QJsonArray props = MetaInspector::listProperties(&obj);

  bool foundIntValue = false;
  bool foundStringValue = false;
  for (const QJsonValue& val : props) {
    QJsonObject prop = val.toObject();
    QString name = prop[QStringLiteral("name")].toString();
    if (name == QStringLiteral("intValue")) {
      foundIntValue = true;
      // intValue has NOTIFY intValueChanged — surfaced so callers know what
      // signal to subscribe to instead of polling.
      QEXPECT_THAT(prop, HasJsonField("notifySignal", "intValueChanged"));
    } else if (name == QStringLiteral("stringValue")) {
      foundStringValue = true;
      // No NOTIFY — empty string.
      QEXPECT_THAT(prop, HasJsonField("notifySignal", ""));
    }
  }
  QEXPECT_THAT(foundIntValue, IsTrue());
  QEXPECT_THAT(foundStringValue, IsTrue());
}

void TestMetaInspector::testVariantToJsonGadget() {
  // Q_GADGET value types with introspectable Q_PROPERTYs should serialize their
  // sub-properties rather than collapsing to an empty/opaque value.
  TestGadget gadget;
  gadget.width = 42;
  gadget.label = QStringLiteral("hi");

  QJsonValue result = variantToJson(QVariant::fromValue(gadget));
  QEXPECT_THAT(result.isObject(), IsTrue());

  QJsonObject obj = result.toObject();
  QEXPECT_THAT(obj, HasJsonField("_type", "TestGadget"));
  QEXPECT_THAT(obj, HasJsonField("width", 42));
  QEXPECT_THAT(obj, HasJsonField("label", "hi"));
}

void TestMetaInspector::testVariantToJsonQObjectRef() {
  // QObject* values serialize as a navigable reference (identity), not null.
  TestObject target;
  target.setObjectName(QStringLiteral("refTarget"));

  QJsonValue result = variantToJson(QVariant::fromValue<QObject*>(&target));
  QEXPECT_THAT(result.isObject(), IsTrue());

  QJsonObject obj = result.toObject();
  QEXPECT_THAT(obj, HasJsonField("className", "TestObject"));
  QEXPECT_THAT(obj, HasJsonField("objectName", "refTarget"));

  // A null QObject* serializes as JSON null.
  QEXPECT_THAT(variantToJson(QVariant::fromValue<QObject*>(nullptr)).isNull(), IsTrue());
}

// ============================================================================
// Method Invocation tests (OBJ-09)
// ============================================================================

void TestMetaInspector::testInvokeVoidMethod() {
  TestObject obj;
  // doSomething() is a void slot
  QJsonValue result = MetaInspector::invokeMethod(&obj, QStringLiteral("doSomething"));
  QEXPECT_THAT(result.isNull(), IsTrue());  // void methods return null
}

void TestMetaInspector::testInvokeMethodWithArgs() {
  TestObject obj;
  QJsonArray args;
  args.append(10);
  args.append(32);

  QJsonValue result = MetaInspector::invokeMethod(&obj, QStringLiteral("addNumbers"), args);
  QEXPECT_THAT(result.toInt(), Eq(42));
}

void TestMetaInspector::testInvokeMethodWithReturnValue() {
  TestObject obj;
  // addNumbers returns int
  QJsonArray args;
  args.append(5);
  args.append(7);
  QJsonValue result = MetaInspector::invokeMethod(&obj, QStringLiteral("addNumbers"), args);
  QEXPECT_THAT(result.toInt(), Eq(12));
}

void TestMetaInspector::testInvokeMethodNotFound() {
  TestObject obj;
  bool exceptionThrown = false;
  try {
    MetaInspector::invokeMethod(&obj, QStringLiteral("nonExistentMethod"));
  } catch (const std::runtime_error& e) {
    exceptionThrown = true;
    QString msg = QString::fromStdString(e.what());
    QEXPECT_THAT(msg, QStrContains("not found"));
  }
  QEXPECT_THAT(exceptionThrown, IsTrue());
}

void TestMetaInspector::testInvokeMethodWrongArgCount() {
  TestObject obj;
  QJsonArray args;
  args.append(1);  // addNumbers expects 2 args, we provide 1

  bool exceptionThrown = false;
  try {
    MetaInspector::invokeMethod(&obj, QStringLiteral("addNumbers"), args);
  } catch (const std::runtime_error& e) {
    exceptionThrown = true;
    QString msg = QString::fromStdString(e.what());
    QEXPECT_THAT(msg, AnyOf(QStrContains("not found"), QStrContains("wrong argument")));
  }
  QEXPECT_THAT(exceptionThrown, IsTrue());
}

// ---------------------------------------------------------------------------
// Pointer arguments
//
// A Q_INVOKABLE taking a pointer used to receive whatever the JSON value
// happened to coerce to, so `describeObject(1)` handed the app the address 1
// and segfaulted it. The probe must never crash its host over a bad parameter:
// every one of these has to come back as an error instead.
// ---------------------------------------------------------------------------

void TestMetaInspector::testInvokePointerArgRejectsNumber() {
  TestObject obj;
  QJsonArray args;
  args.append(1);
  QEXPECT_THAT([&] { MetaInspector::invokeMethod(&obj, QStringLiteral("describeObject"), args); },
               Throws<std::runtime_error>(WhatContains("pointer parameter")));
}

void TestMetaInspector::testInvokePointerArgRejectsBool() {
  TestObject obj;
  QJsonArray args;
  args.append(true);
  QEXPECT_THAT([&] { MetaInspector::invokeMethod(&obj, QStringLiteral("countChildren"), args); },
               Throws<std::runtime_error>(WhatContains("pointer parameter")));
}

void TestMetaInspector::testInvokePointerArgRejectsArbitraryString() {
  TestObject obj;
  QJsonArray args;
  args.append(QStringLiteral("not-an-object-id"));
  QEXPECT_THAT([&] { MetaInspector::invokeMethod(&obj, QStringLiteral("describeObject"), args); },
               Throws<std::runtime_error>(WhatContains("does not exist")));
}

void TestMetaInspector::testInvokePointerArgAcceptsNull() {
  TestObject obj;
  QJsonArray args;
  args.append(QJsonValue(QJsonValue::Null));
  // A deliberate null is a legitimate argument; the method decides what it means.
  const QJsonValue result =
      MetaInspector::invokeMethod(&obj, QStringLiteral("describeObject"), args);
  QEXPECT_THAT(result.toString(), QStrEq("<null>"));
}

void TestMetaInspector::testInvokePointerArgResolvesRegisteredObjectId() {
  TestObject obj;
  obj.setObjectName(QStringLiteral("pointerArgRoot"));
  auto* child = new QObject(&obj);
  child->setObjectName(QStringLiteral("pointerArgChild"));

  // In a live probe the registry has already seen the tree; make that true here
  // so the id the test hands in is one the resolver can actually look up.
  ObjectRegistry::instance()->scanExistingObjects(&obj);
  const QString childId = ObjectRegistry::instance()->objectId(child);

  QJsonArray args;
  args.append(childId);
  const QJsonValue result =
      MetaInspector::invokeMethod(&obj, QStringLiteral("describeObject"), args);

  // Resolving the id is what makes pointer-taking helpers callable at all.
  QEXPECT_THAT(result.toString(), QStrEq("QObject"));
}

void TestMetaInspector::testInvokePointerArgRejectsUnknownObjectId() {
  TestObject obj;
  QJsonArray args;
  args.append(QStringLiteral("MainWindow/NoSuchThing/AtAll"));
  QEXPECT_THAT([&] { MetaInspector::invokeMethod(&obj, QStringLiteral("describeObject"), args); },
               Throws<std::runtime_error>(WhatContains("does not exist")));
}

void TestMetaInspector::testMonadicPropertyOperations() {
  TestObject obj;
  obj.setStringValue(QStringLiteral("initial"));
  obj.setIntValue(42);

  // 1. Monadic read success and .transform()
  auto res = MetaInspector::getPropertyExpected(&obj, QStringLiteral("stringValue"))
                 .transform([](const QJsonValue& val) { return val.toString().toUpper(); });
  QVERIFY(res.has_value());
  QCOMPARE(*res, QStringLiteral("INITIAL"));

  // 2. Monadic read failure on nonexistent property
  auto failRes = MetaInspector::getPropertyExpected(&obj, QStringLiteral("noSuchProp"));
  QVERIFY(!failRes.has_value());
  QCOMPARE(failRes.error().kind, PropertyErrorKind::NotFound);
  QCOMPARE(failRes.error().propertyName, QStringLiteral("noSuchProp"));

  // 3. Monadic write success
  auto setRes = MetaInspector::setPropertyExpected(&obj, QStringLiteral("stringValue"),
                                                   QJsonValue(QStringLiteral("updated")));
  QVERIFY(setRes.has_value());
  QCOMPARE(obj.stringValue(), QStringLiteral("updated"));

  // 4. Monadic write failure on read-only property
  auto roRes =
      MetaInspector::setPropertyExpected(&obj, QStringLiteral("readOnly"), QJsonValue(false));
  QVERIFY(!roRes.has_value());
  QCOMPARE(roRes.error().kind, PropertyErrorKind::ReadOnly);

  // 5. Monadic write failure on null object
  auto nullSet =
      MetaInspector::setPropertyExpected(nullptr, QStringLiteral("intValue"), QJsonValue(123));
  QVERIFY(!nullSet.has_value());
  QCOMPARE(nullSet.error().kind, PropertyErrorKind::NullObject);

  // 6. Monadic chaining via .and_then()
  auto chained =
      MetaInspector::setPropertyExpected(&obj, QStringLiteral("intValue"), QJsonValue(100))
          .and_then([&]() {
            return MetaInspector::getPropertyExpected(&obj, QStringLiteral("intValue"));
          })
          .transform([](const QJsonValue& val) { return val.toInt() * 2; });
  QVERIFY(chained.has_value());
  QCOMPARE(*chained, 200);
}

void TestMetaInspector::testMonadicMethodInvocation() {
  TestObject obj;

  // 1. Monadic invocation success
  QJsonArray args;
  args.append(5);
  args.append(7);
  auto res = MetaInspector::invokeMethodExpected(&obj, QStringLiteral("addNumbers"), args)
                 .transform([](const QJsonValue& val) { return val.toInt(); });
  QVERIFY(res.has_value());
  QCOMPARE(*res, 12);

  // 2. Monadic invocation failure on missing method
  auto failRes =
      MetaInspector::invokeMethodExpected(&obj, QStringLiteral("nonExistentMethod"), args);
  QVERIFY(!failRes.has_value());
  QCOMPARE(failRes.error().kind, MethodErrorKind::NotFound);

  // 3. Monadic invocation failure on null target object
  auto nullRes = MetaInspector::invokeMethodExpected(nullptr, QStringLiteral("addNumbers"), args);
  QVERIFY(!nullRes.has_value());
  QCOMPARE(nullRes.error().kind, MethodErrorKind::NullObject);
}

QTEST_APPLESS_MAIN(TestMetaInspector)
#include "test_meta_inspector.moc"
