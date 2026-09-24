// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "common/qt_matchers.h"
#include "transport/jsonrpc_handler.h"
#include "transport/websocket_server.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QWebSocket>
#include <QtTest>

using namespace qtPilot;
using namespace qtPilot::test;

/// Where a request runs relative to the frame that carried it.
///
/// QtWebSockets delivers every frame in its read buffer from one call into
/// QWebSocketPrivate::processData, and that call is not reentrant. A handler run
/// from inside it that turns the event loop -- qt.sync does, sendKeys does, and
/// a slot that opens a modal dialog does -- lets the socket deliver again while
/// the first delivery is still on the stack. The deferred modes of
/// qt.methods.invoke and qt.ui.activateMenuItem only keep application code out
/// of the request if the request itself is out of the frame dispatch.
class TestWebSocketDispatch : public QObject {
  Q_OBJECT

 private:
  /// Both ends must be connected: the server accepting is not enough, and a
  /// frame sent before the client's own handshake completes is dropped.
  static bool attachClient(WebSocketServer& server, QWebSocket& client) {
    QSignalSpy accepted(&server, &WebSocketServer::clientConnected);
    QSignalSpy connected(&client, &QWebSocket::connected);
    client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(server.port())));
    return (!accepted.isEmpty() || accepted.wait(5000)) &&
           (!connected.isEmpty() || connected.wait(5000));
  }

  static QString request(int id, const QString& method) {
    return QString::fromUtf8(
        QJsonDocument(QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"method", method}})
            .toJson(QJsonDocument::Compact));
  }

 private slots:
  void init() { qunsetenv("QTPILOT_BIND_ADDRESS"); }

  void aRequestRunsAfterItsFrameIsDelivered() {
    WebSocketServer server(0);
    QVERIFY(server.start());
    bool dispatching = true;
    server.rpcHandler()->RegisterMethod(QStringLiteral("test.whereAmI"),
                                        [&](const QString&) -> QString {
                                          dispatching = server.inFrameDispatch();
                                          return QStringLiteral("{}");
                                        });
    QWebSocket client;
    QVERIFY(attachClient(server, client));
    QSignalSpy replies(&client, &QWebSocket::textMessageReceived);

    client.sendTextMessage(request(1, QStringLiteral("test.whereAmI")));

    QVERIFY(replies.wait(5000));
    QEXPECT_THAT(dispatching, IsFalse());
  }

  // Queued requests must still be answered in the order they were sent.
  void pipelinedRequestsAreAnsweredInOrder() {
    WebSocketServer server(0);
    QVERIFY(server.start());
    server.rpcHandler()->RegisterMethod(QStringLiteral("test.echo"),
                                        [](const QString&) { return QStringLiteral("{}"); });
    QWebSocket client;
    QVERIFY(attachClient(server, client));
    QSignalSpy replies(&client, &QWebSocket::textMessageReceived);

    for (int id = 1; id <= 5; ++id) {
      client.sendTextMessage(request(id, QStringLiteral("test.echo")));
    }

    QTRY_COMPARE_WITH_TIMEOUT(replies.count(), 5, 5000);
    for (int i = 0; i < 5; ++i) {
      const QJsonObject reply =
          QJsonDocument::fromJson(replies.at(i).at(0).toString().toUtf8()).object();
      QEXPECT_THAT(reply, HasJsonField("id", Eq(i + 1)));
    }
  }
};

QTEST_MAIN(TestWebSocketDispatch)
#include "test_websocket_dispatch.moc"
