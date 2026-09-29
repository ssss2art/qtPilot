// Copyright (c) 2024-2026 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "common/qt_matchers.h"
#include "transport/jsonrpc_handler.h"
#include "transport/websocket_server.h"

#include <memory>

#include <QAbstractSocket>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTimer>
#include <QWebSocket>
#include <QtTest>

using namespace qtPilot;
using namespace qtPilot::test;

namespace {

/// Requests the chaos loop picks from, including shapes a handler may reject.
const QStringList& requestBodies() {
  static const QStringList kBodies = {
      QStringLiteral(R"({"jsonrpc":"2.0","id":1,"method":"ping"})"),
      QStringLiteral(R"({"jsonrpc":"2.0","id":2,"method":"getVersion"})"),
      QStringLiteral(R"({"jsonrpc":"2.0","id":3,"method":"chaos.slow"})"),
      QStringLiteral(R"({"jsonrpc":"2.0","id":4,"method":"no.such.method"})"),
      QStringLiteral(R"({"jsonrpc":"2.0","method":"ping"})"),  // notification
      QStringLiteral(R"({"jsonrpc":"2.0","id":5,"method":"ping","params":{"x":1}})"),
      QStringLiteral(R"(not json at all)"),
      QStringLiteral(R"({"jsonrpc":"2.0","id":6})"),  // no method
      QStringLiteral(""),
  };
  return kBodies;
}

}  // namespace

/// @brief Randomized connect/send/vanish cycles against a live server.
///
/// The bugs this is aimed at are lifecycle races, not parser bugs: a client
/// that goes away at an awkward moment while the server is mid-request. Those
/// are invisible to a test that connects, asks politely and disconnects, and
/// they are what actually crashed a host application in the field --
/// `onTextMessage()` sending a reply on a pointer that a disconnect had already
/// cleared, seen as a SIGSEGV at address 0x8.
///
/// Every iteration picks its timing from a seeded generator, so a failure is
/// replayable: the seed is printed on failure and can be forced with
/// QTPILOT_CHAOS_SEED. Run it under ASan, where a use-after-free is a
/// diagnosable report rather than a crash that may or may not happen to land on
/// mapped memory.
class TestTransportChaos : public QObject {
  Q_OBJECT

 private:
  /// Connect a client and wait until BOTH sides are ready.
  ///
  /// The server accepting is not the same as the client being ready: a
  /// sendTextMessage() issued before the client's own handshake completes is
  /// dropped on the floor, and the test then waits for a reply that was never
  /// asked for.
  static bool attach(WebSocketServer& server, QWebSocket& client) {
    QSignalSpy accepted(&server, &WebSocketServer::clientConnected);
    QSignalSpy opened(&client, &QWebSocket::connected);
    client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(server.port())));
    if (!accepted.wait(5000)) {
      return false;
    }
    if (client.state() != QAbstractSocket::ConnectedState && !opened.wait(5000)) {
      return false;
    }
    return true;
  }

  /// Spin the event loop for a short while without blocking on a condition.
  static void spin(int ms) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
  }

 private slots:
  void init() { qunsetenv("QTPILOT_BIND_ADDRESS"); }

  /// Clients that connect, ask, and vanish at randomized moments.
  ///
  /// Seeded from a fixed list rather than the clock. A chaos test that picks a
  /// fresh seed every run finds more over time but fails only sometimes, which
  /// in CI is indistinguishable from flakiness and gets muted. These seeds are
  /// known to cover the interesting interleavings -- seed 5 is the one that
  /// reproduced the cleared-client crash -- so a regression fails every run.
  /// QTPILOT_CHAOS_SEED overrides for a longer hunt.
  void clientsVanishingAtArbitraryMomentsDoNotTakeTheServerDown_data() {
    QTest::addColumn<quint32>("seed");
    if (qEnvironmentVariableIsSet("QTPILOT_CHAOS_SEED")) {
      const quint32 forced = qEnvironmentVariableIntValue("QTPILOT_CHAOS_SEED");
      QTest::newRow("forced") << forced;
      return;
    }
    for (quint32 seed : {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u}) {
      QTest::newRow(qPrintable(QStringLiteral("seed-%1").arg(seed))) << seed;
    }
  }

  void clientsVanishingAtArbitraryMomentsDoNotTakeTheServerDown() {
    QFETCH(quint32, seed);
    QRandomGenerator rng(seed);
    qInfo() << "[chaos] seed" << seed;

    WebSocketServer server(0);
    QEXPECT_THAT(server.start(), IsTrue());

    // A handler that turns the event loop, which is what gives a disconnect the
    // chance to land mid-request. A modal popup is the real-world version.
    server.rpcHandler()->RegisterMethod(
        QStringLiteral("chaos.slow"), [](const QString&) -> QString {
          QEventLoop nested;
          QTimer::singleShot(15, &nested, &QEventLoop::quit);
          nested.exec();
          return QStringLiteral(R"({"jsonrpc":"2.0","id":3,"result":"slow"})");
        });

    constexpr int kIterations = 60;
    int completed = 0;
    for (int i = 0; i < kIterations; ++i) {
      auto client = std::make_unique<QWebSocket>();
      if (!attach(server, *client)) {
        // A refused connection is a legitimate outcome (single-client server
        // still tearing the last one down); give it a moment and move on.
        spin(20);
        continue;
      }

      const QString body = requestBodies().at(rng.bounded(requestBodies().size()));
      client->sendTextMessage(body);

      // Vanish somewhere between "before the request is even read" and "well
      // after the reply", including right in the middle of the slow handler.
      const int delayMs = rng.bounded(0, 30);
      if (delayMs > 0) {
        spin(delayMs);
      }

      switch (rng.bounded(3)) {
        case 0:
          client->abort();  // no close handshake at all
          break;
        case 1:
          client->close();  // polite close
          break;
        default:
          // Abort first, then destroy immediately. The immediate destruction is
          // the point -- it is what makes the server notice the disconnect while
          // a request is still in flight, and a deleteLater() here defers that
          // past the window and finds nothing. The abort() before it shuts down
          // Qt's own read path, which otherwise crashes inside processData() for
          // reasons belonging to the test client rather than the server.
          client->abort();
          client.reset();
          break;
      }

      spin(rng.bounded(1, 12));
      if (client) {
        client->abort();
        client.reset();
      }
      ++completed;

      // The server has to still be usable after every one of these.
      QVERIFY2(server.isListening(),
               qPrintable(QStringLiteral("server stopped listening after iteration %1 (seed %2)")
                              .arg(i)
                              .arg(seed)));
    }

    qInfo() << "[chaos] completed" << completed << "of" << kIterations << "cycles";
    QEXPECT_THAT(completed, Gt(0));

    // And a plain request must still work at the end of all that.
    QWebSocket survivor;
    QEXPECT_THAT(attach(server, survivor), IsTrue());
    QSignalSpy reply(&survivor, &QWebSocket::textMessageReceived);
    survivor.sendTextMessage(QStringLiteral(R"({"jsonrpc":"2.0","id":99,"method":"ping"})"));
    QEXPECT_THAT(reply.wait(5000), IsTrue());
    QEXPECT_THAT(reply.first().first().toString(), QStrContains("pong"));
  }
};

QTEST_MAIN(TestTransportChaos)
#include "test_transport_chaos.moc"
