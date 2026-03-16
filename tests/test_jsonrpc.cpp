// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QString>
#include <QTest>

#include "transport/jsonrpc_handler.h"

class TestJsonRpc : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() {
    // Ensure QCoreApplication exists for Qt functionality
    if (QCoreApplication::instance() == nullptr) {
      int argc = 0;
      char** argv = nullptr;
      app_ = new QCoreApplication(argc, argv);
    }
  }

  void cleanupTestCase() {
    // Don't delete app_ - it persists across test runs
  }

  void init() { handler_ = new qtPilot::JsonRpcHandler(this); }

  void cleanup() {
    delete handler_;
    handler_ = nullptr;
  }

  // Test: Valid request returns result
  void test_validRequest() {
    QString request =
        R"({"jsonrpc":"2.0","method":"qtpilot.echo","params":{"hello":"world"},"id":1})";
    QString response = handler_->HandleMessage(request);

    QJsonDocument doc = QJsonDocument::fromJson(response.toUtf8());
    QJsonObject obj = doc.object();

    QCOMPARE(obj["jsonrpc"].toString(), QString("2.0"));
    QCOMPARE(obj["id"].toInt(), 1);
    QCOMPARE(obj["result"].toObject()["hello"].toString(), QString("world"));
  }

  // Test: Parse error returns -32700
  void test_parseError() {
    QString request = R"({invalid json)";
    QString response = handler_->HandleMessage(request);

    QJsonDocument doc = QJsonDocument::fromJson(response.toUtf8());
    QJsonObject obj = doc.object();

    QVERIFY(obj.contains("error"));
    QCOMPARE(obj["error"].toObject()["code"].toInt(), -32700);
  }

  // Test: Invalid request (missing jsonrpc) returns -32600
  void test_invalidRequest() {
    QString request = R"({"id":1,"method":"ping"})";
    QString response = handler_->HandleMessage(request);

    QJsonDocument doc = QJsonDocument::fromJson(response.toUtf8());
    QJsonObject obj = doc.object();

    QVERIFY(obj.contains("error"));
    QCOMPARE(obj["error"].toObject()["code"].toInt(), -32600);
  }

  // Test: Unknown method returns -32601
  void test_methodNotFound() {
    QString request = R"({"jsonrpc":"2.0","id":1,"method":"unknownMethod"})";
    QString response = handler_->HandleMessage(request);

    QJsonDocument doc = QJsonDocument::fromJson(response.toUtf8());
    QJsonObject obj = doc.object();

    QVERIFY(obj.contains("error"));
    QCOMPARE(obj["error"].toObject()["code"].toInt(), -32601);
  }

  // Test: Notification (no id) returns empty response
  void test_notification() {
    QString request = R"({"jsonrpc":"2.0","method":"ping"})";
    QString response = handler_->HandleMessage(request);

    QVERIFY(response.isEmpty());
  }

  // Test: Notification emits signal
  void test_notificationSignal() {
    QSignalSpy spy(handler_, &qtPilot::JsonRpcHandler::NotificationReceived);
    QString request = R"({"jsonrpc":"2.0","method":"test.notify","params":{"key":"value"}})";
    QString response = handler_->HandleMessage(request);

    QVERIFY(response.isEmpty());
    QCOMPARE(spy.count(), 1);
    QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).toString(), QString("test.notify"));
  }

  // Test: Ping returns pong
  void test_pingReturnsPong() {
    QString request = R"({"jsonrpc":"2.0","id":1,"method":"ping"})";
    QString response = handler_->HandleMessage(request);

    QJsonDocument doc = QJsonDocument::fromJson(response.toUtf8());
    QJsonObject obj = doc.object();

    QCOMPARE(obj["jsonrpc"].toString(), QString("2.0"));
    QCOMPARE(obj["id"].toInt(), 1);
    QCOMPARE(obj["result"].toString(), QString("pong"));
  }

  // Test: getVersion returns version info
  void test_getVersionReturnsVersionInfo() {
    QString request = R"({"jsonrpc":"2.0","id":2,"method":"getVersion"})";
    QString response = handler_->HandleMessage(request);

    QJsonDocument doc = QJsonDocument::fromJson(response.toUtf8());
    QJsonObject obj = doc.object();

    QCOMPARE(obj["jsonrpc"].toString(), QString("2.0"));
    QCOMPARE(obj["id"].toInt(), 2);
    QVERIFY(obj["result"].toObject().contains("version"));
    QVERIFY(obj["result"].toObject().contains("protocol"));
    QVERIFY(obj["result"].toObject().contains("name"));
    QCOMPARE(obj["result"].toObject()["name"].toString(), QString("qtPilot"));
  }

  // Test: echo returns params
  void test_echoReturnsParams() {
    QString request =
        R"({"jsonrpc":"2.0","id":3,"method":"echo","params":{"foo":"bar"}})";
    QString response = handler_->HandleMessage(request);

    QJsonDocument doc = QJsonDocument::fromJson(response.toUtf8());
    QJsonObject obj = doc.object();

    QCOMPARE(obj["jsonrpc"].toString(), QString("2.0"));
    QCOMPARE(obj["id"].toInt(), 3);
    QCOMPARE(obj["result"].toObject()["foo"].toString(), QString("bar"));
  }

  // Test: String id is preserved
  void test_stringIdIsPreserved() {
    QString request =
        R"({"jsonrpc":"2.0","id":"my-request-id","method":"ping"})";
    QString response = handler_->HandleMessage(request);

    QJsonDocument doc = QJsonDocument::fromJson(response.toUtf8());
    QJsonObject obj = doc.object();

    QCOMPARE(obj["id"].toString(), QString("my-request-id"));
  }

  // Test: getModes returns array
  void test_getModesReturnsArray() {
    QString request = R"({"jsonrpc":"2.0","id":5,"method":"getModes"})";
    QString response = handler_->HandleMessage(request);

    QJsonDocument doc = QJsonDocument::fromJson(response.toUtf8());
    QJsonObject obj = doc.object();

    QCOMPARE(obj["jsonrpc"].toString(), QString("2.0"));
    QVERIFY(obj["result"].isArray());
    QCOMPARE(obj["result"].toArray().size(), 3);
  }

  // Test: Custom method can be registered
  void test_customMethodCanBeRegistered() {
    handler_->RegisterMethod("customMethod",
                             [](const QString& /*params*/) -> QString {
                               return R"({"custom":"response"})";
                             });

    QString request = R"({"jsonrpc":"2.0","id":6,"method":"customMethod"})";
    QString response = handler_->HandleMessage(request);

    QJsonDocument doc = QJsonDocument::fromJson(response.toUtf8());
    QJsonObject obj = doc.object();

    QCOMPARE(obj["result"].toObject()["custom"].toString(),
             QString("response"));
  }

  // Test: qtpilot.echo method works (per RESEARCH.md spec)
  void test_qtpilotEchoMethod() {
    QString request =
        R"({"jsonrpc":"2.0","method":"qtpilot.echo","params":{"test":"data"},"id":7})";
    QString response = handler_->HandleMessage(request);

    QJsonDocument doc = QJsonDocument::fromJson(response.toUtf8());
    QJsonObject obj = doc.object();

    QCOMPARE(obj["jsonrpc"].toString(), QString("2.0"));
    QCOMPARE(obj["id"].toInt(), 7);
    QCOMPARE(obj["result"].toObject()["test"].toString(), QString("data"));
  }

 private:
  qtPilot::JsonRpcHandler* handler_ = nullptr;
  static QCoreApplication* app_;
};

QCoreApplication* TestJsonRpc::app_ = nullptr;

QTEST_APPLESS_MAIN(TestJsonRpc)
#include "test_jsonrpc.moc"
