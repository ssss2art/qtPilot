// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "common/qt_matchers.h"
#include "core/object_registry.h"
#include "core/probe.h"
#include "introspection/signal_monitor.h"
#include "transport/websocket_server.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>
#include <QUrl>
#include <QWebSocket>

using namespace qtPilot;
using namespace qtPilot::test;

class TestProbeLifecycle : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() {
    // Configure environment before QCoreApplication / Probe singleton creation
    // so Probe::readConfiguration() picks up ephemeral port and explicit mode.
    if (qEnvironmentVariableIsEmpty("QTPILOT_BIND_ADDRESS")) {
      qputenv("QTPILOT_BIND_ADDRESS", QByteArray("loopback"));
    }
    qputenv("QTPILOT_PORT", QByteArray("0"));
    qputenv("QTPILOT_MODE", QByteArray("native"));

    if (QCoreApplication::instance() == nullptr) {
      int argc = 0;
      char** argv = nullptr;
      app_ = new QCoreApplication(argc, argv);
    }
  }

  void cleanupTestCase() {
    auto* probe = Probe::instance();
    if (probe && probe->isInitialized()) {
      probe->shutdown();
    }
    qunsetenv("QTPILOT_MODE");
  }

  void testEnvConfigAndInitializeIdempotency() {
    Probe* probe = Probe::instance();
    QVERIFY(probe != nullptr);
    QEXPECT_THAT(probe->mode(), QStrEq("native"));

    QVERIFY(probe->initialize());
    QEXPECT_THAT(probe->isInitialized(), IsTrue());
    QEXPECT_THAT(probe->isRunning(), IsTrue());
    QVERIFY(probe->server() != nullptr);
    QEXPECT_THAT(probe->port(), Ne(quint16{0}));

    WebSocketServer* firstServer = probe->server();
    const quint16 firstPort = probe->port();

    // Calling initialize() again must be a safe no-op returning true.
    QVERIFY(probe->initialize());
    QCOMPARE(probe->server(), firstServer);
    QCOMPARE(probe->port(), firstPort);
  }

  void testClientDisconnectedCleanup() {
    Probe* probe = Probe::instance();
    QVERIFY(probe->initialize());
    QVERIFY(probe->isRunning());

    QWebSocket client;
    QSignalSpy connectedSpy(probe, &Probe::clientConnected);
    client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(probe->port())));
    QVERIFY(connectedSpy.wait(5000));
    QEXPECT_THAT(probe->server()->hasActiveClient(), IsTrue());

    // Register a signal subscription while connected; disconnect must clear it.
    QObject target;
    target.setObjectName(QStringLiteral("lifecycleTestTarget"));
    const QString targetId = ObjectRegistry::instance()->objectId(&target);
    QVERIFY(!targetId.isEmpty());
    const QString subId =
        SignalMonitor::instance()->subscribe(targetId, QStringLiteral("objectNameChanged"));
    QVERIFY(!subId.isEmpty());
    QEXPECT_THAT(SignalMonitor::instance()->subscriptionCount(), Eq(1));

    QSignalSpy disconnectedSpy(probe, &Probe::clientDisconnected);
    client.close();
    QVERIFY(disconnectedSpy.wait(5000));
    QEXPECT_THAT(probe->server()->hasActiveClient(), IsFalse());
    QEXPECT_THAT(SignalMonitor::instance()->subscriptionCount(), Eq(0));
  }

  void testShutdownAndReinitialize() {
    Probe* probe = Probe::instance();
    QVERIFY(probe->initialize());
    QEXPECT_THAT(probe->isInitialized(), IsTrue());
    QEXPECT_THAT(probe->isRunning(), IsTrue());

    probe->shutdown();
    QEXPECT_THAT(probe->isInitialized(), IsFalse());
    QEXPECT_THAT(probe->isRunning(), IsFalse());
    QCOMPARE(probe->server(), nullptr);

    // shutdown() must be idempotent.
    probe->shutdown();
    QEXPECT_THAT(probe->isInitialized(), IsFalse());

    // Re-initializing after shutdown must succeed cleanly on an ephemeral port.
    probe->setPort(0);
    QVERIFY(probe->initialize());
    QEXPECT_THAT(probe->isInitialized(), IsTrue());
    QEXPECT_THAT(probe->isRunning(), IsTrue());
    QVERIFY(probe->server() != nullptr);
    QEXPECT_THAT(probe->port(), Ne(quint16{0}));

    probe->shutdown();
    QEXPECT_THAT(probe->isInitialized(), IsFalse());
  }

 private:
  QCoreApplication* app_ = nullptr;
};

QTEST_APPLESS_MAIN(TestProbeLifecycle)
#include "test_probe_lifecycle.moc"
