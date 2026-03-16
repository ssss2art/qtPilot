// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

// E2E test: verify that the launcher can elevate to admin, inject the probe
// into test_app, and that the probe's WebSocket server responds to queries.
//
// This test is gated: it calls QSKIP() when not running with admin privileges.
// Use "ctest -L admin" to target it, or "ctest -LE admin" to exclude it.

#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <QUdpSocket>
#include <QWebSocket>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <csignal>
#include <unistd.h>
#endif

namespace {

/// Check whether the current process is running elevated (admin).
bool isElevated() {
#ifdef Q_OS_WIN
  BOOL elevated = FALSE;
  HANDLE token = nullptr;
  if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
    TOKEN_ELEVATION elev = {};
    DWORD size = 0;
    if (GetTokenInformation(token, TokenElevation, &elev, sizeof(elev), &size)) {
      elevated = elev.TokenIsElevated;
    }
    CloseHandle(token);
  }
  return elevated != FALSE;
#else
  return geteuid() == 0;
#endif
}

/// Find a sibling executable relative to this test binary's directory.
/// Searches the test exe dir, then ../bin, then parent dir.
QString findExecutable(const QString& baseName) {
  QDir exeDir(QCoreApplication::applicationDirPath());

#ifdef Q_OS_WIN
  QString suffix = QStringLiteral(".exe");
#else
  QString suffix;
#endif

  QStringList searchDirs = {
      exeDir.absolutePath(),
      exeDir.absoluteFilePath(QStringLiteral("../bin")),
      exeDir.absoluteFilePath(QStringLiteral("..")),
  };

  for (const QString& dir : searchDirs) {
    QString candidate = QDir(dir).absoluteFilePath(baseName + suffix);
    if (QFileInfo::exists(candidate))
      return candidate;
  }
  return QString();
}

}  // namespace

class TestAdminInjection : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase();
  void testElevatedLaunchAndConnect();
  void cleanupTestCase();

 private:
  QString m_launcherPath;
  QString m_testAppPath;
  qint64 m_targetPid = 0;
};

void TestAdminInjection::initTestCase() {
  // Gate: skip if not elevated
  if (!isElevated())
    QSKIP("Test requires administrator privileges — run from an elevated terminal");

  // Locate the launcher executable
  m_launcherPath = findExecutable(QStringLiteral("qtpilot-launch"));
  if (m_launcherPath.isEmpty()) {
    // Try versioned names (e.g., qtpilot-launch-qt5.15)
    QDir exeDir(QCoreApplication::applicationDirPath());
    QStringList matches =
        exeDir.entryList({QStringLiteral("qtpilot-launch*")}, QDir::Files, QDir::Name);
    if (!matches.isEmpty())
      m_launcherPath = exeDir.absoluteFilePath(matches.first());
  }
  QVERIFY2(!m_launcherPath.isEmpty(), "Could not find qtpilot-launch executable in build output");

  // Locate the test_app executable
  m_testAppPath = findExecutable(QStringLiteral("qtPilot-test-app"));
  if (m_testAppPath.isEmpty())
    m_testAppPath = findExecutable(QStringLiteral("qtPilot_test_app"));
  QVERIFY2(!m_testAppPath.isEmpty(), "Could not find test app executable in build output");

  qDebug() << "Launcher:" << m_launcherPath;
  qDebug() << "Test app:" << m_testAppPath;
}

void TestAdminInjection::testElevatedLaunchAndConnect() {
  // 1. Bind a UDP socket to listen for probe discovery broadcasts
  QUdpSocket udp;
  QVERIFY2(udp.bind(QHostAddress::Any, 9221, QUdpSocket::ShareAddress),
           "Failed to bind UDP discovery socket on port 9221");

  // 2. Launch test_app via the launcher with admin elevation
  //    --port 0 : let OS assign an ephemeral port
  //    --detach  : launcher returns immediately after injection
  QProcess launcher;
  launcher.setProgram(m_launcherPath);
  launcher.setArguments({
      QStringLiteral("--run-as-admin"),
      QStringLiteral("--port"),
      QStringLiteral("0"),
      QStringLiteral("--detach"),
      m_testAppPath,
  });
  launcher.start();
  QVERIFY2(launcher.waitForFinished(30000), "Launcher did not finish within 30 seconds");
  QCOMPARE(launcher.exitCode(), 0);

  // 3. Wait for UDP discovery announce from the probe (up to 15 seconds)
  QString wsUrl;
  {
    QSignalSpy readySpy(&udp, &QUdpSocket::readyRead);
    bool discovered = false;

    for (int attempt = 0; attempt < 30 && !discovered; ++attempt) {
      if (!udp.hasPendingDatagrams())
        readySpy.wait(500);

      while (udp.hasPendingDatagrams()) {
        QByteArray data;
        data.resize(static_cast<int>(udp.pendingDatagramSize()));
        udp.readDatagram(data.data(), data.size());

        QJsonObject msg = QJsonDocument::fromJson(data).object();
        if (msg.value(QStringLiteral("type")).toString() == QStringLiteral("announce") &&
            msg.value(QStringLiteral("protocol")).toString() == QStringLiteral("qtPilot-discovery")) {
          int wsPort = msg.value(QStringLiteral("wsPort")).toInt();
          m_targetPid = static_cast<qint64>(msg.value(QStringLiteral("pid")).toDouble());
          wsUrl = QStringLiteral("ws://127.0.0.1:%1").arg(wsPort);
          discovered = true;

          qDebug() << "Discovered probe: pid" << m_targetPid << "port" << wsPort;
          break;
        }
      }
    }
    QVERIFY2(discovered, "Did not receive probe discovery broadcast within 15 seconds");
  }

  // 4. Connect to the probe's WebSocket
  QWebSocket ws;
  {
    QSignalSpy connectedSpy(&ws, &QWebSocket::connected);
    ws.open(QUrl(wsUrl));
    QVERIFY2(connectedSpy.wait(10000), "WebSocket did not connect within 10 seconds");
  }

  // 5. Send chr.getPageText and verify a response arrives
  {
    QSignalSpy messageSpy(&ws, &QWebSocket::textMessageReceived);

    QJsonObject request;
    request[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    request[QStringLiteral("id")] = 1;
    request[QStringLiteral("method")] = QStringLiteral("chr.getPageText");
    request[QStringLiteral("params")] = QJsonObject();
    ws.sendTextMessage(QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact)));

    QVERIFY2(messageSpy.wait(10000), "No response received within 10 seconds");

    QString responseStr = messageSpy.first().first().toString();
    QJsonObject response = QJsonDocument::fromJson(responseStr.toUtf8()).object();

    // Must have a result (not an error)
    QVERIFY2(response.contains(QStringLiteral("result")),
             qPrintable(QStringLiteral("Expected result, got: %1").arg(responseStr)));
    QVERIFY(!response.contains(QStringLiteral("error")));

    qDebug() << "chr.getPageText response received successfully";
  }

  // 6. Disconnect WebSocket
  ws.close();
}

void TestAdminInjection::cleanupTestCase() {
  // Kill the target process if it's still running
  if (m_targetPid > 0) {
#ifdef Q_OS_WIN
    HANDLE proc = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(m_targetPid));
    if (proc) {
      TerminateProcess(proc, 0);
      CloseHandle(proc);
    }
#else
    kill(static_cast<pid_t>(m_targetPid), SIGTERM);
#endif
    qDebug() << "Terminated target process" << m_targetPid;
  }
}

QTEST_MAIN(TestAdminInjection)
#include "test_admin_injection.moc"
