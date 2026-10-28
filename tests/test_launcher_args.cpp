// Copyright (c) 2026 qtPilot Contributors
// SPDX-License-Identifier: MIT

// E2E test: the launcher hands the target every argument after "--" exactly as given, including
// the ones Qt reserves for itself. The launcher is a Qt application, and QCoreApplication removes
// -qmljsdebugger from argv wherever it appears, so a target launched for qmlpreview or the QML
// debugger used to start without the argument that connects it back.

#include "common/qt_matchers.h"

#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

using namespace qtPilot::test;

class TestLauncherArgs : public QObject {
  Q_OBJECT

 private slots:
  void forwardsArgumentsAfterSeparator_data();
  void forwardsArgumentsAfterSeparator();

 private:
  /// Runs the target through the launcher and returns what the target received.
  QStringList runThroughLauncher(const QStringList& targetArgs);

  QTemporaryDir m_dir;
};

QStringList TestLauncherArgs::runThroughLauncher(const QStringList& targetArgs) {
  const QString outPath = m_dir.filePath(QStringLiteral("args.txt"));
  QFile::remove(outPath);

  QStringList arguments{QStringLiteral("--quiet"), QStringLiteral("--port"), QStringLiteral("0"),
                        QStringLiteral("--probe"), QStringLiteral(QTPILOT_TEST_PROBE)};
#ifdef QTPILOT_TEST_QT_DIR
  arguments << QStringLiteral("--qt-dir") << QStringLiteral(QTPILOT_TEST_QT_DIR);
#endif
  arguments << QStringLiteral("--") << QStringLiteral(QTPILOT_TEST_ARGS_ECHO) << targetArgs;

  QProcess launcher;
  launcher.setWorkingDirectory(m_dir.path());  // the target inherits it and writes args.txt there
  launcher.setProcessChannelMode(QProcess::ForwardedChannels);
  launcher.start(QStringLiteral(QTPILOT_TEST_LAUNCHER), arguments);
  // Only what the target received is judged here: the launcher waits for the target, so once it
  // has finished, args.txt is complete whatever the launcher's own exit status.
  if (!launcher.waitForFinished(60000)) {
    launcher.kill();
    return {QStringLiteral("<launcher did not finish>")};
  }

  QFile out(outPath);
  if (!out.open(QIODevice::ReadOnly))
    return {QStringLiteral("<target wrote nothing>")};
  return QString::fromLocal8Bit(out.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

void TestLauncherArgs::forwardsArgumentsAfterSeparator_data() {
  QTest::addColumn<QStringList>("targetArgs");

  QTest::newRow("plain") << QStringList{QStringLiteral("--verbose"), QStringLiteral("project.dv")};
  // What qmlpreview passes: Qt's own switch, with the comma-separated services list.
  QTest::newRow("qmljsdebugger") << QStringList{
      QStringLiteral("-qmljsdebugger=file:preview.socket,block,services:QmlPreview"),
      QStringLiteral("project.dv")};
  QTest::newRow("qmljsdebugger with two dashes")
      << QStringList{QStringLiteral("--qmljsdebugger=port:3768,block")};
  QTest::newRow("launcher option names reach the target")
      << QStringList{QStringLiteral("--port"), QStringLiteral("9")};
}

void TestLauncherArgs::forwardsArgumentsAfterSeparator() {
  QFETCH(QStringList, targetArgs);
  QVERIFY(m_dir.isValid());

  QEXPECT_THAT(runThroughLauncher(targetArgs), Eq(targetArgs));
}

QTEST_GUILESS_MAIN(TestLauncherArgs)
#include "test_launcher_args.moc"
