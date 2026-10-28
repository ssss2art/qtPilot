// Copyright (c) 2026 qtPilot Contributors
// SPDX-License-Identifier: MIT

// E2E test: the launcher exits cleanly once its target has finished. On Windows it loads the probe
// into itself first, to check the probe's dependencies before injecting it; that load must not
// leave anything behind that its own QCoreApplication trips over on the way out.

#include <QProcess>
#include <QtTest>

class TestLauncherExit : public QObject {
  Q_OBJECT

 private slots:
  void exitsCleanlyAfterTheTarget();
};

void TestLauncherExit::exitsCleanlyAfterTheTarget() {
  const QString shell = qEnvironmentVariable("ComSpec", QStringLiteral("cmd.exe"));

  QProcess launcher;
  launcher.setProcessChannelMode(QProcess::ForwardedChannels);
  launcher.start(QStringLiteral(QTPILOT_TEST_LAUNCHER),
                 {QStringLiteral("--quiet"), QStringLiteral("--port"), QStringLiteral("0"),
                  QStringLiteral("--probe"), QStringLiteral(QTPILOT_TEST_PROBE),
                  QStringLiteral("--qt-dir"), QStringLiteral(QTPILOT_TEST_QT_DIR),
                  QStringLiteral("--"), shell, QStringLiteral("/c"), QStringLiteral("exit 0")});
  QVERIFY2(launcher.waitForFinished(60000), "the launcher did not finish");

  QCOMPARE(launcher.exitStatus(), QProcess::NormalExit);
  // 0xC0000005 here is the launcher crashing on exit, after the target has already run.
  QCOMPARE(QString::number(static_cast<quint32>(launcher.exitCode()), 16), QStringLiteral("0"));
}

QTEST_GUILESS_MAIN(TestLauncherExit)
#include "test_launcher_exit.moc"
