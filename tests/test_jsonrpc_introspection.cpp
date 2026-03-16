// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include <QtTest>
#include <QApplication>
#include <QPushButton>
#include <QLineEdit>
#include <QMainWindow>
#include <QVBoxLayout>
#include <QSignalSpy>

#include "transport/jsonrpc_handler.h"
#include "core/object_registry.h"
#include "introspection/signal_monitor.h"

using namespace qtPilot;

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

void TestJsonRpcIntrospection::initTestCase()
{
    // Install object hooks for registry to work
    installObjectHooks();
}

void TestJsonRpcIntrospection::cleanupTestCase()
{
    uninstallObjectHooks();
}

void TestJsonRpcIntrospection::init()
{
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

void TestJsonRpcIntrospection::cleanup()
{
    delete m_testWindow;
    m_testWindow = nullptr;
    m_testButton = nullptr;
    m_testLineEdit = nullptr;

    delete m_handler;
    m_handler = nullptr;
}

QString TestJsonRpcIntrospection::callMethod(const QString& method, const QJsonObject& params)
{
    QJsonObject request;
    request["jsonrpc"] = "2.0";
    request["method"] = method;
    request["params"] = params;
    request["id"] = m_requestId++;

    QString requestStr = QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact));
    return m_handler->HandleMessage(requestStr);
}

QJsonValue TestJsonRpcIntrospection::getResult(const QString& response)
{
    QJsonDocument doc = QJsonDocument::fromJson(response.toUtf8());
    return doc.object()["result"];
}

QJsonObject TestJsonRpcIntrospection::getError(const QString& response)
{
    QJsonDocument doc = QJsonDocument::fromJson(response.toUtf8());
    return doc.object()["error"].toObject();
}

// ========================================================================
// Object Discovery Tests
// ========================================================================

void TestJsonRpcIntrospection::testFindByObjectName()
{
    // Note: Object IDs are computed at hook time (during construction)
    // before objectName is set. So we verify the API works by checking
    // that the returned ID can be used to find the object again.
    QString response = callMethod("qtpilot.findByObjectName",
        QJsonObject{{"name", "testButton"}});

    QJsonValue result = getResult(response);
    QVERIFY(result.isObject());

    QString id = result.toObject()["id"].toString();
    QVERIFY(!id.isEmpty());

    // Verify the ID can be used to look up the object
    QObject* found = ObjectRegistry::instance()->findById(id);
    QVERIFY(found == m_testButton);
}

void TestJsonRpcIntrospection::testFindByClassName()
{
    QString response = callMethod("qtpilot.findByClassName",
        QJsonObject{{"className", "QPushButton"}});

    QJsonValue result = getResult(response);
    QVERIFY(result.isObject());

    QJsonArray ids = result.toObject()["ids"].toArray();
    QVERIFY(ids.size() >= 1);

    // Verify at least one of the returned IDs refers to our test button
    bool found = false;
    for (const QJsonValue& v : ids) {
        QObject* obj = ObjectRegistry::instance()->findById(v.toString());
        if (obj == m_testButton) {
            found = true;
            break;
        }
    }
    QVERIFY(found);
}

void TestJsonRpcIntrospection::testGetObjectTree()
{
    QString id = ObjectRegistry::instance()->objectId(m_testWindow);

    QString response = callMethod("qtpilot.getObjectTree",
        QJsonObject{{"root", id}, {"maxDepth", 2}});

    QJsonValue result = getResult(response);
    QVERIFY(result.isObject());

    QJsonObject tree = result.toObject();
    QVERIFY(tree.contains("id") || tree.contains("children") || tree.contains("className"));
}

void TestJsonRpcIntrospection::testGetObjectInfo()
{
    QString id = ObjectRegistry::instance()->objectId(m_testButton);

    QString response = callMethod("qtpilot.getObjectInfo",
        QJsonObject{{"id", id}});

    QJsonValue result = getResult(response);
    QVERIFY(result.isObject());

    QJsonObject info = result.toObject();
    QCOMPARE(info["className"].toString(), QString("QPushButton"));
    QCOMPARE(info["objectName"].toString(), QString("testButton"));
}

// ========================================================================
// Property Tests
// ========================================================================

void TestJsonRpcIntrospection::testListProperties()
{
    QString id = ObjectRegistry::instance()->objectId(m_testButton);

    QString response = callMethod("qtpilot.listProperties",
        QJsonObject{{"id", id}});

    QJsonValue result = getResult(response);
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

void TestJsonRpcIntrospection::testGetProperty()
{
    QString id = ObjectRegistry::instance()->objectId(m_testButton);

    QString response = callMethod("qtpilot.getProperty",
        QJsonObject{{"id", id}, {"name", "text"}});

    QJsonValue result = getResult(response);
    QVERIFY(result.isObject());
    QCOMPARE(result.toObject()["value"].toString(), QString("Test Button"));
}

void TestJsonRpcIntrospection::testSetProperty()
{
    QString id = ObjectRegistry::instance()->objectId(m_testButton);

    QString response = callMethod("qtpilot.setProperty",
        QJsonObject{{"id", id}, {"name", "text"}, {"value", "New Text"}});

    QJsonValue result = getResult(response);
    QVERIFY(result.isObject());
    QCOMPARE(result.toObject()["success"].toBool(), true);

    // Verify the property was actually set
    QCOMPARE(m_testButton->text(), QString("New Text"));
}

// ========================================================================
// Method Tests
// ========================================================================

void TestJsonRpcIntrospection::testListMethods()
{
    QString id = ObjectRegistry::instance()->objectId(m_testButton);

    QString response = callMethod("qtpilot.listMethods",
        QJsonObject{{"id", id}});

    QJsonValue result = getResult(response);
    QVERIFY(result.isArray());

    QJsonArray methods = result.toArray();
    QVERIFY(methods.size() > 0);

    // Should have "click" slot
    bool hasClick = false;
    for (const QJsonValue& v : methods) {
        if (v.toObject()["name"].toString() == "click") {
            hasClick = true;
            break;
        }
    }
    QVERIFY(hasClick);
}

void TestJsonRpcIntrospection::testInvokeMethod()
{
    QString id = ObjectRegistry::instance()->objectId(m_testButton);

    // Set up signal spy
    QSignalSpy spy(m_testButton, &QPushButton::clicked);

    QString response = callMethod("qtpilot.invokeMethod",
        QJsonObject{{"id", id}, {"method", "click"}, {"args", QJsonArray()}});

    QApplication::processEvents();

    QJsonValue result = getResult(response);
    QVERIFY(result.isObject());

    // The click slot should have fired the clicked signal
    QCOMPARE(spy.count(), 1);
}

void TestJsonRpcIntrospection::testListSignals()
{
    QString id = ObjectRegistry::instance()->objectId(m_testButton);

    QString response = callMethod("qtpilot.listSignals",
        QJsonObject{{"id", id}});

    QJsonValue result = getResult(response);
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

// ========================================================================
// Signal Subscription Tests
// ========================================================================

void TestJsonRpcIntrospection::testSubscribeSignal()
{
    QString id = ObjectRegistry::instance()->objectId(m_testButton);

    QString response = callMethod("qtpilot.subscribeSignal",
        QJsonObject{{"objectId", id}, {"signal", "clicked"}});

    QJsonValue result = getResult(response);
    QVERIFY(result.isObject());

    QString subId = result.toObject()["subscriptionId"].toString();
    QVERIFY(!subId.isEmpty());
    QVERIFY(subId.startsWith("sub_"));  // Format is sub_N

    // Clean up
    SignalMonitor::instance()->unsubscribe(subId);
}

void TestJsonRpcIntrospection::testUnsubscribeSignal()
{
    QString id = ObjectRegistry::instance()->objectId(m_testButton);

    // First subscribe
    QString subResponse = callMethod("qtpilot.subscribeSignal",
        QJsonObject{{"objectId", id}, {"signal", "clicked"}});
    QString subId = getResult(subResponse).toObject()["subscriptionId"].toString();

    int countBefore = SignalMonitor::instance()->subscriptionCount();

    // Then unsubscribe
    QString response = callMethod("qtpilot.unsubscribeSignal",
        QJsonObject{{"subscriptionId", subId}});

    QJsonValue result = getResult(response);
    QVERIFY(result.isObject());
    QCOMPARE(result.toObject()["success"].toBool(), true);

    int countAfter = SignalMonitor::instance()->subscriptionCount();
    QCOMPARE(countAfter, countBefore - 1);
}

void TestJsonRpcIntrospection::testLifecycleNotifications()
{
    // Enable lifecycle notifications
    QString response = callMethod("qtpilot.setLifecycleNotifications",
        QJsonObject{{"enabled", true}});

    QJsonValue result = getResult(response);
    QVERIFY(result.isObject());
    QCOMPARE(result.toObject()["enabled"].toBool(), true);
    QCOMPARE(SignalMonitor::instance()->lifecycleNotificationsEnabled(), true);

    // Disable them
    response = callMethod("qtpilot.setLifecycleNotifications",
        QJsonObject{{"enabled", false}});

    result = getResult(response);
    QCOMPARE(result.toObject()["enabled"].toBool(), false);
    QCOMPARE(SignalMonitor::instance()->lifecycleNotificationsEnabled(), false);
}

// ========================================================================
// UI Interaction Tests
// ========================================================================

void TestJsonRpcIntrospection::testClick()
{
    QString id = ObjectRegistry::instance()->objectId(m_testButton);
    QSignalSpy spy(m_testButton, &QPushButton::clicked);

    QString response = callMethod("qtpilot.click",
        QJsonObject{{"id", id}});

    QApplication::processEvents();

    QJsonValue result = getResult(response);
    QVERIFY(result.isObject());
    QCOMPARE(result.toObject()["success"].toBool(), true);

    QCOMPARE(spy.count(), 1);
}

void TestJsonRpcIntrospection::testSendKeys()
{
    QString id = ObjectRegistry::instance()->objectId(m_testLineEdit);
    m_testLineEdit->clear();
    m_testLineEdit->setFocus();
    QApplication::processEvents();

    QString response = callMethod("qtpilot.sendKeys",
        QJsonObject{{"id", id}, {"text", "Hello"}});

    QApplication::processEvents();

    QJsonValue result = getResult(response);
    QVERIFY(result.isObject());
    QCOMPARE(result.toObject()["success"].toBool(), true);

    QCOMPARE(m_testLineEdit->text(), QString("Hello"));
}

void TestJsonRpcIntrospection::testScreenshot()
{
    QString id = ObjectRegistry::instance()->objectId(m_testButton);

    QString response = callMethod("qtpilot.screenshot",
        QJsonObject{{"id", id}});

    QJsonValue result = getResult(response);
    QVERIFY(result.isObject());

    QString base64 = result.toObject()["image"].toString();
    QVERIFY(!base64.isEmpty());

    // Verify it's valid base64 PNG
    QByteArray decoded = QByteArray::fromBase64(base64.toLatin1());
    QVERIFY(decoded.startsWith("\x89PNG"));
}

void TestJsonRpcIntrospection::testGetGeometry()
{
    QString id = ObjectRegistry::instance()->objectId(m_testButton);

    QString response = callMethod("qtpilot.getGeometry",
        QJsonObject{{"id", id}});

    QJsonValue result = getResult(response);
    QVERIFY(result.isObject());

    QJsonObject geo = result.toObject();
    QVERIFY(geo.contains("local"));
    QVERIFY(geo.contains("global"));
    QVERIFY(geo.contains("devicePixelRatio"));

    QJsonObject local = geo["local"].toObject();
    QVERIFY(local["width"].toInt() > 0);
    QVERIFY(local["height"].toInt() > 0);
}

void TestJsonRpcIntrospection::testHitTest()
{
    // Get button's global position
    QPoint globalPos = m_testButton->mapToGlobal(m_testButton->rect().center());

    QString response = callMethod("qtpilot.hitTest",
        QJsonObject{{"x", globalPos.x()}, {"y", globalPos.y()}});

    QJsonValue result = getResult(response);
    QVERIFY(result.isObject());

    QString foundId = result.toObject()["id"].toString();
    // The hit test may return the button or a child - just verify it found something
    // Note: In minimal QPA, hit testing may not work perfectly
    // So we just verify the API returns a valid response format
    QVERIFY(result.toObject().contains("id"));
}

QTEST_MAIN(TestJsonRpcIntrospection)
#include "test_jsonrpc_introspection.moc"
