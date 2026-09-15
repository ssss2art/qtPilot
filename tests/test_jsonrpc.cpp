// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "transport/jsonrpc_handler.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QString>
#include <QTest>

#include "common/qt_matchers.h"

using namespace qtPilot::test;

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

    QEXPECT_THAT(QJsonDocument::fromJson(response.toUtf8()).object(),
                 AllOf(HasJsonRpcId(1),
                       IsJsonRpcSuccess(HasJsonField("hello", "world"))));
  }

  // Test: Parse error returns -32700
  void test_parseError() {
    QString request = R"({invalid json)";
    QString response = handler_->HandleMessage(request);

    QEXPECT_THAT(QJsonDocument::fromJson(response.toUtf8()).object(),
                 IsJsonRpcError(-32700));
  }

  // Test: Invalid request (missing jsonrpc) returns -32600
  void test_invalidRequest() {
    QString request = R"({"id":1,"method":"ping"})";
    QString response = handler_->HandleMessage(request);

    QEXPECT_THAT(QJsonDocument::fromJson(response.toUtf8()).object(),
                 IsJsonRpcError(-32600));
  }

  // Test: Unknown method returns -32601
  void test_methodNotFound() {
    QString request = R"({"jsonrpc":"2.0","id":1,"method":"unknownMethod"})";
    QString response = handler_->HandleMessage(request);

    QEXPECT_THAT(QJsonDocument::fromJson(response.toUtf8()).object(),
                 IsJsonRpcError(-32601));
  }

  // Test: Notification (no id) returns empty response
  void test_notification() {
    QString request = R"({"jsonrpc":"2.0","method":"ping"})";
    QString response = handler_->HandleMessage(request);

    QEXPECT_THAT(response, QIsEmpty());
  }

  // Test: Notification emits signal
  void test_notificationSignal() {
    QSignalSpy spy(handler_, &qtPilot::JsonRpcHandler::NotificationReceived);
    QString request = R"({"jsonrpc":"2.0","method":"test.notify","params":{"key":"value"}})";
    QString response = handler_->HandleMessage(request);

    QEXPECT_THAT(response, QIsEmpty());
    QEXPECT_THAT(spy.count(), Eq(1));
    QList<QVariant> args = spy.takeFirst();
    QEXPECT_THAT(args.at(0).toString(), QStrEq("test.notify"));
  }

  // Test: Ping returns pong
  void test_pingReturnsPong() {
    QString request = R"({"jsonrpc":"2.0","id":1,"method":"ping"})";
    QString response = handler_->HandleMessage(request);

    QEXPECT_THAT(QJsonDocument::fromJson(response.toUtf8()).object(),
                 AllOf(HasJsonRpcId(1),
                       IsJsonRpcSuccess(QStrEq("pong"))));
  }

  // Test: getVersion returns version info
  void test_getVersionReturnsVersionInfo() {
    QString request = R"({"jsonrpc":"2.0","id":2,"method":"getVersion"})";
    QString response = handler_->HandleMessage(request);

    QEXPECT_THAT(QJsonDocument::fromJson(response.toUtf8()).object(),
                 AllOf(HasJsonRpcId(2),
                       IsJsonRpcSuccess(AllOf(HasJsonField("version"),
                                              HasJsonField("protocol"),
                                              HasJsonField("name", "qtPilot")))));
  }

  // Test: getVersion reports the ACTUAL build version.
  //
  // The check above only asserted the key was present, which is how a hardcoded
  // "0.1.0" survived here while the project moved to 0.3.1 -- every client was
  // told the wrong version and no test objected. QTPILOT_EXPECTED_VERSION comes
  // from CMake's PROJECT_VERSION, so this fails if the handler is ever pinned to
  // a literal again.
  void test_getVersionReportsTheBuildVersion() {
    QString request = R"({"jsonrpc":"2.0","id":2,"method":"getVersion"})";
    QString response = handler_->HandleMessage(request);

    QEXPECT_THAT(QJsonDocument::fromJson(response.toUtf8()).object(),
                 IsJsonRpcSuccess(HasJsonField(
                     "version",
                     AllOf(QStrEq(QTPILOT_EXPECTED_VERSION),
                           Not(QStrEq("0.1.0"))))));
  }

  // Test: getVersion carries a wire-protocol revision a client can negotiate on.
  //
  // Without this the client had no way to detect skew, so a probe built from a
  // different revision failed later as an opaque "method not found".
  void test_getVersionCarriesProtocolVersion() {
    QString request = R"({"jsonrpc":"2.0","id":2,"method":"getVersion"})";
    QString response = handler_->HandleMessage(request);

    QEXPECT_THAT(QJsonDocument::fromJson(response.toUtf8()).object(),
                 IsJsonRpcSuccess(HasJsonField("protocolVersion", 1)));
  }

  // Test: echo returns params
  void test_echoReturnsParams() {
    QString request = R"({"jsonrpc":"2.0","id":3,"method":"echo","params":{"foo":"bar"}})";
    QString response = handler_->HandleMessage(request);

    QEXPECT_THAT(QJsonDocument::fromJson(response.toUtf8()).object(),
                 AllOf(HasJsonRpcId(3),
                       IsJsonRpcSuccess(HasJsonField("foo", "bar"))));
  }

  // Test: String id is preserved
  void test_stringIdIsPreserved() {
    QString request = R"({"jsonrpc":"2.0","id":"my-request-id","method":"ping"})";
    QString response = handler_->HandleMessage(request);

    QEXPECT_THAT(QJsonDocument::fromJson(response.toUtf8()).object(),
                 HasJsonRpcId("my-request-id"));
  }

  // Test: getModes returns array
  void test_getModesReturnsArray() {
    QString request = R"({"jsonrpc":"2.0","id":5,"method":"getModes"})";
    QString response = handler_->HandleMessage(request);

    QEXPECT_THAT(QJsonDocument::fromJson(response.toUtf8()).object(),
                 AllOf(HasJsonRpcId(5),
                       IsJsonRpcSuccess(JsonArraySize(3))));
  }

  // Test: Custom method can be registered
  void test_customMethodCanBeRegistered() {
    handler_->RegisterMethod("customMethod", [](const QString& /*params*/) -> QString {
      return R"({"custom":"response"})";
    });

    QString request = R"({"jsonrpc":"2.0","id":6,"method":"customMethod"})";
    QString response = handler_->HandleMessage(request);

    QEXPECT_THAT(QJsonDocument::fromJson(response.toUtf8()).object(),
                 AllOf(HasJsonRpcId(6),
                       IsJsonRpcSuccess(HasJsonField("custom", "response"))));
  }

  // Test: qtpilot.echo method works (per RESEARCH.md spec)
  void test_qtpilotEchoMethod() {
    QString request =
        R"({"jsonrpc":"2.0","method":"qtpilot.echo","params":{"test":"data"},"id":7})";
    QString response = handler_->HandleMessage(request);

    QEXPECT_THAT(QJsonDocument::fromJson(response.toUtf8()).object(),
                 AllOf(HasJsonRpcId(7),
                       IsJsonRpcSuccess(HasJsonField("test", "data"))));
  }

 private:
  qtPilot::JsonRpcHandler* handler_ = nullptr;
  static QCoreApplication* app_;
};

QCoreApplication* TestJsonRpc::app_ = nullptr;

QTEST_APPLESS_MAIN(TestJsonRpc)
#include "test_jsonrpc.moc"
