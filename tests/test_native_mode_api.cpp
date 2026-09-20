// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "api/error_codes.h"
#include "api/native_mode_api.h"
#include "api/symbolic_name_map.h"
#include "common/qt_matchers.h"
#include "core/object_registry.h"
#include "core/object_resolver.h"
#include "introspection/signal_monitor.h"
#include "transport/jsonrpc_handler.h"

#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QVBoxLayout>
#include <QWidget>
#include <QtTest>

using namespace qtPilot;
using namespace qtPilot::test;

namespace {

class DoubleClickProbeWidget : public QWidget {
  Q_OBJECT
 public:
  using QWidget::QWidget;

 signals:
  void doubleClicked();

 protected:
  void mouseDoubleClickEvent(QMouseEvent* event) override {
    emit doubleClicked();
    QWidget::mouseDoubleClickEvent(event);
  }
};

}  // namespace

/// @brief Integration tests for the complete Native Mode API (qt.* methods).
///
/// Tests all 7 API domains end-to-end through the JSON-RPC handler:
/// system, objects, properties, methods, signals, ui, names.
/// Also verifies ResponseEnvelope format, ObjectResolver multi-style
/// resolution, and structured error responses.
class TestNativeModeApi : public QObject {
  Q_OBJECT

 private slots:
  void treeIncludesTopLevelWidgets();
  void searchFindsPreExistingTopLevelWidget();
  void initTestCase();
  void cleanupTestCase();
  void init();
  void cleanup();

  // Response Envelope
  void testResponseEnvelope();

  // System methods
  void testPing();
  void testVersion();

  // Object discovery (qt.objects.*)
  void testObjectsTree();
  void testObjectsInspect();
  void testInspectDefaultInfoOnly();
  void testInspectPropertiesPart();
  void testInspectAllAlias();
  void testInspectUnknownPartError();
  void testInspectModelPartNullForNonModel();
  void testInspectGeometryPartOnWidget();
  void testObjectsSearchByClassName();
  void testObjectsSearchByObjectName();
  void testObjectsSearchByProperties();
  void testObjectsSearchEmptyFilters();
  void testObjectsSearchByRootOnly();
  void testObjectsSearchNonExistentRootThrows();
  void testObjectsSearchByRootOnlySubtreeIsolation();
  void testObjectsSearchParamAliases();
  void testObjectsSearchLimitTruncation();

  // Properties (qt.properties.*)
  void testPropertiesGetSet();

  // Methods (qt.methods.*)
  void testMethodsInvoke();

  // Signals (qt.signals.*)
  void testSignalsSubscribeUnsubscribe();

  // UI (qt.ui.*)
  void testUiGeometry();
  void testUiScreenshot();
  void testUiClick();
  void testUiDoubleClick();
  void testUiSendKeys();

  // Name map (qt.names.*)
  void testNamesRegisterAndList();
  void testNamesUnregister();
  void testNamesValidate();

  // ObjectResolver multi-style
  void testNumericIdResolution();
  void testSymbolicNameResolution();
  void testResolveExpectedMonadic();
  void testSymbolicNameMapExpectedMonadic();

  // Error handling
  void testStructuredErrorMissingObjectId();
  void testStructuredErrorObjectNotFound();

  // QWidget lifecycle and modal regressions
  void testDynamicWidgetCreationAndDestructionSafety();
  void testModalDialogWidgetInteraction();

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

  QEXPECT_THAT(envelope, AllOf(HasJsonField("result"), HasJsonField("meta")));

  QJsonObject meta = envelope["meta"].toObject();
  QEXPECT_THAT(meta, HasJsonField("timestamp"));
  // timestamp should be a positive integer (milliseconds since epoch)
  qint64 ts = static_cast<qint64>(meta["timestamp"].toDouble());
  QEXPECT_THAT(ts, Gt(0));
}

// ========================================================================
// System Tests
// ========================================================================

void TestNativeModeApi::testPing() {
  QJsonValue result = callResult("qt.ping", QJsonObject());
  QEXPECT_THAT(result.isObject(), IsTrue());

  QJsonObject obj = result.toObject();
  QEXPECT_THAT(obj, AllOf(HasJsonField("pong", Eq(true)), HasJsonField("eventLoopLatency")));
  QEXPECT_THAT(static_cast<qint64>(obj["timestamp"].toDouble()), Gt(0));
  QEXPECT_THAT(static_cast<qint64>(obj["eventLoopLatency"].toDouble()), Ge(0));
}

void TestNativeModeApi::testVersion() {
  QJsonValue result = callResult("qt.version", QJsonObject());
  QEXPECT_THAT(result.isObject(), IsTrue());

  QJsonObject obj = result.toObject();
  QEXPECT_THAT(
      obj,
      AllOf(HasJsonField("version", QIsNotEmpty()), HasJsonField("protocol", QStrEq("jsonrpc-2.0")),
            HasJsonField("name", QStrEq("qtPilot")), HasJsonField("mode", QStrEq("native")),
            HasJsonField("deprecated", JsonArrayContains(QStrEq("qtpilot.*")))));
}

// ========================================================================
// Object Discovery Tests
// ========================================================================

void TestNativeModeApi::testObjectsTree() {
  QJsonValue result = callResult("qt.objects.tree", QJsonObject{{"maxDepth", 2}});
  QEXPECT_THAT(result.isObject(), IsTrue());

  QJsonObject tree = result.toObject();
  // Tree should have some structure - at least children or className
  QEXPECT_THAT(tree,
               AnyOf(HasJsonField("children"), HasJsonField("className"), HasJsonField("id")));
}

void TestNativeModeApi::testObjectsInspect() {
  // Get objectId directly from ObjectRegistry
  QString objectId = ObjectRegistry::instance()->objectId(m_testButton);
  QEXPECT_THAT(objectId, QIsNotEmpty());

  // Inspect
  QJsonValue result =
      callResult("qt.objects.inspect", QJsonObject{{"objectId", objectId}, {"parts", "all"}});
  QEXPECT_THAT(result.isObject(), IsTrue());

  QJsonObject inspected = result.toObject();
  QEXPECT_THAT(
      inspected,
      AllOf(HasJsonField("info", HasJsonField("className", QStrEq("QPushButton"))),
            HasJsonField("properties", QIsNotEmpty()), HasJsonField("methods", QIsNotEmpty()),
            HasJsonField("signals", QIsNotEmpty())));
}

void TestNativeModeApi::testInspectDefaultInfoOnly() {
  auto* w = new QLabel(QStringLiteral("inspTest"), m_testWindow);
  w->setObjectName(QStringLiteral("inspTarget"));
  ObjectRegistry::instance()->scanExistingObjects(m_testWindow);
  QString id = ObjectRegistry::instance()->objectId(w);

  QJsonObject params;
  params[QStringLiteral("objectId")] = id;
  QJsonValue result = callResult("qt.objects.inspect", params);

  QJsonObject obj = result.toObject();
  QEXPECT_THAT(obj, AllOf(HasJsonField("info"), DoesNotHaveJsonField("properties"),
                          DoesNotHaveJsonField("methods"), DoesNotHaveJsonField("signals"),
                          DoesNotHaveJsonField("qml"), DoesNotHaveJsonField("geometry"),
                          DoesNotHaveJsonField("model")));
}

void TestNativeModeApi::testInspectPropertiesPart() {
  auto* w = new QLabel(QStringLiteral("propTest"), m_testWindow);
  ObjectRegistry::instance()->scanExistingObjects(m_testWindow);
  QString id = ObjectRegistry::instance()->objectId(w);

  QJsonObject params;
  params[QStringLiteral("objectId")] = id;
  QJsonArray parts;
  parts.append(QStringLiteral("properties"));
  params[QStringLiteral("parts")] = parts;
  QJsonValue result = callResult("qt.objects.inspect", params);

  QJsonObject obj = result.toObject();
  QEXPECT_THAT(obj, AllOf(HasJsonField("properties", QIsNotEmpty()), DoesNotHaveJsonField("info")));
}

void TestNativeModeApi::testInspectAllAlias() {
  auto* w = new QLabel(QStringLiteral("allTest"), m_testWindow);
  ObjectRegistry::instance()->scanExistingObjects(m_testWindow);
  QString id = ObjectRegistry::instance()->objectId(w);

  QJsonObject params;
  params[QStringLiteral("objectId")] = id;
  params[QStringLiteral("parts")] = QStringLiteral("all");
  QJsonValue result = callResult("qt.objects.inspect", params);

  QJsonObject obj = result.toObject();
  QEXPECT_THAT(obj, AllOf(HasJsonField("info"), HasJsonField("properties"), HasJsonField("methods"),
                          HasJsonField("signals"), HasJsonField("qml"), HasJsonField("geometry"),
                          HasJsonField("model")));
}

void TestNativeModeApi::testInspectUnknownPartError() {
  auto* w = new QLabel(QStringLiteral("errTest"), m_testWindow);
  ObjectRegistry::instance()->scanExistingObjects(m_testWindow);
  QString id = ObjectRegistry::instance()->objectId(w);

  QJsonObject params;
  params[QStringLiteral("objectId")] = id;
  QJsonArray parts;
  parts.append(QStringLiteral("bogus"));
  params[QStringLiteral("parts")] = parts;
  QJsonObject error = callExpectError("qt.objects.inspect", params);
  QEXPECT_THAT(error, HasJsonField("code", Eq(static_cast<int>(ErrorCode::kInvalidField))));
}

void TestNativeModeApi::testInspectModelPartNullForNonModel() {
  auto* w = new QLabel(QStringLiteral("nullModelTest"), m_testWindow);
  ObjectRegistry::instance()->scanExistingObjects(m_testWindow);
  QString id = ObjectRegistry::instance()->objectId(w);

  QJsonObject params;
  params[QStringLiteral("objectId")] = id;
  QJsonArray parts;
  parts.append(QStringLiteral("model"));
  params[QStringLiteral("parts")] = parts;
  QJsonValue result = callResult("qt.objects.inspect", params);

  QJsonObject obj = result.toObject();
  QEXPECT_THAT(obj, HasJsonField("model"));
  QEXPECT_THAT(obj["model"].isNull(), IsTrue());
}

void TestNativeModeApi::testInspectGeometryPartOnWidget() {
  auto* w = new QLabel(QStringLiteral("geomTest"), m_testWindow);
  w->resize(100, 50);
  ObjectRegistry::instance()->scanExistingObjects(m_testWindow);
  QString id = ObjectRegistry::instance()->objectId(w);

  QJsonObject params;
  params[QStringLiteral("objectId")] = id;
  QJsonArray parts;
  parts.append(QStringLiteral("geometry"));
  params[QStringLiteral("parts")] = parts;
  QJsonValue result = callResult("qt.objects.inspect", params);

  QJsonObject geom = result.toObject()[QStringLiteral("geometry")].toObject();
  QEXPECT_THAT(geom, AllOf(HasJsonField("width", Eq(100)), HasJsonField("height", Eq(50)),
                           HasJsonField("visible")));
}

void TestNativeModeApi::testObjectsSearchByClassName() {
  auto* widget = new QPushButton(QStringLiteral("test"), m_testWindow);
  widget->setObjectName(QStringLiteral("searchTarget"));
  ObjectRegistry::instance()->scanExistingObjects(m_testWindow);

  QJsonObject params;
  params[QStringLiteral("className")] = QStringLiteral("QPushButton");
  QJsonValue result = callResult("qt.objects.search", params);

  QEXPECT_THAT(result.isObject(), IsTrue());
  QJsonObject obj = result.toObject();
  QEXPECT_THAT(obj,
               AllOf(HasJsonField("objects"), HasJsonField("count"), HasJsonField("truncated")));
  QJsonArray objects = obj[QStringLiteral("objects")].toArray();
  QEXPECT_THAT(objects.size(), Ge(1));

  QEXPECT_THAT(objects,
               JsonArrayContains(AllOf(HasJsonField("objectName", QStrEq("searchTarget")),
                                       HasJsonField("className", QStrEq("QPushButton")),
                                       HasJsonField("objectId"), HasJsonField("numericId"))));
}

void TestNativeModeApi::testObjectsSearchByObjectName() {
  auto* w = new QLabel(QStringLiteral("hello"), m_testWindow);
  w->setObjectName(QStringLiteral("uniqueLabel42"));
  ObjectRegistry::instance()->scanExistingObjects(m_testWindow);

  QJsonObject params;
  params[QStringLiteral("objectName")] = QStringLiteral("uniqueLabel42");
  QJsonValue result = callResult("qt.objects.search", params);

  QJsonArray objects = result.toObject()[QStringLiteral("objects")].toArray();
  QEXPECT_THAT(objects.size(), Eq(1));
  QEXPECT_THAT(objects[0].toObject(), HasJsonField("objectName", QStrEq("uniqueLabel42")));
}

void TestNativeModeApi::testObjectsSearchByProperties() {
  auto* w = new QPushButton(QStringLiteral("Go"), m_testWindow);
  w->setObjectName(QStringLiteral("propTestBtn"));
  w->setEnabled(false);
  ObjectRegistry::instance()->scanExistingObjects(m_testWindow);

  QJsonObject params;
  params[QStringLiteral("className")] = QStringLiteral("QPushButton");
  QJsonObject props;
  props[QStringLiteral("enabled")] = false;
  params[QStringLiteral("properties")] = props;
  QJsonValue result = callResult("qt.objects.search", params);

  QJsonArray objects = result.toObject()[QStringLiteral("objects")].toArray();
  QEXPECT_THAT(objects, JsonArrayContains(HasJsonField("objectName", QStrEq("propTestBtn"))));
}

void TestNativeModeApi::testObjectsSearchEmptyFilters() {
  QJsonObject params;
  QJsonValue result = callResult("qt.objects.search", params);
  QEXPECT_THAT(result.isObject(), IsTrue());
  QJsonObject obj = result.toObject();
  QEXPECT_THAT(obj,
               AllOf(HasJsonField("objects"), HasJsonField("count"), HasJsonField("truncated")));
  QEXPECT_THAT(obj[QStringLiteral("count")].toInt(), Gt(0));
}

void TestNativeModeApi::testObjectsSearchByRootOnly() {
  QString rootId = ObjectRegistry::instance()->objectId(m_testWindow);
  QJsonObject params;
  params[QStringLiteral("root")] = rootId;
  QJsonValue result = callResult("qt.objects.search", params);
  QEXPECT_THAT(result.isObject(), IsTrue());
  QJsonObject obj = result.toObject();
  QEXPECT_THAT(obj, HasJsonField("objects"));
  QJsonArray objects = obj[QStringLiteral("objects")].toArray();
  QEXPECT_THAT(objects.size(), Ge(1));

  QEXPECT_THAT(objects, JsonArrayContains(HasJsonField("objectName", QStrEq("testBtn"))));
}

void TestNativeModeApi::testObjectsSearchNonExistentRootThrows() {
  QJsonObject error = callExpectError(
      "qt.objects.search",
      QJsonObject{{QStringLiteral("root"), QStringLiteral("nonexistent/root/path")}});
  QEXPECT_THAT(
      error,
      AllOf(HasJsonField("code", Eq(static_cast<int>(ErrorCode::kObjectNotFound))),
            HasJsonField("message", QStrContains("Root object not found")),
            HasJsonField("data", AllOf(HasJsonField("method", QStrEq("qt.objects.search")),
                                       HasJsonField("root", QStrEq("nonexistent/root/path"))))));
}

void TestNativeModeApi::testObjectsSearchByRootOnlySubtreeIsolation() {
  // Scoping search to m_testButton should only find m_testButton itself and any descendants,
  // never sibling widgets like m_testLineEdit or m_testButton2.
  QString buttonId = ObjectRegistry::instance()->objectId(m_testButton);
  QJsonObject params;
  params[QStringLiteral("root")] = buttonId;
  QJsonValue result = callResult("qt.objects.search", params);
  QEXPECT_THAT(result.isObject(), IsTrue());

  QJsonArray objects = result.toObject()[QStringLiteral("objects")].toArray();
  QEXPECT_THAT(objects.size(), Ge(1));
  for (const auto& val : objects) {
    QString name = val.toObject()[QStringLiteral("objectName")].toString();
    QEXPECT_THAT(name, Ne(QStringLiteral("testLineEdit")));
    QEXPECT_THAT(name, Ne(QStringLiteral("otherBtn")));
  }
}

void TestNativeModeApi::testObjectsSearchParamAliases() {
  // test 'name' alias for 'objectName'
  QJsonObject params1;
  params1[QStringLiteral("name")] = QStringLiteral("testBtn");
  QJsonValue res1 = callResult("qt.objects.search", params1);
  QEXPECT_THAT(res1.toObject()[QStringLiteral("count")].toInt(), Ge(1));

  // test 'class_name' alias for 'className'
  QJsonObject params2;
  params2[QStringLiteral("class_name")] = QStringLiteral("QPushButton");
  QJsonValue res2 = callResult("qt.objects.search", params2);
  QEXPECT_THAT(res2.toObject()[QStringLiteral("count")].toInt(), Ge(1));

  // test 'rootId' alias for 'root'
  QString rootId = ObjectRegistry::instance()->objectId(m_testWindow);
  QJsonObject params3;
  params3[QStringLiteral("rootId")] = rootId;
  QJsonValue res3 = callResult("qt.objects.search", params3);
  QEXPECT_THAT(res3.toObject()[QStringLiteral("count")].toInt(), Ge(1));
}

void TestNativeModeApi::testObjectsSearchLimitTruncation() {
  auto* lbl = new QLabel(QStringLiteral("truncTest"), m_testWindow);
  lbl->setObjectName(QStringLiteral("truncLabel_0"));
  ObjectRegistry::instance()->scanExistingObjects(m_testWindow);

  QJsonObject params;
  params[QStringLiteral("objectName")] = QStringLiteral("truncLabel_0");
  params[QStringLiteral("limit")] = 0;
  QJsonValue result = callResult("qt.objects.search", params);
  QJsonObject obj = result.toObject();
  QEXPECT_THAT(obj, AllOf(HasJsonField("count", Eq(0)), HasJsonField("truncated", Eq(true))));
}

// ========================================================================
// Property Tests
// ========================================================================

void TestNativeModeApi::testPropertiesGetSet() {
  QString objectId = ObjectRegistry::instance()->objectId(m_testButton);

  // Get text
  QJsonValue getResult =
      callResult("qt.properties.get", QJsonObject{{"objectId", objectId}, {"name", "text"}});
  QEXPECT_THAT(getResult.isObject(), IsTrue());
  QEXPECT_THAT(getResult.toObject(), HasJsonField("value", QStrEq("Test Button")));

  // Set text
  QJsonValue setResult =
      callResult("qt.properties.set",
                 QJsonObject{{"objectId", objectId}, {"name", "text"}, {"value", "Changed"}});
  QEXPECT_THAT(setResult.isObject(), IsTrue());
  QEXPECT_THAT(setResult.toObject(), HasJsonField("ok", Eq(true)));

  // Verify change via get
  QJsonValue getResult2 =
      callResult("qt.properties.get", QJsonObject{{"objectId", objectId}, {"name", "text"}});
  QEXPECT_THAT(getResult2.toObject(), HasJsonField("value", QStrEq("Changed")));

  // Also verify via direct Qt API
  QEXPECT_THAT(m_testButton->text(), QStrEq("Changed"));
}

// ========================================================================
// Method Tests
// ========================================================================

void TestNativeModeApi::testMethodsInvoke() {
  QString objectId = ObjectRegistry::instance()->objectId(m_testButton);

  // Invoke setEnabled(false) to disable the button
  QJsonValue result = callResult(
      "qt.methods.invoke",
      QJsonObject{{"objectId", objectId}, {"method", "setEnabled"}, {"args", QJsonArray{false}}});
  QEXPECT_THAT(result.isObject(), IsTrue());

  // Verify the button is now disabled
  QEXPECT_THAT(m_testButton->isEnabled(), IsFalse());

  // Re-enable for other tests
  m_testButton->setEnabled(true);
}

// ========================================================================
// Signal Tests
// ========================================================================

void TestNativeModeApi::testSignalsSubscribeUnsubscribe() {
  QString objectId = ObjectRegistry::instance()->objectId(m_testButton);

  // Subscribe
  QJsonValue subResult = callResult("qt.signals.subscribe",
                                    QJsonObject{{"objectId", objectId}, {"signal", "clicked"}});
  QEXPECT_THAT(subResult.isObject(), IsTrue());

  QJsonObject subObj = subResult.toObject();
  QEXPECT_THAT(subObj, HasJsonField("subscriptionId", QStrStartsWith("sub_")));
  QString subscriptionId = subObj["subscriptionId"].toString();

  int countBefore = SignalMonitor::instance()->subscriptionCount();

  // Unsubscribe
  QJsonValue unsubResult =
      callResult("qt.signals.unsubscribe", QJsonObject{{"subscriptionId", subscriptionId}});
  QEXPECT_THAT(unsubResult.isObject(), IsTrue());
  QEXPECT_THAT(unsubResult.toObject(), HasJsonField("ok", Eq(true)));

  int countAfter = SignalMonitor::instance()->subscriptionCount();
  QEXPECT_THAT(countAfter, Eq(countBefore - 1));
}

// ========================================================================
// UI Tests
// ========================================================================

void TestNativeModeApi::testUiGeometry() {
  QString objectId = ObjectRegistry::instance()->objectId(m_testButton);

  QJsonValue result = callResult("qt.ui.geometry", QJsonObject{{"objectId", objectId}});
  QEXPECT_THAT(result.isObject(), IsTrue());

  QJsonObject geo = result.toObject();
  QEXPECT_THAT(
      geo, AllOf(HasJsonField("local"), HasJsonField("global"), HasJsonField("devicePixelRatio")));

  QJsonObject local = geo["local"].toObject();
  QEXPECT_THAT(local["width"].toInt(), Gt(0));
  QEXPECT_THAT(local["height"].toInt(), Gt(0));
}

void TestNativeModeApi::testUiScreenshot() {
  QString objectId = ObjectRegistry::instance()->objectId(m_testButton);

  QJsonValue result = callResult("qt.ui.screenshot", QJsonObject{{"objectId", objectId}});
  QEXPECT_THAT(result.isObject(), IsTrue());

  QJsonObject resObj = result.toObject();
  QEXPECT_THAT(resObj, HasJsonField("image"));

  QString image = resObj["image"].toString();
  QEXPECT_THAT(image, QIsNotEmpty());

  // Verify it decodes to valid PNG
  QByteArray decoded = QByteArray::fromBase64(image.toLatin1());
  QEXPECT_THAT(decoded.startsWith("\x89PNG"), IsTrue());
}

void TestNativeModeApi::testUiClick() {
  QString objectId = ObjectRegistry::instance()->objectId(m_testButton);

  QSignalSpy spy(m_testButton, &QPushButton::clicked);

  QJsonValue result = callResult("qt.ui.click", QJsonObject{{"objectId", objectId}});
  QApplication::processEvents();

  QEXPECT_THAT(result.isObject(), IsTrue());
  QEXPECT_THAT(result.toObject(), HasJsonField("ok", Eq(true)));
  QEXPECT_THAT(spy.count(), Eq(1));
}

void TestNativeModeApi::testUiDoubleClick() {
  auto* probe = new DoubleClickProbeWidget(m_testWindow);
  probe->setObjectName(QStringLiteral("doubleClickProbe"));
  probe->setFixedSize(80, 30);
  m_testWindow->layout()->addWidget(probe);
  QApplication::processEvents();
  ObjectRegistry::instance()->scanExistingObjects(probe);

  QString objectId = ObjectRegistry::instance()->objectId(probe);
  QSignalSpy spy(probe, &DoubleClickProbeWidget::doubleClicked);

  QJsonValue result = callResult("qt.ui.doubleClick", QJsonObject{{"objectId", objectId}});
  QApplication::processEvents();
  QApplication::processEvents();

  QEXPECT_THAT(result.isObject(), IsTrue());
  QEXPECT_THAT(result.toObject(), HasJsonField("ok", Eq(true)));
  QEXPECT_THAT(spy.size(), Eq(1));
}

void TestNativeModeApi::testUiSendKeys() {
  QString objectId = ObjectRegistry::instance()->objectId(m_testLineEdit);

  m_testLineEdit->clear();
  m_testLineEdit->setFocus();
  QApplication::processEvents();

  QJsonValue result =
      callResult("qt.ui.sendKeys", QJsonObject{{"objectId", objectId}, {"text", "Hello"}});
  QApplication::processEvents();

  QEXPECT_THAT(result.isObject(), IsTrue());
  QEXPECT_THAT(result.toObject(), HasJsonField("ok", Eq(true)));
  QEXPECT_THAT(m_testLineEdit->text(), QStrEq("Hello"));
}

// ========================================================================
// Name Map Tests
// ========================================================================

void TestNativeModeApi::testNamesRegisterAndList() {
  // Register a name
  QJsonValue regResult = callResult("qt.names.register",
                                    QJsonObject{{"name", "myBtn"}, {"path", "testWindow/testBtn"}});
  QEXPECT_THAT(regResult.isObject(), IsTrue());
  QEXPECT_THAT(regResult.toObject(), HasJsonField("ok", Eq(true)));

  // List names
  QJsonValue listResult = callResult("qt.names.list", QJsonObject());
  QEXPECT_THAT(listResult.isObject(), IsTrue());

  QJsonObject names = listResult.toObject();
  QEXPECT_THAT(names, HasJsonField("myBtn", QStrEq("testWindow/testBtn")));
}

void TestNativeModeApi::testNamesUnregister() {
  // Register a name
  callResult("qt.names.register", QJsonObject{{"name", "tempName"}, {"path", "some/path"}});

  // Verify it exists
  QJsonValue listBefore = callResult("qt.names.list", QJsonObject());
  QEXPECT_THAT(listBefore.toObject(), HasJsonField("tempName"));

  // Unregister
  QJsonValue unregResult = callResult("qt.names.unregister", QJsonObject{{"name", "tempName"}});
  QEXPECT_THAT(unregResult.isObject(), IsTrue());
  QEXPECT_THAT(unregResult.toObject(), HasJsonField("ok", Eq(true)));

  // Verify gone
  QJsonValue listAfter = callResult("qt.names.list", QJsonObject());
  QEXPECT_THAT(listAfter.toObject(), DoesNotHaveJsonField("tempName"));
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
  QEXPECT_THAT(result.isArray(), IsTrue());

  QJsonArray validations = result.toArray();
  QEXPECT_THAT(validations.size(), Ge(2));

  // Check that validBtn is valid and invalidBtn is invalid
  QEXPECT_THAT(validations,
               AllOf(JsonArrayContains(AllOf(HasJsonField("name", QStrEq("validBtn")),
                                             HasJsonField("valid", Eq(true)))),
                     JsonArrayContains(AllOf(HasJsonField("name", QStrEq("invalidBtn")),
                                             HasJsonField("valid", Eq(false))))));
}

// ========================================================================
// ObjectResolver Multi-style Tests
// ========================================================================

void TestNativeModeApi::testNumericIdResolution() {
  // Assign a numeric ID to the button via the resolver
  int numericId = ObjectResolver::assignNumericId(m_testButton);
  QEXPECT_THAT(numericId, Gt(0));

  // Now call qt.objects.inspect using the numeric ID format "#N"
  QString numericRef = QString("#%1").arg(numericId);
  QJsonValue infoResult = callResult("qt.objects.inspect", QJsonObject{{"objectId", numericRef}});
  QEXPECT_THAT(infoResult.isObject(), IsTrue());
  QJsonObject info = infoResult.toObject()["info"].toObject();
  QEXPECT_THAT(info, HasJsonField("className", QStrEq("QPushButton")));

  // Verify automatic cleanup on object destruction
  int tempId = -1;
  {
    auto* tempObj = new QObject();
    tempId = ObjectResolver::assignNumericId(tempObj);
    QEXPECT_THAT(tempId, Gt(0));
    QCOMPARE(ObjectResolver::findByNumericId(tempId), tempObj);
    delete tempObj;
  }
  // After destruction, lookup returns nullptr and id mapping is gone
  QCOMPARE(ObjectResolver::findByNumericId(tempId), nullptr);

  // Test findByNumericIdExpected
  int expId = ObjectResolver::assignNumericId(m_testButton);
  auto numExpRes = ObjectResolver::findByNumericIdExpected(expId);
  QVERIFY(numExpRes.has_value());
  QCOMPARE(*numExpRes, static_cast<QObject*>(m_testButton));

  auto numBadRes = ObjectResolver::findByNumericIdExpected(999999);
  QVERIFY(!numBadRes.has_value());
  QEXPECT_THAT(numBadRes.error(), QStrContains("not found"));

  auto numNegRes = ObjectResolver::findByNumericIdExpected(-5);
  QVERIFY(!numNegRes.has_value());
  QEXPECT_THAT(numNegRes.error(), QStrContains("positive"));
}

void TestNativeModeApi::testSymbolicNameResolution() {
  // Get the hierarchical path of the test button
  QString hierPath = ObjectRegistry::instance()->objectId(m_testButton);
  QEXPECT_THAT(hierPath, QIsNotEmpty());

  // Register a symbolic name for it
  callResult("qt.names.register", QJsonObject{{"name", "symBtn"}, {"path", hierPath}});

  // Now call qt.objects.inspect using the symbolic name
  QJsonValue infoResult = callResult("qt.objects.inspect", QJsonObject{{"objectId", "symBtn"}});
  QEXPECT_THAT(infoResult.isObject(), IsTrue());
  QJsonObject info = infoResult.toObject()["info"].toObject();
  QEXPECT_THAT(info, HasJsonField("className", QStrEq("QPushButton")));
}

void TestNativeModeApi::testResolveExpectedMonadic() {
  // 1. Successful resolution returns expected holding pointer
  QString hierPath = ObjectRegistry::instance()->objectId(m_testButton);
  auto res = ObjectResolver::resolveExpected(hierPath);
  QVERIFY(res.has_value());
  QCOMPARE(*res, static_cast<QObject*>(m_testButton));

  // 2. Monadic chaining via .and_then()
  auto widgetRes =
      res.and_then([](QObject* obj) -> std::expected<QWidget*, ObjectResolver::ResolveError> {
        if (auto* w = qobject_cast<QWidget*>(obj)) {
          return w;
        }
        return std::unexpected(ObjectResolver::ResolveError{
            ObjectResolver::ResolveErrorKind::NotFound, QString(), "Not a widget"});
      });
  QVERIFY(widgetRes.has_value());
  QCOMPARE(*widgetRes, static_cast<QWidget*>(m_testButton));

  // 3. Monadic transformation via .transform()
  auto classRes = widgetRes.transform(
      [](QWidget* w) { return QString::fromUtf8(w->metaObject()->className()); });
  QVERIFY(classRes.has_value());
  QCOMPARE(*classRes, QStringLiteral("QPushButton"));

  // 3b. Numeric resolution via resolveExpected
  int numId = ObjectResolver::assignNumericId(m_testButton);
  auto numRes = ObjectResolver::resolveExpected(QString::number(numId));
  QVERIFY(numRes.has_value());
  QCOMPARE(*numRes, static_cast<QObject*>(m_testButton));

  auto hashNumRes = ObjectResolver::resolveExpected(QStringLiteral("#%1").arg(numId));
  QVERIFY(hashNumRes.has_value());
  QCOMPARE(*hashNumRes, static_cast<QObject*>(m_testButton));

  // 3c. Symbolic resolution via resolveExpected
  SymbolicNameMap::instance()->registerName(QStringLiteral("myButton"), hierPath);
  auto symRes = ObjectResolver::resolveExpected(QStringLiteral("myButton"));
  QVERIFY(symRes.has_value());
  QCOMPARE(*symRes, static_cast<QObject*>(m_testButton));
  SymbolicNameMap::instance()->unregisterName(QStringLiteral("myButton"));

  // 4. Empty identifier produces EmptyId error
  auto emptyRes = ObjectResolver::resolveExpected("");
  QVERIFY(!emptyRes.has_value());
  QCOMPARE(static_cast<int>(emptyRes.error().kind),
           static_cast<int>(ObjectResolver::ResolveErrorKind::EmptyId));

  // 5. Nonexistent identifier produces NotFound error
  auto missingRes = ObjectResolver::resolveExpected("nonexistent_id_404");
  QVERIFY(!missingRes.has_value());
  QCOMPARE(static_cast<int>(missingRes.error().kind),
           static_cast<int>(ObjectResolver::ResolveErrorKind::NotFound));

  // 6. Monadic parameter extractors via JSON-RPC
  // Missing required "name" parameter produces kInvalidParams
  QJsonObject missingNameErr =
      callExpectError("qt.properties.get", QJsonObject{{"objectId", hierPath}});
  QCOMPARE(missingNameErr["code"].toInt(), static_cast<int>(JsonRpcError::kInvalidParams));

  // Missing required "value" parameter produces kInvalidParams
  QJsonObject missingValErr =
      callExpectError("qt.properties.set", QJsonObject{{"objectId", hierPath}, {"name", "text"}});
  QCOMPARE(missingValErr["code"].toInt(), static_cast<int>(JsonRpcError::kInvalidParams));

  // Missing required "method" parameter produces kInvalidParams
  QJsonObject missingMethodErr =
      callExpectError("qt.methods.invoke", QJsonObject{{"objectId", hierPath}});
  QCOMPARE(missingMethodErr["code"].toInt(), static_cast<int>(JsonRpcError::kInvalidParams));
}

void TestNativeModeApi::testSymbolicNameMapExpectedMonadic() {
  auto* nameMap = SymbolicNameMap::instance();
  QString hierPath = ObjectRegistry::instance()->objectId(m_testButton);

  // 1. resolveExpected
  nameMap->registerName(QStringLiteral("okBtn"), hierPath);
  auto foundRes = nameMap->resolveExpected(QStringLiteral("okBtn"));
  QVERIFY(foundRes.has_value());
  QCOMPARE(*foundRes, hierPath);

  auto missingRes = nameMap->resolveExpected(QStringLiteral("nonexistentSymbol"));
  QVERIFY(!missingRes.has_value());
  QEXPECT_THAT(missingRes.error(), QStrContains("not registered"));

  // 2. loadFromFileExpected error paths
  auto badLoad = nameMap->loadFromFileExpected(QStringLiteral("/path/does/not/exist/names.json"));
  QVERIFY(!badLoad.has_value());
  QEXPECT_THAT(badLoad.error(), QStrContains("Failed to open file"));

  // 3. saveToFileExpected error path
  auto badSave = nameMap->saveToFileExpected(QStringLiteral("/invalid/root/path/names.json"));
  QVERIFY(!badSave.has_value());
  QEXPECT_THAT(badSave.error(), QStrContains("Failed to write file"));

  // 4. saveToFileExpected and loadFromFileExpected round-trip
  QTemporaryFile tmpFile;
  QVERIFY(tmpFile.open());
  QString tmpPath = tmpFile.fileName();
  tmpFile.close();

  auto saveOk = nameMap->saveToFileExpected(tmpPath);
  QVERIFY(saveOk.has_value());

  nameMap->unregisterName(QStringLiteral("okBtn"));
  QVERIFY(!nameMap->resolveExpected(QStringLiteral("okBtn")).has_value());

  auto loadOk = nameMap->loadFromFileExpected(tmpPath);
  QVERIFY(loadOk.has_value());
  QVERIFY(nameMap->resolveExpected(QStringLiteral("okBtn")).has_value());

  // Cleanup
  nameMap->unregisterName(QStringLiteral("okBtn"));
}

// ========================================================================
// Error Handling Tests
// ========================================================================

void TestNativeModeApi::testStructuredErrorMissingObjectId() {
  // Call qt.properties.get without objectId
  QJsonObject error = callExpectError("qt.properties.get", QJsonObject{{"name", "text"}});

  QEXPECT_THAT(
      error,
      AllOf(HasJsonField("code", Eq(static_cast<int>(JsonRpcError::kInvalidParams))),
            HasJsonField("message", QIsNotEmpty()), HasJsonField("data", HasJsonField("method"))));
}

void TestNativeModeApi::testStructuredErrorObjectNotFound() {
  // Call with a nonexistent objectId
  QJsonObject error =
      callExpectError("qt.objects.inspect", QJsonObject{{"objectId", "nonexistent/path/xyz"}});

  QEXPECT_THAT(error,
               AllOf(HasJsonField("code", Eq(static_cast<int>(ErrorCode::kObjectNotFound))),
                     HasJsonField("message", QIsNotEmpty()),
                     HasJsonField("data", AllOf(HasJsonField("objectId"), HasJsonField("hint")))));
}

void TestNativeModeApi::testDynamicWidgetCreationAndDestructionSafety() {
  auto* dynBtn = new QPushButton("Dynamic Button", m_testWindow);
  dynBtn->setObjectName("dynamicBtn");
  m_testWindow->layout()->addWidget(dynBtn);
  QApplication::processEvents();

  // Find the dynamically created widget
  QJsonObject searchParams;
  searchParams["objectName"] = "dynamicBtn";
  QJsonArray results =
      callResult("qt.objects.search", searchParams).toObject()["objects"].toArray();
  QEXPECT_THAT(results.size(), Eq(1));
  QString objectId = results[0].toObject()["objectId"].toString();
  QEXPECT_THAT(objectId, QIsNotEmpty());

  // Verify properties can be read
  QJsonObject getParams;
  getParams["objectId"] = objectId;
  getParams["name"] = "text";
  QJsonObject getRes = callResult("qt.properties.get", getParams).toObject();
  QEXPECT_THAT(getRes["value"].toString(), Eq("Dynamic Button"));

  // Now dynamically destroy the widget
  delete dynBtn;
  QApplication::processEvents();

  // Calling methods on the destroyed widget must safely return kObjectNotFound (-32001)
  QJsonObject inspectError =
      callExpectError("qt.objects.inspect", QJsonObject{{"objectId", objectId}});
  QEXPECT_THAT(inspectError["code"].toInt(), Eq(static_cast<int>(ErrorCode::kObjectNotFound)));

  QJsonObject propError = callExpectError("qt.properties.get", getParams);
  QEXPECT_THAT(propError["code"].toInt(), Eq(static_cast<int>(ErrorCode::kObjectNotFound)));

  QJsonObject clickError = callExpectError("qt.ui.click", QJsonObject{{"objectId", objectId}});
  QEXPECT_THAT(clickError["code"].toInt(), Eq(static_cast<int>(ErrorCode::kObjectNotFound)));
}

void TestNativeModeApi::testModalDialogWidgetInteraction() {
  QDialog modalDialog(m_testWindow);
  modalDialog.setObjectName("modalTestDialog");
  modalDialog.setWindowTitle("Modal Title");
  modalDialog.setModal(true);

  QVBoxLayout* layout = new QVBoxLayout(&modalDialog);
  QPushButton* closeBtn = new QPushButton("Close Modal", &modalDialog);
  closeBtn->setObjectName("closeModalBtn");
  layout->addWidget(closeBtn);
  connect(closeBtn, &QPushButton::clicked, &modalDialog, &QDialog::accept);

  modalDialog.show();
  QApplication::processEvents();

  // Verify modal dialog is discoverable and interactive
  QJsonObject searchParams;
  searchParams["objectName"] = "closeModalBtn";
  QJsonArray results =
      callResult("qt.objects.search", searchParams).toObject()["objects"].toArray();
  QEXPECT_THAT(results.size(), Eq(1));
  QString btnId = results[0].toObject()["objectId"].toString();

  // Click the close button via qt.ui.click
  QJsonValue clickRes = callResult("qt.ui.click", QJsonObject{{"objectId", btnId}});
  QEXPECT_THAT(clickRes.isObject(), IsTrue());
  QEXPECT_THAT(clickRes.toObject(), HasJsonField("ok", Eq(true)));
  QApplication::processEvents();

  // Dialog should now be closed / not visible
  QEXPECT_THAT(modalDialog.isVisible(), IsFalse());
}

// A top-level QWidget is parentless, so it is NOT a QObject child of the
// application -- exactly like a top-level QWindow. The startup scan already
// treats windows as extra roots (Probe::initialize), but never walked
// QApplication::topLevelWidgets(), so anything built before the probe attached
// stayed invisible to qt.objects.search. That is the ordinary shape of a main
// window built in main(), and it reproduced on qtPilot's own test_app: neither
// MainWindow nor its actions could be found, while a QML scene in the same
// process was fully visible.
void TestNativeModeApi::searchFindsPreExistingTopLevelWidget() {
  uninstallObjectHooks();  // stand in for "created before the probe attached"

  QWidget preExisting;
  preExisting.setObjectName(QStringLiteral("preExistingTopLevelWidget"));
  auto* child = new QWidget(&preExisting);
  child->setObjectName(QStringLiteral("preExistingChildWidget"));

  ObjectRegistry::instance()->scanAllExistingObjects();

  QEXPECT_THAT(
      ObjectRegistry::instance()->findByObjectName(QStringLiteral("preExistingTopLevelWidget")),
      NotNull());
  QEXPECT_THAT(
      ObjectRegistry::instance()->findByObjectName(QStringLiteral("preExistingChildWidget")),
      NotNull());

  // Restore the hooks BEFORE these widgets leave scope. Without the remove hook
  // live, their destruction is never reported and the registry keeps dangling
  // pointers that crash a later test -- which is exactly what happened when this
  // test first ran inside the full suite rather than on its own.
  installObjectHooks();
}

// qt.objects.tree walks from a root set that includes the application and every
// visible top-level QWindow -- but not top-level QWidgets, which are parentless
// too and therefore just as unreachable from the application. The result is a
// tree that shows a handful of application-owned objects while the entire widget
// hierarchy is missing: on qtPilot's own test_app the tree reported 5 objects
// where the (registry-backed) search reported 178.
//
// This is the same blind spot fixed for search in #33, in the other traversal.
void TestNativeModeApi::treeIncludesTopLevelWidgets() {
  QWidget topLevel;
  topLevel.setObjectName(QStringLiteral("treeRootWidget"));
  auto* child = new QWidget(&topLevel);
  child->setObjectName(QStringLiteral("treeChildWidget"));
  topLevel.show();
  QEXPECT_THAT(QTest::qWaitForWindowExposed(&topLevel), IsTrue());

  const QJsonObject envelope =
      callEnvelope(QStringLiteral("qt.objects.tree"), QJsonObject{{"maxDepth", 6}});
  const QString dumped = QString::fromUtf8(QJsonDocument(envelope).toJson(QJsonDocument::Compact));

  QEXPECT_THAT(dumped, AllOf(QStrContains("treeRootWidget"), QStrContains("treeChildWidget")));
}

QTEST_MAIN(TestNativeModeApi)
#include "test_native_mode_api.moc"
