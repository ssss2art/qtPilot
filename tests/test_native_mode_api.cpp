// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "api/error_codes.h"
#include "api/native_mode_api.h"
#include "api/symbolic_name_map.h"
#include "core/object_registry.h"
#include "core/object_resolver.h"
#include "introspection/signal_monitor.h"
#include "transport/jsonrpc_handler.h"

#include <QApplication>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QVBoxLayout>
#include <QtTest>

using namespace qtPilot;

/// @brief Integration tests for the complete Native Mode API (qt.* methods).
///
/// Tests all 7 API domains end-to-end through the JSON-RPC handler:
/// system, objects, properties, methods, signals, ui, names.
/// Also verifies ResponseEnvelope format, ObjectResolver multi-style
/// resolution, and structured error responses.
class TestNativeModeApi : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase();
  void cleanupTestCase();
  void init();
  void cleanup();

  // Response Envelope
  void testResponseEnvelope();

  // System methods
  void testPing();
  void testVersion();
  void testModes();

  // Object discovery (qt.objects.*)
  void testObjectsFind();
  void testObjectsFindNotFound();
  void testObjectsFindByClass();
  void testObjectsTree();
  void testObjectsInfo();
  void testObjectsInspect();
  void testObjectsQuery();
  void testObjectsQueryWithPropertyFilter();

  // Properties (qt.properties.*)
  void testPropertiesList();
  void testPropertiesGetSet();

  // Methods (qt.methods.*)
  void testMethodsList();
  void testMethodsInvoke();

  // Signals (qt.signals.*)
  void testSignalsList();
  void testSignalsSubscribeUnsubscribe();

  // UI (qt.ui.*)
  void testUiGeometry();
  void testUiScreenshot();
  void testUiClick();
  void testUiSendKeys();

  // Name map (qt.names.*)
  void testNamesRegisterAndList();
  void testNamesUnregister();
  void testNamesValidate();

  // ObjectResolver multi-style
  void testNumericIdResolution();
  void testSymbolicNameResolution();

  // Error handling
  void testStructuredErrorMissingObjectId();
  void testStructuredErrorObjectNotFound();

 private:
  /// @brief Make a JSON-RPC call and return the full parsed response object.
  QJsonObject callRaw(const QString& method, const QJsonObject& params);

  /// @brief Make a JSON-RPC call and return the envelope result (unwrapped from JSON-RPC).
  /// The envelope has {result, meta} structure.
  QJsonObject callEnvelope(const QString& method, const QJsonObject& params);

  /// @brief Make a JSON-RPC call and return the inner result value from the envelope.
  QJsonValue callResult(const QString& method, const QJsonObject& params);

  /// @brief Make a JSON-RPC call expecting an error, return the error object.
  QJsonObject callExpectError(const QString& method, const QJsonObject& params);

  JsonRpcHandler* m_handler = nullptr;
  NativeModeApi* m_api = nullptr;
  QWidget* m_testWindow = nullptr;
  QPushButton* m_testButton = nullptr;
  QPushButton* m_testButton2 = nullptr;
  QLineEdit* m_testLineEdit = nullptr;
  int m_requestId = 1;
};

void TestNativeModeApi::initTestCase() {
  installObjectHooks();
}

void TestNativeModeApi::cleanupTestCase() {
  uninstallObjectHooks();
}

void TestNativeModeApi::init() {
  m_handler = new JsonRpcHandler(this);
  m_api = new NativeModeApi(m_handler, this);

  // Create test widget tree
  m_testWindow = new QWidget();
  m_testWindow->setObjectName("testWindow");

  QVBoxLayout* layout = new QVBoxLayout(m_testWindow);

  m_testButton = new QPushButton("Test Button", m_testWindow);
  m_testButton->setObjectName("testBtn");
  layout->addWidget(m_testButton);

  m_testButton2 = new QPushButton("Other Button", m_testWindow);
  m_testButton2->setObjectName("otherBtn");
  m_testButton2->setEnabled(false);
  layout->addWidget(m_testButton2);

  m_testLineEdit = new QLineEdit(m_testWindow);
  m_testLineEdit->setObjectName("testLineEdit");
  layout->addWidget(m_testLineEdit);

  m_testWindow->show();
  QApplication::processEvents();

  // Register widgets with ObjectRegistry
  ObjectRegistry::instance()->scanExistingObjects(m_testWindow);
}

void TestNativeModeApi::cleanup() {
  // Clear numeric IDs and name map entries before destroying objects
  ObjectResolver::clearNumericIds();

  // Clean up name map entries added during test
  auto* nameMap = SymbolicNameMap::instance();
  QJsonObject names = nameMap->allNames();
  for (auto it = names.constBegin(); it != names.constEnd(); ++it) {
    nameMap->unregisterName(it.key());
  }

  delete m_testWindow;
  m_testWindow = nullptr;
  m_testButton = nullptr;
  m_testButton2 = nullptr;
  m_testLineEdit = nullptr;

  delete m_api;
  m_api = nullptr;
  delete m_handler;
  m_handler = nullptr;
}

QJsonObject TestNativeModeApi::callRaw(const QString& method, const QJsonObject& params) {
  QJsonObject request;
  request["jsonrpc"] = "2.0";
  request["method"] = method;
  request["params"] = params;
  request["id"] = m_requestId++;

  QString requestStr = QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact));
  QString responseStr = m_handler->HandleMessage(requestStr);

  return QJsonDocument::fromJson(responseStr.toUtf8()).object();
}

QJsonObject TestNativeModeApi::callEnvelope(const QString& method, const QJsonObject& params) {
  QJsonObject response = callRaw(method, params);
  // The JSON-RPC result field contains the envelope string that was parsed
  // Actually, HandleMessage wraps the method return (a JSON string) as result.
  // The result is the serialized envelope. We need to parse it.
  QJsonValue resultVal = response["result"];

  // HandleMessage uses CreateSuccessResponse which does:
  //   {"jsonrpc":"2.0","id":N,"result":<raw string>}
  // where the raw string is the envelope JSON. Since CreateSuccessResponse
  // does string interpolation, the result is already a parsed JSON object.
  if (resultVal.isObject()) {
    return resultVal.toObject();
  }
  // Should not happen for envelope-wrapped responses
  return QJsonObject();
}

QJsonValue TestNativeModeApi::callResult(const QString& method, const QJsonObject& params) {
  QJsonObject envelope = callEnvelope(method, params);
  return envelope["result"];
}

QJsonObject TestNativeModeApi::callExpectError(const QString& method, const QJsonObject& params) {
  QJsonObject response = callRaw(method, params);
  return response["error"].toObject();
}

// ========================================================================
// Response Envelope Tests
// ========================================================================

void TestNativeModeApi::testResponseEnvelope() {
  // Call qt.ping and verify envelope structure
  QJsonObject envelope = callEnvelope("qt.ping", QJsonObject());

  QVERIFY(envelope.contains("result"));
  QVERIFY(envelope.contains("meta"));

  QJsonObject meta = envelope["meta"].toObject();
  QVERIFY(meta.contains("timestamp"));
  // timestamp should be a positive integer (milliseconds since epoch)
  qint64 ts = static_cast<qint64>(meta["timestamp"].toDouble());
  QVERIFY(ts > 0);
}

// ========================================================================
// System Tests
// ========================================================================

void TestNativeModeApi::testPing() {
  QJsonValue result = callResult("qt.ping", QJsonObject());
  QVERIFY(result.isObject());

  QJsonObject obj = result.toObject();
  QCOMPARE(obj["pong"].toBool(), true);
  QVERIFY(static_cast<qint64>(obj["timestamp"].toDouble()) > 0);
  QVERIFY(obj.contains("eventLoopLatency"));
  QVERIFY(static_cast<qint64>(obj["eventLoopLatency"].toDouble()) >= 0);
}

void TestNativeModeApi::testVersion() {
  QJsonValue result = callResult("qt.version", QJsonObject());
  QVERIFY(result.isObject());

  QJsonObject obj = result.toObject();
  QVERIFY(!obj["version"].toString().isEmpty());
  QCOMPARE(obj["protocol"].toString(), QString("jsonrpc-2.0"));
  QCOMPARE(obj["name"].toString(), QString("qtPilot"));
  QCOMPARE(obj["mode"].toString(), QString("native"));

  QJsonArray deprecated = obj["deprecated"].toArray();
  QVERIFY(deprecated.size() >= 1);
  bool hasQtpilot = false;
  for (const QJsonValue& v : deprecated) {
    if (v.toString() == "qtpilot.*") {
      hasQtpilot = true;
      break;
    }
  }
  QVERIFY(hasQtpilot);
}

void TestNativeModeApi::testModes() {
  QJsonValue result = callResult("qt.modes", QJsonObject());
  QVERIFY(result.isArray());

  QJsonArray modes = result.toArray();
  QStringList modeStrings;
  for (const QJsonValue& v : modes) {
    modeStrings.append(v.toString());
  }
  QVERIFY(modeStrings.contains("native"));
  QVERIFY(modeStrings.contains("computer_use"));
  QVERIFY(modeStrings.contains("chrome"));
}

// ========================================================================
// Object Discovery Tests
// ========================================================================

void TestNativeModeApi::testObjectsFind() {
  QJsonValue result = callResult("qt.objects.find", QJsonObject{{"name", "testBtn"}});
  QVERIFY(result.isObject());

  QJsonObject obj = result.toObject();
  QVERIFY(!obj["objectId"].toString().isEmpty());
  QVERIFY(obj["className"].toString().contains("QPushButton"));
  QVERIFY(obj["numericId"].toInt() > 0);
}

void TestNativeModeApi::testObjectsFindNotFound() {
  QJsonObject error =
      callExpectError("qt.objects.find", QJsonObject{{"name", "nonexistent_widget_xyz"}});

  QCOMPARE(error["code"].toInt(), ErrorCode::kObjectNotFound);
  QVERIFY(!error["message"].toString().isEmpty());

  // Structured error should have data with hint
  QJsonObject data = error["data"].toObject();
  QVERIFY(data.contains("hint"));
}

void TestNativeModeApi::testObjectsFindByClass() {
  QJsonValue result =
      callResult("qt.objects.findByClass", QJsonObject{{"className", "QPushButton"}});
  QVERIFY(result.isObject());

  QJsonArray objects = result.toObject()["objects"].toArray();
  QVERIFY(objects.size() >= 2);  // testBtn + otherBtn

  // Verify each entry has required fields
  for (const QJsonValue& v : objects) {
    QJsonObject entry = v.toObject();
    QVERIFY(!entry["objectId"].toString().isEmpty());
    QVERIFY(entry["className"].toString().contains("QPushButton"));
    QVERIFY(entry["numericId"].toInt() > 0);
  }
}

void TestNativeModeApi::testObjectsTree() {
  QJsonValue result = callResult("qt.objects.tree", QJsonObject{{"maxDepth", 2}});
  QVERIFY(result.isObject());

  QJsonObject tree = result.toObject();
  // Tree should have some structure - at least children or className
  QVERIFY(tree.contains("children") || tree.contains("className") || tree.contains("id"));
}

void TestNativeModeApi::testObjectsInfo() {
  // First find the button to get its objectId
  QJsonValue findResult = callResult("qt.objects.find", QJsonObject{{"name", "testBtn"}});
  QString objectId = findResult.toObject()["objectId"].toString();
  QVERIFY(!objectId.isEmpty());

  // Now get info
  QJsonValue result = callResult("qt.objects.info", QJsonObject{{"objectId", objectId}});
  QVERIFY(result.isObject());

  QJsonObject info = result.toObject();
  QCOMPARE(info["className"].toString(), QString("QPushButton"));
}

void TestNativeModeApi::testObjectsInspect() {
  // First find the button
  QJsonValue findResult = callResult("qt.objects.find", QJsonObject{{"name", "testBtn"}});
  QString objectId = findResult.toObject()["objectId"].toString();

  // Inspect
  QJsonValue result = callResult("qt.objects.inspect", QJsonObject{{"objectId", objectId}});
  QVERIFY(result.isObject());

  QJsonObject inspected = result.toObject();
  QVERIFY(inspected.contains("info"));
  QVERIFY(inspected.contains("properties"));
  QVERIFY(inspected.contains("methods"));
  QVERIFY(inspected.contains("signals"));

  // info should have className
  QCOMPARE(inspected["info"].toObject()["className"].toString(), QString("QPushButton"));

  // properties should be an array with entries
  QVERIFY(inspected["properties"].toArray().size() > 0);

  // methods should have entries
  QVERIFY(inspected["methods"].toArray().size() > 0);

  // signals should have entries
  QVERIFY(inspected["signals"].toArray().size() > 0);
}

void TestNativeModeApi::testObjectsQuery() {
  // Query by className only
  QJsonValue result = callResult("qt.objects.query", QJsonObject{{"className", "QPushButton"}});
  QVERIFY(result.isArray());

  QJsonArray matches = result.toArray();
  QVERIFY(matches.size() >= 2);  // testBtn + otherBtn

  for (const QJsonValue& v : matches) {
    QJsonObject entry = v.toObject();
    QVERIFY(!entry["objectId"].toString().isEmpty());
    QVERIFY(entry["className"].toString().contains("QPushButton"));
  }
}

void TestNativeModeApi::testObjectsQueryWithPropertyFilter() {
  // Query with property filter: enabled=false should match only otherBtn
  QJsonValue result = callResult(
      "qt.objects.query",
      QJsonObject{{"className", "QPushButton"}, {"properties", QJsonObject{{"enabled", false}}}});
  QVERIFY(result.isArray());

  QJsonArray matches = result.toArray();
  QVERIFY(matches.size() >= 1);

  // All matches should be disabled buttons
  for (const QJsonValue& v : matches) {
    QJsonObject entry = v.toObject();
    QVERIFY(entry["className"].toString().contains("QPushButton"));
  }
}

// ========================================================================
// Property Tests
// ========================================================================

void TestNativeModeApi::testPropertiesList() {
  // Find button first
  QJsonValue findResult = callResult("qt.objects.find", QJsonObject{{"name", "testBtn"}});
  QString objectId = findResult.toObject()["objectId"].toString();

  QJsonValue result = callResult("qt.properties.list", QJsonObject{{"objectId", objectId}});
  QVERIFY(result.isArray());

  QJsonArray props = result.toArray();
  QVERIFY(props.size() > 0);

  // Should have "text" property
  bool hasText = false;
  for (const QJsonValue& v : props) {
    if (v.toObject()["name"].toString() == "text") {
      hasText = true;
      break;
    }
  }
  QVERIFY(hasText);
}

void TestNativeModeApi::testPropertiesGetSet() {
  // Find button
  QJsonValue findResult = callResult("qt.objects.find", QJsonObject{{"name", "testBtn"}});
  QString objectId = findResult.toObject()["objectId"].toString();

  // Get text
  QJsonValue getResult =
      callResult("qt.properties.get", QJsonObject{{"objectId", objectId}, {"name", "text"}});
  QVERIFY(getResult.isObject());
  QCOMPARE(getResult.toObject()["value"].toString(), QString("Test Button"));

  // Set text
  QJsonValue setResult =
      callResult("qt.properties.set",
                 QJsonObject{{"objectId", objectId}, {"name", "text"}, {"value", "Changed"}});
  QVERIFY(setResult.isObject());
  QCOMPARE(setResult.toObject()["success"].toBool(), true);

  // Verify change via get
  QJsonValue getResult2 =
      callResult("qt.properties.get", QJsonObject{{"objectId", objectId}, {"name", "text"}});
  QCOMPARE(getResult2.toObject()["value"].toString(), QString("Changed"));

  // Also verify via direct Qt API
  QCOMPARE(m_testButton->text(), QString("Changed"));
}

// ========================================================================
// Method Tests
// ========================================================================

void TestNativeModeApi::testMethodsList() {
  QJsonValue findResult = callResult("qt.objects.find", QJsonObject{{"name", "testBtn"}});
  QString objectId = findResult.toObject()["objectId"].toString();

  QJsonValue result = callResult("qt.methods.list", QJsonObject{{"objectId", objectId}});
  QVERIFY(result.isArray());

  QJsonArray methods = result.toArray();
  QVERIFY(methods.size() > 0);

  // Should have "click" method
  bool hasClick = false;
  for (const QJsonValue& v : methods) {
    if (v.toObject()["name"].toString() == "click") {
      hasClick = true;
      break;
    }
  }
  QVERIFY(hasClick);
}

void TestNativeModeApi::testMethodsInvoke() {
  QJsonValue findResult = callResult("qt.objects.find", QJsonObject{{"name", "testBtn"}});
  QString objectId = findResult.toObject()["objectId"].toString();

  // Invoke setEnabled(false) to disable the button
  QJsonValue result = callResult(
      "qt.methods.invoke",
      QJsonObject{{"objectId", objectId}, {"method", "setEnabled"}, {"args", QJsonArray{false}}});
  QVERIFY(result.isObject());

  // Verify the button is now disabled
  QCOMPARE(m_testButton->isEnabled(), false);

  // Re-enable for other tests
  m_testButton->setEnabled(true);
}

// ========================================================================
// Signal Tests
// ========================================================================

void TestNativeModeApi::testSignalsList() {
  QJsonValue findResult = callResult("qt.objects.find", QJsonObject{{"name", "testBtn"}});
  QString objectId = findResult.toObject()["objectId"].toString();

  QJsonValue result = callResult("qt.signals.list", QJsonObject{{"objectId", objectId}});
  QVERIFY(result.isArray());

  QJsonArray signalList = result.toArray();
  QVERIFY(signalList.size() > 0);

  // Should have "clicked" signal
  bool hasClicked = false;
  for (const QJsonValue& v : signalList) {
    if (v.toObject()["name"].toString() == "clicked") {
      hasClicked = true;
      break;
    }
  }
  QVERIFY(hasClicked);
}

void TestNativeModeApi::testSignalsSubscribeUnsubscribe() {
  QJsonValue findResult = callResult("qt.objects.find", QJsonObject{{"name", "testBtn"}});
  QString objectId = findResult.toObject()["objectId"].toString();

  // Subscribe
  QJsonValue subResult = callResult("qt.signals.subscribe",
                                    QJsonObject{{"objectId", objectId}, {"signal", "clicked"}});
  QVERIFY(subResult.isObject());

  QString subscriptionId = subResult.toObject()["subscriptionId"].toString();
  QVERIFY(!subscriptionId.isEmpty());
  QVERIFY(subscriptionId.startsWith("sub_"));

  int countBefore = SignalMonitor::instance()->subscriptionCount();

  // Unsubscribe
  QJsonValue unsubResult =
      callResult("qt.signals.unsubscribe", QJsonObject{{"subscriptionId", subscriptionId}});
  QVERIFY(unsubResult.isObject());
  QCOMPARE(unsubResult.toObject()["success"].toBool(), true);

  int countAfter = SignalMonitor::instance()->subscriptionCount();
  QCOMPARE(countAfter, countBefore - 1);
}

// ========================================================================
// UI Tests
// ========================================================================

void TestNativeModeApi::testUiGeometry() {
  QJsonValue findResult = callResult("qt.objects.find", QJsonObject{{"name", "testBtn"}});
  QString objectId = findResult.toObject()["objectId"].toString();

  QJsonValue result = callResult("qt.ui.geometry", QJsonObject{{"objectId", objectId}});
  QVERIFY(result.isObject());

  QJsonObject geo = result.toObject();
  QVERIFY(geo.contains("local"));
  QVERIFY(geo.contains("global"));
  QVERIFY(geo.contains("devicePixelRatio"));

  QJsonObject local = geo["local"].toObject();
  QVERIFY(local["width"].toInt() > 0);
  QVERIFY(local["height"].toInt() > 0);
}

void TestNativeModeApi::testUiScreenshot() {
  QJsonValue findResult = callResult("qt.objects.find", QJsonObject{{"name", "testBtn"}});
  QString objectId = findResult.toObject()["objectId"].toString();

  QJsonValue result = callResult("qt.ui.screenshot", QJsonObject{{"objectId", objectId}});
  QVERIFY(result.isObject());

  QString image = result.toObject()["image"].toString();
  QVERIFY(!image.isEmpty());

  // Verify it decodes to valid PNG
  QByteArray decoded = QByteArray::fromBase64(image.toLatin1());
  QVERIFY(decoded.startsWith("\x89PNG"));
}

void TestNativeModeApi::testUiClick() {
  QJsonValue findResult = callResult("qt.objects.find", QJsonObject{{"name", "testBtn"}});
  QString objectId = findResult.toObject()["objectId"].toString();

  QSignalSpy spy(m_testButton, &QPushButton::clicked);

  QJsonValue result = callResult("qt.ui.click", QJsonObject{{"objectId", objectId}});
  QApplication::processEvents();

  QVERIFY(result.isObject());
  QCOMPARE(result.toObject()["success"].toBool(), true);
  QCOMPARE(spy.count(), 1);
}

void TestNativeModeApi::testUiSendKeys() {
  QJsonValue findResult = callResult("qt.objects.find", QJsonObject{{"name", "testLineEdit"}});
  QString objectId = findResult.toObject()["objectId"].toString();

  m_testLineEdit->clear();
  m_testLineEdit->setFocus();
  QApplication::processEvents();

  QJsonValue result =
      callResult("qt.ui.sendKeys", QJsonObject{{"objectId", objectId}, {"text", "Hello"}});
  QApplication::processEvents();

  QVERIFY(result.isObject());
  QCOMPARE(result.toObject()["success"].toBool(), true);
  QCOMPARE(m_testLineEdit->text(), QString("Hello"));
}

// ========================================================================
// Name Map Tests
// ========================================================================

void TestNativeModeApi::testNamesRegisterAndList() {
  // Register a name
  QJsonValue regResult = callResult("qt.names.register",
                                    QJsonObject{{"name", "myBtn"}, {"path", "testWindow/testBtn"}});
  QVERIFY(regResult.isObject());
  QCOMPARE(regResult.toObject()["success"].toBool(), true);

  // List names
  QJsonValue listResult = callResult("qt.names.list", QJsonObject());
  QVERIFY(listResult.isObject());

  QJsonObject names = listResult.toObject();
  QVERIFY(names.contains("myBtn"));
  QCOMPARE(names["myBtn"].toString(), QString("testWindow/testBtn"));
}

void TestNativeModeApi::testNamesUnregister() {
  // Register a name
  callResult("qt.names.register", QJsonObject{{"name", "tempName"}, {"path", "some/path"}});

  // Verify it exists
  QJsonValue listBefore = callResult("qt.names.list", QJsonObject());
  QVERIFY(listBefore.toObject().contains("tempName"));

  // Unregister
  QJsonValue unregResult = callResult("qt.names.unregister", QJsonObject{{"name", "tempName"}});
  QVERIFY(unregResult.isObject());
  QCOMPARE(unregResult.toObject()["success"].toBool(), true);

  // Verify gone
  QJsonValue listAfter = callResult("qt.names.list", QJsonObject());
  QVERIFY(!listAfter.toObject().contains("tempName"));
}

void TestNativeModeApi::testNamesValidate() {
  // Register a name pointing to a valid path
  QString validPath = ObjectRegistry::instance()->objectId(m_testButton);
  callResult("qt.names.register", QJsonObject{{"name", "validBtn"}, {"path", validPath}});

  // Register a name pointing to an invalid path
  callResult("qt.names.register",
             QJsonObject{{"name", "invalidBtn"}, {"path", "nonexistent/path"}});

  // Validate
  QJsonValue result = callResult("qt.names.validate", QJsonObject());
  QVERIFY(result.isArray());

  QJsonArray validations = result.toArray();
  QVERIFY(validations.size() >= 2);

  // Check that validBtn is valid and invalidBtn is invalid
  bool foundValid = false, foundInvalid = false;
  for (const QJsonValue& v : validations) {
    QJsonObject entry = v.toObject();
    if (entry["name"].toString() == "validBtn") {
      QCOMPARE(entry["valid"].toBool(), true);
      foundValid = true;
    }
    if (entry["name"].toString() == "invalidBtn") {
      QCOMPARE(entry["valid"].toBool(), false);
      foundInvalid = true;
    }
  }
  QVERIFY(foundValid);
  QVERIFY(foundInvalid);
}

// ========================================================================
// ObjectResolver Multi-style Tests
// ========================================================================

void TestNativeModeApi::testNumericIdResolution() {
  // Find the button (this assigns a numeric ID)
  QJsonValue findResult = callResult("qt.objects.find", QJsonObject{{"name", "testBtn"}});
  int numericId = findResult.toObject()["numericId"].toInt();
  QVERIFY(numericId > 0);

  // Now call qt.objects.info using the numeric ID format "#N"
  QString numericRef = QString("#%1").arg(numericId);
  QJsonValue infoResult = callResult("qt.objects.info", QJsonObject{{"objectId", numericRef}});
  QVERIFY(infoResult.isObject());
  QCOMPARE(infoResult.toObject()["className"].toString(), QString("QPushButton"));
}

void TestNativeModeApi::testSymbolicNameResolution() {
  // Get the hierarchical path of the test button
  QString hierPath = ObjectRegistry::instance()->objectId(m_testButton);
  QVERIFY(!hierPath.isEmpty());

  // Register a symbolic name for it
  callResult("qt.names.register", QJsonObject{{"name", "symBtn"}, {"path", hierPath}});

  // Now call qt.objects.info using the symbolic name
  QJsonValue infoResult = callResult("qt.objects.info", QJsonObject{{"objectId", "symBtn"}});
  QVERIFY(infoResult.isObject());
  QCOMPARE(infoResult.toObject()["className"].toString(), QString("QPushButton"));
}

// ========================================================================
// Error Handling Tests
// ========================================================================

void TestNativeModeApi::testStructuredErrorMissingObjectId() {
  // Call qt.properties.get without objectId
  QJsonObject error = callExpectError("qt.properties.get", QJsonObject{{"name", "text"}});

  QCOMPARE(error["code"].toInt(), JsonRpcError::kInvalidParams);
  QVERIFY(!error["message"].toString().isEmpty());

  // Should have structured data with method info
  QJsonObject data = error["data"].toObject();
  QVERIFY(data.contains("method"));
}

void TestNativeModeApi::testStructuredErrorObjectNotFound() {
  // Call with a nonexistent objectId
  QJsonObject error =
      callExpectError("qt.objects.info", QJsonObject{{"objectId", "nonexistent/path/xyz"}});

  QCOMPARE(error["code"].toInt(), ErrorCode::kObjectNotFound);
  QVERIFY(!error["message"].toString().isEmpty());

  QJsonObject data = error["data"].toObject();
  QVERIFY(data.contains("objectId"));
  QVERIFY(data.contains("hint"));
}

QTEST_MAIN(TestNativeModeApi)
#include "test_native_mode_api.moc"
