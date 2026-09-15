// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "common/qt_matchers.h"
#include "core/object_registry.h"
#include "introspection/signal_monitor.h"
#include "transport/jsonrpc_handler.h"

#include <QApplication>
#include <QLineEdit>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPushButton>
#include <QSignalSpy>
#include <QVBoxLayout>
#include <QtTest>

using namespace qtPilot;
using namespace qtPilot::test;

namespace {

class MousePositionRecorder : public QObject {
 public:
  QPoint pressPosition;

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() == QEvent::MouseButtonPress) {
      pressPosition = static_cast<QMouseEvent*>(event)->pos();
    }
    return QObject::eventFilter(watched, event);
  }
};

}  // namespace

/// @brief Integration tests for JSON-RPC introspection API.
///
/// Tests the complete JSON-RPC API end-to-end by calling HandleMessage()
/// with properly formatted requests and verifying responses.
class TestJsonRpcIntrospection : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase();
  void cleanupTestCase();
  void init();
  void cleanup();

  // Object discovery
  void testFindByObjectName();
  void testFindByClassName();
  void testGetObjectTree();
  void testGetObjectInfo();

  // Properties
  void testListProperties();
  void testGetProperty();
  void testSetProperty();

  // Methods
  void testListMethods();
  void testInvokeMethod();
  void testListSignals();

  // Signals
  void testSubscribeSignal();
  void testUnsubscribeSignal();
  void testLifecycleNotifications();

  // UI Interaction
  void testClick();
  void testClickExplicitTopLeft();
  void testClickRejectsMalformedPosition();
  void testSendKeys();
  void testScreenshot();
  void testGetGeometry();
  void testHitTest();

 private:
  /// @brief Make a JSON-RPC call and return the result.
  QString callMethod(const QString& method, const QJsonObject& params);

  /// @brief Extract result from JSON-RPC response.
  QJsonValue getResult(const QString& response);

  /// @brief Extract error from JSON-RPC response.
  QJsonObject getError(const QString& response);

  JsonRpcHandler* m_handler = nullptr;
  QWidget* m_testWindow = nullptr;
  QPushButton* m_testButton = nullptr;
  QLineEdit* m_testLineEdit = nullptr;
  int m_requestId = 1;
};

void TestJsonRpcIntrospection::initTestCase() {
  // Install object hooks for registry to work
  installObjectHooks();
}

void TestJsonRpcIntrospection::cleanupTestCase() {
  uninstallObjectHooks();
}

void TestJsonRpcIntrospection::init() {
  m_handler = new JsonRpcHandler(this);

  // Create test widgets
  m_testWindow = new QWidget();
  m_testWindow->setObjectName("testWindow");

  QVBoxLayout* layout = new QVBoxLayout(m_testWindow);

  m_testButton = new QPushButton("Test Button", m_testWindow);
  m_testButton->setObjectName("testButton");
  layout->addWidget(m_testButton);

  m_testLineEdit = new QLineEdit(m_testWindow);
  m_testLineEdit->setObjectName("testLineEdit");
  layout->addWidget(m_testLineEdit);

  m_testWindow->show();
  QApplication::processEvents();

  // Register widgets with ObjectRegistry
  ObjectRegistry::instance()->scanExistingObjects(m_testWindow);
}

void TestJsonRpcIntrospection::cleanup() {
  delete m_testWindow;
  m_testWindow = nullptr;
  m_testButton = nullptr;
  m_testLineEdit = nullptr;

  delete m_handler;
  m_handler = nullptr;
}

QString TestJsonRpcIntrospection::callMethod(const QString& method, const QJsonObject& params) {
  QJsonObject request;
  request["jsonrpc"] = "2.0";
  request["method"] = method;
  request["params"] = params;
  request["id"] = m_requestId++;

  QString requestStr = QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact));
  return m_handler->HandleMessage(requestStr);
}

QJsonValue TestJsonRpcIntrospection::getResult(const QString& response) {
  QJsonDocument doc = QJsonDocument::fromJson(response.toUtf8());
  return doc.object()["result"];
}

QJsonObject TestJsonRpcIntrospection::getError(const QString& response) {
  QJsonDocument doc = QJsonDocument::fromJson(response.toUtf8());
  return doc.object()["error"].toObject();
}

// ========================================================================
// Object Discovery Tests
// ========================================================================

void TestJsonRpcIntrospection::testFindByObjectName() {
  // Note: Object IDs are computed at hook time (during construction)
  // before objectName is set. So we verify the API works by checking
  // that the returned ID can be used to find the object again.
  QString response = callMethod("qtpilot.findByObjectName", QJsonObject{{"name", "testButton"}});

  QJsonValue result = getResult(response);
  QEXPECT_THAT(result.isObject(), IsTrue());

  QJsonObject resObj = result.toObject();
  QEXPECT_THAT(resObj, HasJsonField("id"));

  QString id = resObj["id"].toString();
  QEXPECT_THAT(id, QIsNotEmpty());

  // Verify the ID can be used to look up the object
  QObject* found = ObjectRegistry::instance()->findById(id);
  QEXPECT_THAT(found, Eq(m_testButton));
}

void TestJsonRpcIntrospection::testFindByClassName() {
  QString response =
      callMethod("qtpilot.findByClassName", QJsonObject{{"className", "QPushButton"}});

  QJsonValue result = getResult(response);
  QEXPECT_THAT(result.isObject(), IsTrue());

  QJsonObject resObj = result.toObject();
  QEXPECT_THAT(resObj, HasJsonField("ids"));

  QJsonArray ids = resObj["ids"].toArray();
  QEXPECT_THAT(ids.size(), Ge(1));

  // Verify at least one of the returned IDs refers to our test button
  bool found = false;
  for (const QJsonValue& v : ids) {
    QObject* obj = ObjectRegistry::instance()->findById(v.toString());
    if (obj == m_testButton) {
      found = true;
      break;
    }
  }
  QEXPECT_THAT(found, IsTrue());
}

void TestJsonRpcIntrospection::testGetObjectTree() {
  QString id = ObjectRegistry::instance()->objectId(m_testWindow);

  QString response =
      callMethod("qtpilot.getObjectTree", QJsonObject{{"root", id}, {"maxDepth", 2}});

  QJsonValue result = getResult(response);
  QEXPECT_THAT(result.isObject(), IsTrue());

  QJsonObject tree = result.toObject();
  QEXPECT_THAT(tree, AnyOf(HasJsonField("id"), HasJsonField("children"), HasJsonField("className")));
}

void TestJsonRpcIntrospection::testGetObjectInfo() {
  QString id = ObjectRegistry::instance()->objectId(m_testButton);

  QString response = callMethod("qtpilot.getObjectInfo", QJsonObject{{"id", id}});

  QJsonValue result = getResult(response);
  QEXPECT_THAT(result.isObject(), IsTrue());

  QJsonObject info = result.toObject();
  QEXPECT_THAT(info, AllOf(
      HasJsonField("className", QStrEq("QPushButton")),
      HasJsonField("objectName", QStrEq("testButton"))));
}

// ========================================================================
// Property Tests
// ========================================================================

void TestJsonRpcIntrospection::testListProperties() {
  QString id = ObjectRegistry::instance()->objectId(m_testButton);

  QString response = callMethod("qtpilot.listProperties", QJsonObject{{"id", id}});

  QJsonValue result = getResult(response);
  QEXPECT_THAT(result.isArray(), IsTrue());

  QJsonArray props = result.toArray();
  QEXPECT_THAT(props, QIsNotEmpty());
  QEXPECT_THAT(props, JsonArrayContains(HasJsonField("name", QStrEq("text"))));
}

void TestJsonRpcIntrospection::testGetProperty() {
  QString id = ObjectRegistry::instance()->objectId(m_testButton);

  QString response = callMethod("qtpilot.getProperty", QJsonObject{{"id", id}, {"name", "text"}});

  QJsonValue result = getResult(response);
  QEXPECT_THAT(result.isObject(), IsTrue());
  QEXPECT_THAT(result.toObject(), HasJsonField("value", QStrEq("Test Button")));
}

void TestJsonRpcIntrospection::testSetProperty() {
  QString id = ObjectRegistry::instance()->objectId(m_testButton);

  QString response = callMethod("qtpilot.setProperty",
                                QJsonObject{{"id", id}, {"name", "text"}, {"value", "New Text"}});

  QJsonValue result = getResult(response);
  QEXPECT_THAT(result.isObject(), IsTrue());
  QEXPECT_THAT(result.toObject(), HasJsonField("success", Eq(true)));

  // Verify the property was actually set
  QEXPECT_THAT(m_testButton->text(), QStrEq("New Text"));
}

// ========================================================================
// Method Tests
// ========================================================================

void TestJsonRpcIntrospection::testListMethods() {
  QString id = ObjectRegistry::instance()->objectId(m_testButton);

  QString response = callMethod("qtpilot.listMethods", QJsonObject{{"id", id}});

  QJsonValue result = getResult(response);
  QEXPECT_THAT(result.isArray(), IsTrue());

  QJsonArray methods = result.toArray();
  QEXPECT_THAT(methods, QIsNotEmpty());
  QEXPECT_THAT(methods, JsonArrayContains(HasJsonField("name", QStrEq("click"))));
}

void TestJsonRpcIntrospection::testInvokeMethod() {
  QString id = ObjectRegistry::instance()->objectId(m_testButton);

  // Set up signal spy
  QSignalSpy spy(m_testButton, &QPushButton::clicked);

  QString response = callMethod(
      "qtpilot.invokeMethod", QJsonObject{{"id", id}, {"method", "click"}, {"args", QJsonArray()}});

  QApplication::processEvents();

  QJsonValue result = getResult(response);
  QEXPECT_THAT(result.isObject(), IsTrue());

  // The click slot should have fired the clicked signal
  QEXPECT_THAT(spy.count(), Eq(1));
}

void TestJsonRpcIntrospection::testListSignals() {
  QString id = ObjectRegistry::instance()->objectId(m_testButton);

  QString response = callMethod("qtpilot.listSignals", QJsonObject{{"id", id}});

  QJsonValue result = getResult(response);
  QEXPECT_THAT(result.isArray(), IsTrue());

  QJsonArray signalList = result.toArray();
  QEXPECT_THAT(signalList, QIsNotEmpty());
  QEXPECT_THAT(signalList, JsonArrayContains(HasJsonField("name", QStrEq("clicked"))));
}

// ========================================================================
// Signal Subscription Tests
// ========================================================================

void TestJsonRpcIntrospection::testSubscribeSignal() {
  QString id = ObjectRegistry::instance()->objectId(m_testButton);

  QString response =
      callMethod("qtpilot.subscribeSignal", QJsonObject{{"objectId", id}, {"signal", "clicked"}});

  QJsonValue result = getResult(response);
  QEXPECT_THAT(result.isObject(), IsTrue());

  QJsonObject resObj = result.toObject();
  QEXPECT_THAT(resObj, HasJsonField("subscriptionId", QStrStartsWith("sub_")));
  QString subId = resObj["subscriptionId"].toString();

  // Clean up
  SignalMonitor::instance()->unsubscribe(subId);
}

void TestJsonRpcIntrospection::testUnsubscribeSignal() {
  QString id = ObjectRegistry::instance()->objectId(m_testButton);

  // First subscribe
  QString subResponse =
      callMethod("qtpilot.subscribeSignal", QJsonObject{{"objectId", id}, {"signal", "clicked"}});
  QString subId = getResult(subResponse).toObject()["subscriptionId"].toString();

  int countBefore = SignalMonitor::instance()->subscriptionCount();

  // Then unsubscribe
  QString response =
      callMethod("qtpilot.unsubscribeSignal", QJsonObject{{"subscriptionId", subId}});

  QJsonValue result = getResult(response);
  QEXPECT_THAT(result.isObject(), IsTrue());
  QEXPECT_THAT(result.toObject(), HasJsonField("success", Eq(true)));

  int countAfter = SignalMonitor::instance()->subscriptionCount();
  QEXPECT_THAT(countAfter, Eq(countBefore - 1));
}

void TestJsonRpcIntrospection::testLifecycleNotifications() {
  // Enable lifecycle notifications
  QString response =
      callMethod("qtpilot.setLifecycleNotifications", QJsonObject{{"enabled", true}});

  QJsonValue result = getResult(response);
  QEXPECT_THAT(result.isObject(), IsTrue());
  QEXPECT_THAT(result.toObject(), HasJsonField("enabled", Eq(true)));
  QEXPECT_THAT(SignalMonitor::instance()->lifecycleNotificationsEnabled(), IsTrue());

  // Disable them
  response = callMethod("qtpilot.setLifecycleNotifications", QJsonObject{{"enabled", false}});

  result = getResult(response);
  QEXPECT_THAT(result.toObject(), HasJsonField("enabled", Eq(false)));
  QEXPECT_THAT(SignalMonitor::instance()->lifecycleNotificationsEnabled(), IsFalse());
}

// ========================================================================
// UI Interaction Tests
// ========================================================================

void TestJsonRpcIntrospection::testClick() {
  QString id = ObjectRegistry::instance()->objectId(m_testButton);
  QSignalSpy spy(m_testButton, &QPushButton::clicked);

  QString response = callMethod("qtpilot.click", QJsonObject{{"id", id}});

  QApplication::processEvents();

  QJsonValue result = getResult(response);
  QEXPECT_THAT(result.isObject(), IsTrue());
  QEXPECT_THAT(result.toObject(), HasJsonField("success", Eq(true)));

  QEXPECT_THAT(spy.count(), Eq(1));
}

void TestJsonRpcIntrospection::testClickExplicitTopLeft() {
  const QString id = ObjectRegistry::instance()->objectId(m_testButton);
  MousePositionRecorder recorder;
  m_testButton->installEventFilter(&recorder);

  const QString response = callMethod(
      "qtpilot.click", QJsonObject{{"id", id}, {"position", QJsonObject{{"x", 0}, {"y", 0}}}});

  QEXPECT_THAT(getError(response), QIsEmpty());
  QEXPECT_THAT(recorder.pressPosition, Eq(QPoint(0, 0)));
}

void TestJsonRpcIntrospection::testClickRejectsMalformedPosition() {
  const QString id = ObjectRegistry::instance()->objectId(m_testButton);
  const QString response =
      callMethod("qtpilot.click", QJsonObject{{"id", id}, {"position", QJsonObject{{"x", "bad"}}}});

  const QJsonObject error = getError(response);
  QEXPECT_THAT(error, AllOf(
      HasJsonField("code", Eq(static_cast<int>(JsonRpcError::kInvalidParams))),
      HasJsonField("message", QStrContains("numeric"))));
}

void TestJsonRpcIntrospection::testSendKeys() {
  QString id = ObjectRegistry::instance()->objectId(m_testLineEdit);
  m_testLineEdit->clear();
  m_testLineEdit->setFocus();
  QApplication::processEvents();

  QString response = callMethod("qtpilot.sendKeys", QJsonObject{{"id", id}, {"text", "Hello"}});

  QApplication::processEvents();

  QJsonValue result = getResult(response);
  QEXPECT_THAT(result.isObject(), IsTrue());
  QEXPECT_THAT(result.toObject(), HasJsonField("success", Eq(true)));

  QEXPECT_THAT(m_testLineEdit->text(), QStrEq("Hello"));
}

void TestJsonRpcIntrospection::testScreenshot() {
  QString id = ObjectRegistry::instance()->objectId(m_testButton);

  QString response = callMethod("qtpilot.screenshot", QJsonObject{{"id", id}});

  QJsonValue result = getResult(response);
  QEXPECT_THAT(result.isObject(), IsTrue());

  QJsonObject resObj = result.toObject();
  QEXPECT_THAT(resObj, HasJsonField("image"));

  QString base64 = resObj["image"].toString();
  QEXPECT_THAT(base64, QIsNotEmpty());

  // Verify it's valid base64 PNG
  QByteArray decoded = QByteArray::fromBase64(base64.toLatin1());
  QEXPECT_THAT(decoded.startsWith("\x89PNG"), IsTrue());
}

void TestJsonRpcIntrospection::testGetGeometry() {
  QString id = ObjectRegistry::instance()->objectId(m_testButton);

  QString response = callMethod("qtpilot.getGeometry", QJsonObject{{"id", id}});

  QJsonValue result = getResult(response);
  QEXPECT_THAT(result.isObject(), IsTrue());

  QJsonObject geo = result.toObject();
  QEXPECT_THAT(geo, AllOf(
      HasJsonField("local"),
      HasJsonField("global"),
      HasJsonField("devicePixelRatio")));

  QJsonObject local = geo["local"].toObject();
  QEXPECT_THAT(local["width"].toInt(), Gt(0));
  QEXPECT_THAT(local["height"].toInt(), Gt(0));
}

void TestJsonRpcIntrospection::testHitTest() {
  // Get button's global position
  QPoint globalPos = m_testButton->mapToGlobal(m_testButton->rect().center());

  QString response =
      callMethod("qtpilot.hitTest", QJsonObject{{"x", globalPos.x()}, {"y", globalPos.y()}});

  QJsonValue result = getResult(response);
  QEXPECT_THAT(result.isObject(), IsTrue());
  QEXPECT_THAT(result.toObject(), HasJsonField("id"));
}

QTEST_MAIN(TestJsonRpcIntrospection)
#include "test_jsonrpc_introspection.moc"
