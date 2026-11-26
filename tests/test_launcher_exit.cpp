// Copyright (c) 2026 qtPilot Contributors
// SPDX-License-Identifier: MIT

// E2E test: the launcher exits cleanly once its target has finished and propagates the target's
// exit code. On Windows it loads the probe into itself first, to check the probe's dependencies
// before injecting it; that load must not leave anything behind that its own QCoreApplication trips
// over on the way out.

#include <chrono>

#include <QProcess>
#include <QProcessEnvironment>
#include <QtTest>

namespace {

/// How long one launch may take, target included, before the test gives up on it.
constexpr std::chrono::milliseconds kLauncherTimeout{60000};

}  // namespace

class TestLauncherExit : public QObject {
  Q_OBJECT

 private slots:
  void propagatesTargetExitCode_data();
  void propagatesTargetExitCode();
};

void TestLauncherExit::propagatesTargetExitCode_data() {
  QTest::addColumn<QStringList>("targetArgs");
  QTest::addColumn<int>("expectedExitCode");

  QTest::newRow("zero") << QStringList{} << 0;
  QTest::newRow("non-zero") << QStringList{QStringLiteral("--exit-code"), QStringLiteral("42")}
                            << 42;
}

void TestLauncherExit::propagatesTargetExitCode() {
  QFETCH(QStringList, targetArgs);
  QFETCH(int, expectedExitCode);

  // As a user runs it: the suite's QTPILOT_ENABLED=0 is for this process, not the launcher's.
  QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
  environment.remove(QStringLiteral("QTPILOT_ENABLED"));
  const QString asanOptions = environment.value(QStringLiteral("ASAN_OPTIONS"));
  environment.insert(QStringLiteral("ASAN_OPTIONS"),
                     (asanOptions.isEmpty() ? QString() : asanOptions + QLatin1Char(':')) +
                         QStringLiteral("verify_asan_link_order=0"));

  QStringList arguments = {QStringLiteral("--port"), QStringLiteral("0"), QStringLiteral("--probe"),
                           QStringLiteral(QTPILOT_TEST_PROBE)};
#ifdef QTPILOT_TEST_QT_DIR
  arguments << QStringLiteral("--qt-dir") << QStringLiteral(QTPILOT_TEST_QT_DIR);
#endif
  arguments << QStringLiteral("--") << QStringLiteral(QTPILOT_TEST_EXIT_TARGET) << targetArgs;

  QProcess launcher;
  launcher.setProcessEnvironment(environment);
  launcher.setProcessChannelMode(QProcess::ForwardedChannels);  // the launcher's own diagnostics
  launcher.start(QStringLiteral(QTPILOT_TEST_LAUNCHER), arguments);
  QVERIFY2(launcher.waitForStarted(static_cast<int>(kLauncherTimeout.count())),
           qPrintable(launcher.errorString()));
  if (!launcher.waitForFinished(static_cast<int>(kLauncherTimeout.count()))) {
    launcher.kill();
    launcher.waitForFinished();
    QFAIL("the launcher did not finish");
  }

  QCOMPARE(launcher.exitStatus(), QProcess::NormalExit);
  // 0xC0000005 here is the launcher crashing on exit, after the target has already run.
  QCOMPARE(QString::number(static_cast<quint32>(launcher.exitCode()), 16),
           QString::number(static_cast<quint32>(expectedExitCode), 16));
}

QTEST_GUILESS_MAIN(TestLauncherExit)
#include "test_launcher_exit.moc"
