// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

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

/// @brief Test helper class with custom properties and signals
class TestObject : public QObject {
  Q_OBJECT
  Q_PROPERTY(int intValue READ intValue WRITE setIntValue NOTIFY intValueChanged)
  Q_PROPERTY(QString stringValue READ stringValue WRITE setStringValue)
  Q_PROPERTY(bool readOnly READ readOnly CONSTANT)

 public:
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

 public slots:
  void doSomething() {}
  int addNumbers(int a, int b) { return a + b; }

 signals:
  void intValueChanged(int newValue);
  void customSignal(const QString& message, int code);

 private:
  int m_intValue = 42;
  QString m_stringValue = QStringLiteral("test");
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

  // Method invocation (OBJ-09)
  void testInvokeVoidMethod();
  void testInvokeMethodWithArgs();
  void testInvokeMethodWithReturnValue();
  void testInvokeMethodNotFound();
  void testInvokeMethodWrongArgCount();

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
  QCOMPARE(variantToJson(QVariant(true)), QJsonValue(true));
  QCOMPARE(variantToJson(QVariant(false)), QJsonValue(false));
}

void TestMetaInspector::testVariantToJsonNumbers() {
  // Integer types
  QCOMPARE(variantToJson(QVariant(42)), QJsonValue(42));
  QCOMPARE(variantToJson(QVariant(-17)), QJsonValue(-17));

  // Floating point
  QCOMPARE(variantToJson(QVariant(3.14)), QJsonValue(3.14));
  QCOMPARE(variantToJson(QVariant(float(2.5))), QJsonValue(2.5));
}

void TestMetaInspector::testVariantToJsonString() {
  QCOMPARE(variantToJson(QVariant(QStringLiteral("hello"))), QJsonValue(QStringLiteral("hello")));
  QCOMPARE(variantToJson(QVariant(QString())), QJsonValue(QString()));
}

void TestMetaInspector::testVariantToJsonPoint() {
  QJsonValue result = variantToJson(QVariant(QPoint(10, 20)));
  QVERIFY(result.isObject());

  QJsonObject obj = result.toObject();
  QCOMPARE(obj[QStringLiteral("x")].toInt(), 10);
  QCOMPARE(obj[QStringLiteral("y")].toInt(), 20);

  // QPointF
  result = variantToJson(QVariant(QPointF(1.5, 2.5)));
  obj = result.toObject();
  QCOMPARE(obj[QStringLiteral("x")].toDouble(), 1.5);
  QCOMPARE(obj[QStringLiteral("y")].toDouble(), 2.5);
}

void TestMetaInspector::testVariantToJsonSize() {
  QJsonValue result = variantToJson(QVariant(QSize(100, 50)));
  QVERIFY(result.isObject());

  QJsonObject obj = result.toObject();
  QCOMPARE(obj[QStringLiteral("width")].toInt(), 100);
  QCOMPARE(obj[QStringLiteral("height")].toInt(), 50);

  // QSizeF
  result = variantToJson(QVariant(QSizeF(10.5, 20.5)));
  obj = result.toObject();
  QCOMPARE(obj[QStringLiteral("width")].toDouble(), 10.5);
  QCOMPARE(obj[QStringLiteral("height")].toDouble(), 20.5);
}

void TestMetaInspector::testVariantToJsonRect() {
  QJsonValue result = variantToJson(QVariant(QRect(0, 0, 100, 50)));
  QVERIFY(result.isObject());

  QJsonObject obj = result.toObject();
  QCOMPARE(obj[QStringLiteral("x")].toInt(), 0);
  QCOMPARE(obj[QStringLiteral("y")].toInt(), 0);
  QCOMPARE(obj[QStringLiteral("width")].toInt(), 100);
  QCOMPARE(obj[QStringLiteral("height")].toInt(), 50);

  // QRectF
  result = variantToJson(QVariant(QRectF(1.5, 2.5, 10.5, 20.5)));
  obj = result.toObject();
  QCOMPARE(obj[QStringLiteral("x")].toDouble(), 1.5);
  QCOMPARE(obj[QStringLiteral("y")].toDouble(), 2.5);
  QCOMPARE(obj[QStringLiteral("width")].toDouble(), 10.5);
  QCOMPARE(obj[QStringLiteral("height")].toDouble(), 20.5);
}

void TestMetaInspector::testVariantToJsonColor() {
  QJsonValue result = variantToJson(QVariant::fromValue(QColor(255, 0, 0)));
  QVERIFY(result.isObject());

  QJsonObject obj = result.toObject();
  QCOMPARE(obj[QStringLiteral("r")].toInt(), 255);
  QCOMPARE(obj[QStringLiteral("g")].toInt(), 0);
  QCOMPARE(obj[QStringLiteral("b")].toInt(), 0);
  QCOMPARE(obj[QStringLiteral("a")].toInt(), 255);

  // With alpha
  result = variantToJson(QVariant::fromValue(QColor(0, 255, 0, 128)));
  obj = result.toObject();
  QCOMPARE(obj[QStringLiteral("r")].toInt(), 0);
  QCOMPARE(obj[QStringLiteral("g")].toInt(), 255);
  QCOMPARE(obj[QStringLiteral("b")].toInt(), 0);
  QCOMPARE(obj[QStringLiteral("a")].toInt(), 128);
}

void TestMetaInspector::testVariantToJsonList() {
  // QStringList
  QStringList strings = {QStringLiteral("one"), QStringLiteral("two"), QStringLiteral("three")};
  QJsonValue result = variantToJson(QVariant(strings));
  QVERIFY(result.isArray());

  QJsonArray arr = result.toArray();
  QCOMPARE(arr.size(), 3);
  QCOMPARE(arr[0].toString(), QStringLiteral("one"));
  QCOMPARE(arr[1].toString(), QStringLiteral("two"));
  QCOMPARE(arr[2].toString(), QStringLiteral("three"));

  // QVariantList
  QVariantList list = {1, QStringLiteral("mixed"), true};
  result = variantToJson(QVariant(list));
  QVERIFY(result.isArray());

  arr = result.toArray();
  QCOMPARE(arr.size(), 3);
  QCOMPARE(arr[0].toInt(), 1);
  QCOMPARE(arr[1].toString(), QStringLiteral("mixed"));
  QCOMPARE(arr[2].toBool(), true);
}

void TestMetaInspector::testVariantToJsonMap() {
  QVariantMap map;
  map[QStringLiteral("name")] = QStringLiteral("test");
  map[QStringLiteral("value")] = 42;
  map[QStringLiteral("enabled")] = true;

  QJsonValue result = variantToJson(QVariant(map));
  QVERIFY(result.isObject());

  QJsonObject obj = result.toObject();
  QCOMPARE(obj[QStringLiteral("name")].toString(), QStringLiteral("test"));
  QCOMPARE(obj[QStringLiteral("value")].toInt(), 42);
  QCOMPARE(obj[QStringLiteral("enabled")].toBool(), true);
}

void TestMetaInspector::testVariantToJsonUnknown() {
  // QFont is an unknown type that should fall back to structured output
  QFont font(QStringLiteral("Arial"), 12);
  QJsonValue result = variantToJson(QVariant::fromValue(font));
  QVERIFY(result.isObject());

  QJsonObject obj = result.toObject();
  QVERIFY(obj.contains(QStringLiteral("_type")));
  QCOMPARE(obj[QStringLiteral("_type")].toString(), QStringLiteral("QFont"));
  // value field should exist (may be string representation)
  QVERIFY(obj.contains(QStringLiteral("value")));
}

// ============================================================================
// JSON to Variant tests
// ============================================================================

void TestMetaInspector::testJsonToVariantBasic() {
  // Bool
  QCOMPARE(jsonToVariant(QJsonValue(true)).toBool(), true);
  QCOMPARE(jsonToVariant(QJsonValue(false)).toBool(), false);

  // Number
  QCOMPARE(jsonToVariant(QJsonValue(42)).toDouble(), 42.0);
  QCOMPARE(jsonToVariant(QJsonValue(3.14)).toDouble(), 3.14);

  // String
  QCOMPARE(jsonToVariant(QJsonValue(QStringLiteral("hello"))).toString(), QStringLiteral("hello"));

  // Null
  QVERIFY(!jsonToVariant(QJsonValue()).isValid());
}

void TestMetaInspector::testJsonToVariantGeometry() {
  // Point-like object
  QJsonObject pointObj;
  pointObj[QStringLiteral("x")] = 10;
  pointObj[QStringLiteral("y")] = 20;
  QVariant result = jsonToVariant(pointObj);
  QCOMPARE(result.toPoint(), QPoint(10, 20));

  // Rect-like object
  QJsonObject rectObj;
  rectObj[QStringLiteral("x")] = 5;
  rectObj[QStringLiteral("y")] = 10;
  rectObj[QStringLiteral("width")] = 100;
  rectObj[QStringLiteral("height")] = 50;
  result = jsonToVariant(rectObj);
  QCOMPARE(result.toRect(), QRect(5, 10, 100, 50));

  // Size-like object
  QJsonObject sizeObj;
  sizeObj[QStringLiteral("width")] = 640;
  sizeObj[QStringLiteral("height")] = 480;
  result = jsonToVariant(sizeObj);
  QCOMPARE(result.toSize(), QSize(640, 480));
}

void TestMetaInspector::testJsonToVariantColor() {
  // RGB object
  QJsonObject colorObj;
  colorObj[QStringLiteral("r")] = 255;
  colorObj[QStringLiteral("g")] = 128;
  colorObj[QStringLiteral("b")] = 0;
  QVariant result = jsonToVariant(colorObj);
  QColor color = result.value<QColor>();
  QCOMPARE(color.red(), 255);
  QCOMPARE(color.green(), 128);
  QCOMPARE(color.blue(), 0);

  // With alpha
  colorObj[QStringLiteral("a")] = 200;
  result = jsonToVariant(colorObj);
  color = result.value<QColor>();
  QCOMPARE(color.alpha(), 200);

  // From string (with target type)
  result = jsonToVariant(QJsonValue(QStringLiteral("#FF0000")), QMetaType::QColor);
  color = result.value<QColor>();
  QCOMPARE(color.red(), 255);
  QCOMPARE(color.green(), 0);
  QCOMPARE(color.blue(), 0);
}

void TestMetaInspector::testJsonToVariantRoundTrip() {
  // Test round-trip for various types
  QPoint origPoint(50, 75);
  QVariant result = jsonToVariant(variantToJson(QVariant(origPoint)));
  QCOMPARE(result.toPoint(), origPoint);

  QSize origSize(800, 600);
  result = jsonToVariant(variantToJson(QVariant(origSize)));
  QCOMPARE(result.toSize(), origSize);

  QRect origRect(10, 20, 100, 200);
  result = jsonToVariant(variantToJson(QVariant(origRect)));
  QCOMPARE(result.toRect(), origRect);

  QColor origColor(128, 64, 32, 255);
  result = jsonToVariant(variantToJson(QVariant::fromValue(origColor)));
  QCOMPARE(result.value<QColor>(), origColor);
}

// ============================================================================
// MetaInspector tests
// ============================================================================

void TestMetaInspector::testObjectInfo() {
  TestObject obj;
  obj.setObjectName(QStringLiteral("testObj"));

  QJsonObject info = MetaInspector::objectInfo(&obj);

  QCOMPARE(info[QStringLiteral("className")].toString(), QStringLiteral("TestObject"));
  QCOMPARE(info[QStringLiteral("objectName")].toString(), QStringLiteral("testObj"));

  QJsonArray superClasses = info[QStringLiteral("superClasses")].toArray();
  QVERIFY(superClasses.size() >= 2);
  QCOMPARE(superClasses[0].toString(), QStringLiteral("TestObject"));
  QCOMPARE(superClasses[1].toString(), QStringLiteral("QObject"));
}

void TestMetaInspector::testObjectInfoWidget() {
  QPushButton button(QStringLiteral("Click Me"));
  button.setObjectName(QStringLiteral("submitBtn"));
  button.setEnabled(true);

  QJsonObject info = MetaInspector::objectInfo(&button);

  QCOMPARE(info[QStringLiteral("className")].toString(), QStringLiteral("QPushButton"));
  QCOMPARE(info[QStringLiteral("objectName")].toString(), QStringLiteral("submitBtn"));
  QVERIFY(info.contains(QStringLiteral("visible")));
  QVERIFY(info.contains(QStringLiteral("enabled")));
  QCOMPARE(info[QStringLiteral("enabled")].toBool(), true);
}

void TestMetaInspector::testListProperties() {
  TestObject obj;
  obj.setIntValue(123);
  obj.setStringValue(QStringLiteral("hello"));

  QJsonArray props = MetaInspector::listProperties(&obj);

  // Should have at least our 3 custom properties + objectName from QObject
  QVERIFY(props.size() >= 4);

  // Find our intValue property
  bool foundIntValue = false;
  bool foundStringValue = false;
  bool foundReadOnly = false;

  for (const QJsonValue& val : props) {
    QJsonObject prop = val.toObject();
    QString name = prop[QStringLiteral("name")].toString();

    if (name == QStringLiteral("intValue")) {
      foundIntValue = true;
      QCOMPARE(prop[QStringLiteral("type")].toString(), QStringLiteral("int"));
      QCOMPARE(prop[QStringLiteral("readable")].toBool(), true);
      QCOMPARE(prop[QStringLiteral("writable")].toBool(), true);
      QCOMPARE(prop[QStringLiteral("value")].toInt(), 123);
    } else if (name == QStringLiteral("stringValue")) {
      foundStringValue = true;
      QCOMPARE(prop[QStringLiteral("value")].toString(), QStringLiteral("hello"));
    } else if (name == QStringLiteral("readOnly")) {
      foundReadOnly = true;
      QCOMPARE(prop[QStringLiteral("writable")].toBool(), false);
      QCOMPARE(prop[QStringLiteral("value")].toBool(), true);
    }
  }

  QVERIFY2(foundIntValue, "intValue property not found");
  QVERIFY2(foundStringValue, "stringValue property not found");
  QVERIFY2(foundReadOnly, "readOnly property not found");
}

void TestMetaInspector::testListPropertiesWidget() {
  QPushButton button(QStringLiteral("Test Button"));

  QJsonArray props = MetaInspector::listProperties(&button);

  // Should have many properties from QPushButton, QAbstractButton, QWidget
  QVERIFY(props.size() >= 10);

  // Find text and enabled properties
  bool foundText = false;
  bool foundEnabled = false;
  bool foundVisible = false;

  for (const QJsonValue& val : props) {
    QJsonObject prop = val.toObject();
    QString name = prop[QStringLiteral("name")].toString();

    if (name == QStringLiteral("text")) {
      foundText = true;
      QCOMPARE(prop[QStringLiteral("value")].toString(), QStringLiteral("Test Button"));
    } else if (name == QStringLiteral("enabled")) {
      foundEnabled = true;
    } else if (name == QStringLiteral("visible")) {
      foundVisible = true;
    }
  }

  QVERIFY2(foundText, "text property not found");
  QVERIFY2(foundEnabled, "enabled property not found");
  QVERIFY2(foundVisible, "visible property not found");
}

void TestMetaInspector::testListMethods() {
  TestObject obj;

  QJsonArray methods = MetaInspector::listMethods(&obj);

  // Should have at least our 2 custom slots + inherited deleteLater
  QVERIFY(methods.size() >= 3);

  bool foundDoSomething = false;
  bool foundAddNumbers = false;

  for (const QJsonValue& val : methods) {
    QJsonObject method = val.toObject();
    QString name = method[QStringLiteral("name")].toString();

    if (name == QStringLiteral("doSomething")) {
      foundDoSomething = true;
      QCOMPARE(method[QStringLiteral("signature")].toString(), QStringLiteral("doSomething()"));
      QCOMPARE(method[QStringLiteral("access")].toString(), QStringLiteral("public"));
    } else if (name == QStringLiteral("addNumbers")) {
      foundAddNumbers = true;
      QCOMPARE(method[QStringLiteral("signature")].toString(),
               QStringLiteral("addNumbers(int,int)"));
      QCOMPARE(method[QStringLiteral("returnType")].toString(), QStringLiteral("int"));

      QJsonArray paramTypes = method[QStringLiteral("parameterTypes")].toArray();
      QCOMPARE(paramTypes.size(), 2);
      QCOMPARE(paramTypes[0].toString(), QStringLiteral("int"));
      QCOMPARE(paramTypes[1].toString(), QStringLiteral("int"));
    }
  }

  QVERIFY2(foundDoSomething, "doSomething() slot not found");
  QVERIFY2(foundAddNumbers, "addNumbers() slot not found");
}

void TestMetaInspector::testListSignals() {
  TestObject obj;

  QJsonArray signalList = MetaInspector::listSignals(&obj);

  // Should have at least our 2 custom signals + destroyed/objectNameChanged from QObject
  QVERIFY(signalList.size() >= 4);

  bool foundIntValueChanged = false;
  bool foundCustomSignal = false;

  for (const QJsonValue& val : signalList) {
    QJsonObject sig = val.toObject();
    QString name = sig[QStringLiteral("name")].toString();

    if (name == QStringLiteral("intValueChanged")) {
      foundIntValueChanged = true;
      QCOMPARE(sig[QStringLiteral("signature")].toString(), QStringLiteral("intValueChanged(int)"));

      QJsonArray paramTypes = sig[QStringLiteral("parameterTypes")].toArray();
      QCOMPARE(paramTypes.size(), 1);
      QCOMPARE(paramTypes[0].toString(), QStringLiteral("int"));
    } else if (name == QStringLiteral("customSignal")) {
      foundCustomSignal = true;
      QCOMPARE(sig[QStringLiteral("signature")].toString(),
               QStringLiteral("customSignal(QString,int)"));

      QJsonArray paramTypes = sig[QStringLiteral("parameterTypes")].toArray();
      QCOMPARE(paramTypes.size(), 2);
      QCOMPARE(paramTypes[0].toString(), QStringLiteral("QString"));
      QCOMPARE(paramTypes[1].toString(), QStringLiteral("int"));
    }
  }

  QVERIFY2(foundIntValueChanged, "intValueChanged signal not found");
  QVERIFY2(foundCustomSignal, "customSignal signal not found");
}

void TestMetaInspector::testInheritanceChain() {
  QPushButton button;

  QStringList chain = MetaInspector::inheritanceChain(&button);

  // QPushButton -> QAbstractButton -> QWidget -> QObject
  QVERIFY(chain.size() >= 4);
  QCOMPARE(chain[0], QStringLiteral("QPushButton"));
  QCOMPARE(chain[1], QStringLiteral("QAbstractButton"));
  QCOMPARE(chain[2], QStringLiteral("QWidget"));
  QCOMPARE(chain[3], QStringLiteral("QObject"));
}

void TestMetaInspector::testNullObject() {
  // All methods should handle nullptr gracefully
  QVERIFY(MetaInspector::objectInfo(nullptr).isEmpty());
  QVERIFY(MetaInspector::listProperties(nullptr).isEmpty());
  QVERIFY(MetaInspector::listMethods(nullptr).isEmpty());
  QVERIFY(MetaInspector::listSignals(nullptr).isEmpty());
  QVERIFY(MetaInspector::inheritanceChain(nullptr).isEmpty());
}

// ============================================================================
// Property Get/Set tests (OBJ-06, OBJ-07)
// ============================================================================

void TestMetaInspector::testGetPropertyString() {
  QPushButton button(QStringLiteral("Hello Button"));
  QJsonValue result = MetaInspector::getProperty(&button, QStringLiteral("text"));
  QCOMPARE(result.toString(), QStringLiteral("Hello Button"));
}

void TestMetaInspector::testGetPropertyInt() {
  TestObject obj;
  obj.setIntValue(123);
  QJsonValue result = MetaInspector::getProperty(&obj, QStringLiteral("intValue"));
  QCOMPARE(result.toInt(), 123);
}

void TestMetaInspector::testGetPropertyNotFound() {
  TestObject obj;
  bool exceptionThrown = false;
  try {
    MetaInspector::getProperty(&obj, QStringLiteral("nonExistentProperty"));
  } catch (const std::runtime_error& e) {
    exceptionThrown = true;
    QString msg = QString::fromStdString(e.what());
    QVERIFY2(msg.contains(QStringLiteral("not found")), qPrintable(msg));
  }
  QVERIFY2(exceptionThrown, "Expected exception for nonexistent property");
}

void TestMetaInspector::testSetPropertyString() {
  QPushButton button;
  bool success = MetaInspector::setProperty(&button, QStringLiteral("text"),
                                            QJsonValue(QStringLiteral("New Text")));
  QVERIFY(success);
  QCOMPARE(button.text(), QStringLiteral("New Text"));
}

void TestMetaInspector::testSetPropertyInt() {
  TestObject obj;
  bool success = MetaInspector::setProperty(&obj, QStringLiteral("intValue"), QJsonValue(999));
  QVERIFY(success);
  QCOMPARE(obj.intValue(), 999);
}

void TestMetaInspector::testSetPropertyReadOnly() {
  TestObject obj;
  bool exceptionThrown = false;
  try {
    MetaInspector::setProperty(&obj, QStringLiteral("readOnly"), QJsonValue(false));
  } catch (const std::runtime_error& e) {
    exceptionThrown = true;
    QString msg = QString::fromStdString(e.what());
    QVERIFY2(msg.contains(QStringLiteral("read-only")), qPrintable(msg));
  }
  QVERIFY2(exceptionThrown, "Expected exception for read-only property");
}

void TestMetaInspector::testSetPropertyTypeCoercion() {
  TestObject obj;
  // Set int property using a JSON double (should coerce)
  bool success = MetaInspector::setProperty(&obj, QStringLiteral("intValue"), QJsonValue(42.0));
  QVERIFY(success);
  QCOMPARE(obj.intValue(), 42);
}

void TestMetaInspector::testDynamicProperty() {
  QObject obj;

  // Set dynamic property
  bool success = MetaInspector::setProperty(&obj, QStringLiteral("dynamicProp"),
                                            QJsonValue(QStringLiteral("dynamic value")));
  QVERIFY(success);

  // Get dynamic property
  QJsonValue result = MetaInspector::getProperty(&obj, QStringLiteral("dynamicProp"));
  QCOMPARE(result.toString(), QStringLiteral("dynamic value"));
}

// ============================================================================
// Method Invocation tests (OBJ-09)
// ============================================================================

void TestMetaInspector::testInvokeVoidMethod() {
  TestObject obj;
  // doSomething() is a void slot
  QJsonValue result = MetaInspector::invokeMethod(&obj, QStringLiteral("doSomething"));
  QVERIFY(result.isNull());  // void methods return null
}

void TestMetaInspector::testInvokeMethodWithArgs() {
  TestObject obj;
  QJsonArray args;
  args.append(10);
  args.append(32);

  QJsonValue result = MetaInspector::invokeMethod(&obj, QStringLiteral("addNumbers"), args);
  QCOMPARE(result.toInt(), 42);
}

void TestMetaInspector::testInvokeMethodWithReturnValue() {
  TestObject obj;
  // addNumbers returns int
  QJsonArray args;
  args.append(5);
  args.append(7);
  QJsonValue result = MetaInspector::invokeMethod(&obj, QStringLiteral("addNumbers"), args);
  QCOMPARE(result.toInt(), 12);
}

void TestMetaInspector::testInvokeMethodNotFound() {
  TestObject obj;
  bool exceptionThrown = false;
  try {
    MetaInspector::invokeMethod(&obj, QStringLiteral("nonExistentMethod"));
  } catch (const std::runtime_error& e) {
    exceptionThrown = true;
    QString msg = QString::fromStdString(e.what());
    QVERIFY2(msg.contains(QStringLiteral("not found")), qPrintable(msg));
  }
  QVERIFY2(exceptionThrown, "Expected exception for nonexistent method");
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
    QVERIFY2(
        msg.contains(QStringLiteral("not found")) || msg.contains(QStringLiteral("wrong argument")),
        qPrintable(msg));
  }
  QVERIFY2(exceptionThrown, "Expected exception for wrong argument count");
}

QTEST_APPLESS_MAIN(TestMetaInspector)
#include "test_meta_inspector.moc"
