// Copyright (c) 2026 qtPilot Contributors
// SPDX-License-Identifier: MIT
#include "common/qt_matchers.h"
#include "transport/jsonrpc_handler.h"
#include "transport/websocket_server.h"

#include <memory>
#include <vector>

#include <QFile>
#include <QNetworkRequest>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUuid>
#include <QWebSocket>
#include <QtTest>

using namespace qtPilot;
using namespace qtPilot::test;

class TestAuthenticatedTransport : public QObject {
  Q_OBJECT
 private:
  QTemporaryDir m_directory;
  QByteArray m_token;

  QNetworkRequest request(const WebSocketServer& server, const QByteArray& token) const {
    QNetworkRequest request(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(server.port())));
    if (!token.isEmpty()) {
      request.setRawHeader("Authorization", "Bearer " + token);
    }
    return request;
  }

  void provision(const QByteArray& token) {
    const QString path = m_directory.filePath(QStringLiteral("token"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write(token), token.size());
    file.close();
    qputenv("QTPILOT_AUTH_TOKEN_FILE", path.toUtf8());
  }

 private slots:
  void init() {
    qunsetenv("QTPILOT_BIND_ADDRESS");
    qunsetenv("QTPILOT_TLS_CERT_FILE");
    qunsetenv("QTPILOT_TLS_KEY_FILE");
    qputenv("QTPILOT_PROFILE", "local");
    m_token = QUuid::createUuid().toString(QUuid::Id128).toLatin1();
    provision(m_token);
  }
  void cleanup() {
    for (const auto* key : {"QTPILOT_PROFILE", "QTPILOT_AUTH_TOKEN_FILE", "QTPILOT_TLS_CERT_FILE",
                            "QTPILOT_TLS_KEY_FILE", "QTPILOT_BIND_ADDRESS"}) {
      qunsetenv(key);
    }
  }

  void deniedClientsHaveNoApplicationEffects_data() {
    QTest::addColumn<bool>("missing");
    QTest::newRow("missing credential") << true;
    QTest::newRow("wrong credential") << false;
  }
  void deniedClientsHaveNoApplicationEffects() {
    QFETCH(bool, missing);
    WebSocketServer server(0);
    QVERIFY(server.start());
    int effects = 0;
    server.rpcHandler()->RegisterMethod(QStringLiteral("test.mutate"),
                                        [&](const QString&) { return QString::number(++effects); });
    QWebSocket client;
    connect(&client, &QWebSocket::connected, &client, [&] {
      client.sendTextMessage(QStringLiteral(R"({"jsonrpc":"2.0","id":1,"method":"test.mutate"})"));
    });
    client.open(request(server, missing ? QByteArray() : QUuid::createUuid().toByteArray()));
    QTRY_VERIFY_WITH_TIMEOUT(effects != 0 || client.state() == QAbstractSocket::UnconnectedState,
                             3000);
    QEXPECT_THAT(effects, Eq(0));
    QEXPECT_THAT(server.hasActiveClient(), IsFalse());
  }

  void validCredentialIsAcknowledgedBeforeDispatch() {
    WebSocketServer server(0);
    QVERIFY(server.start());
    int effects = 0;
    server.rpcHandler()->RegisterMethod(QStringLiteral("test.mutate"),
                                        [&](const QString&) { return QString::number(++effects); });
    QWebSocket client;
    QSignalSpy replies(&client, &QWebSocket::textMessageReceived);
    client.open(request(server, m_token));
    QTRY_VERIFY_WITH_TIMEOUT(!replies.isEmpty(), 3000);
    const auto acknowledgement =
        QJsonDocument::fromJson(replies[0][0].toString().toUtf8()).object();
    QEXPECT_THAT(acknowledgement,
                 HasJsonField("method", Eq(QStringLiteral("qtpilot.authenticated"))));
    QEXPECT_THAT(effects, Eq(0));
    client.sendTextMessage(QStringLiteral(R"({"jsonrpc":"2.0","id":1,"method":"test.mutate"})"));
    QTRY_COMPARE_WITH_TIMEOUT(effects, 1, 3000);
  }

  void tokenDoesNotBypassBrowserOriginPolicy() {
    WebSocketServer server(0);
    QVERIFY(server.start());
    QWebSocket client(QStringLiteral("https://untrusted.invalid"));
    QSignalSpy gone(&client, &QWebSocket::disconnected);
    client.open(request(server, m_token));
    QTRY_VERIFY_WITH_TIMEOUT(!gone.isEmpty(), 3000);
    QEXPECT_THAT(server.hasActiveClient(), IsFalse());
  }

  void invalidCredentialFileFailsBeforeListen() {
    provision(QByteArray("invalid"));
    WebSocketServer server(0);
    QEXPECT_THAT(server.start(), IsFalse());
    QEXPECT_THAT(server.isListening(), IsFalse());
  }

  void oneStalledUpgradeDoesNotReserveTheActiveClient() {
    WebSocketServer server(0);
    QVERIFY(server.start());
    QTcpSocket stalled;
    stalled.connectToHost(QHostAddress::LocalHost, server.port());
    QTRY_COMPARE_WITH_TIMEOUT(server.pendingAdmissionCount(), 1, 3000);
    QWebSocket client;
    QSignalSpy replies(&client, &QWebSocket::textMessageReceived);
    client.open(request(server, m_token));
    QTRY_VERIFY_WITH_TIMEOUT(!replies.isEmpty(), 3000);
    QEXPECT_THAT(server.hasActiveClient(), IsTrue());
  }

  void stalledUpgradeCountAndLifetimeAreBounded() {
    WebSocketServer server(0);
    QVERIFY(server.start());
    std::vector<std::unique_ptr<QTcpSocket>> stalled;
    for (int i = 0; i < 17; ++i) {
      auto socket = std::make_unique<QTcpSocket>();
      socket->connectToHost(QHostAddress::LocalHost, server.port());
      stalled.push_back(std::move(socket));
    }
    QTRY_COMPARE_WITH_TIMEOUT(server.pendingAdmissionCount(), 16, 3000);
    QEXPECT_THAT(server.hasActiveClient(), IsFalse());
    QTRY_COMPARE_WITH_TIMEOUT(server.pendingAdmissionCount(), 0, 6500);
  }
};
QTEST_GUILESS_MAIN(TestAuthenticatedTransport)
#include "test_authenticated_transport.moc"
