// Copyright (c) 2026 qtPilot Contributors
// SPDX-License-Identifier: MIT

// E2E test: the launcher hands the target every argument after "--" exactly as given, including
// the ones Qt reserves for itself. The launcher is a Qt application, and QCoreApplication removes
// -qmljsdebugger from argv wherever it appears, so a target launched for qmlpreview or the QML
// debugger used to start without the argument that connects it back.

#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

namespace {

/// How long one launch may take, target included, before the test gives up on it.
constexpr int kLauncherTimeoutMs = 60000;

/// Stands for the target executable in a row's command line.
const QString kTarget = QStringLiteral("<target>");

}  // namespace

class TestLauncherArgs : public QObject {
  Q_OBJECT

 private slots:
  void forwardsArgumentsAfterSeparator_data();
  void forwardsArgumentsAfterSeparator();

 private:
  /// Runs the launcher with its usual options followed by @p command (kTarget naming the target)
  /// and returns what the target received, or a <...> description of why it received nothing.
  QStringList runThroughLauncher(const QStringList& command);

  QTemporaryDir m_dir;
};

QStringList TestLauncherArgs::runThroughLauncher(const QStringList& command) {
  const QString outPath = m_dir.filePath(QStringLiteral("args.txt"));
  QFile::remove(outPath);

  QStringList arguments = {QStringLiteral("--port"), QStringLiteral("0"), QStringLiteral("--probe"),
                           QStringLiteral(QTPILOT_TEST_PROBE)};
#ifdef QTPILOT_TEST_QT_DIR
  arguments << QStringLiteral("--qt-dir") << QStringLiteral(QTPILOT_TEST_QT_DIR);
#endif
  for (const QString& word : command)
    arguments << (word == kTarget ? QStringLiteral(QTPILOT_TEST_ARGS_ECHO) : word);

  QProcess launcher;
  launcher.setWorkingDirectory(m_dir.path());  // the target inherits it and writes args.txt there
  launcher.setProcessChannelMode(QProcess::ForwardedChannels);  // the launcher's own diagnostics
  launcher.start(QStringLiteral(QTPILOT_TEST_LAUNCHER), arguments);
  if (!launcher.waitForStarted(kLauncherTimeoutMs))
    return {QStringLiteral("<launcher did not start: %1>").arg(launcher.errorString())};
  if (!launcher.waitForFinished(kLauncherTimeoutMs)) {
    launcher.kill();
    launcher.waitForFinished();
    return {QStringLiteral("<launcher did not finish>")};
  }

  // Only what the target received is judged here: the launcher waits for the target, so once it
  // has finished, args.txt is complete whatever the launcher's own exit status.
  QFile out(outPath);
  if (!out.open(QIODevice::ReadOnly)) {
    return {QStringLiteral("<target wrote nothing; launcher exit code %1, status %2>")
                .arg(launcher.exitCode())
                .arg(static_cast<int>(launcher.exitStatus()))};
  }
  return QString::fromUtf8(out.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

void TestLauncherArgs::forwardsArgumentsAfterSeparator_data() {
  QTest::addColumn<QStringList>("command");
  QTest::addColumn<QStringList>("received");

  const QString separator = QStringLiteral("--");
  const QString qmlPreview =
      QStringLiteral("-qmljsdebugger=file:preview.socket,block,services:QmlPreview");

  QTest::newRow("plain") << QStringList{separator, kTarget, QStringLiteral("--verbose"),
                                        QStringLiteral("project.dv")}
                         << QStringList{QStringLiteral("--verbose"), QStringLiteral("project.dv")};
  // What qmlpreview passes: Qt's own switch, with the comma-separated services list.
  QTest::newRow("qmljsdebugger") << QStringList{separator, kTarget, qmlPreview,
                                                QStringLiteral("project.dv")}
                                 << QStringList{qmlPreview, QStringLiteral("project.dv")};
  QTest::newRow("qmljsdebugger with two dashes")
      << QStringList{separator, kTarget, QStringLiteral("--qmljsdebugger=port:3768,block")}
      << QStringList{QStringLiteral("--qmljsdebugger=port:3768,block")};
  QTest::newRow("launcher option names reach the target")
      << QStringList{separator, kTarget, QStringLiteral("--port"), QStringLiteral("9")}
      << QStringList{QStringLiteral("--port"), QStringLiteral("9")};
  // Outside any ANSI code page: on Windows the narrow argv cannot carry it. Escaped, so the
  // source file's encoding cannot change what is tested.
  QTest::newRow("non-ASCII") << QStringList{separator, kTarget, QStringLiteral(u"\u9879\u76EE.dv")}
                             << QStringList{QStringLiteral(u"\u9879\u76EE.dv")};
  // The "--" is not always the separator before the target. Named before it, the target stays the
  // target, and only what follows the "--" is its arguments.
  QTest::newRow("target before the separator")
      << QStringList{kTarget, separator, QStringLiteral("project.dv")}
      << QStringList{QStringLiteral("project.dv")};
  // An option's value that happens to be "--" is not the separator either.
  QTest::newRow("\"--\" as an option's value")
      << QStringList{QStringLiteral("--auth-token-file"), separator, separator, kTarget, qmlPreview}
      << QStringList{qmlPreview};
}

void TestLauncherArgs::forwardsArgumentsAfterSeparator() {
  QFETCH(QStringList, command);
  QFETCH(QStringList, received);
  QVERIFY(m_dir.isValid());

  // QCOMPARE rather than a gmock matcher: printing a QStringList through gmock instantiates
  // stdext::checked_array_iterator with Qt 5 on MSVC, which is deprecated (STL4043).
  QCOMPARE(runThroughLauncher(command), received);
}

QTEST_GUILESS_MAIN(TestLauncherArgs)
#include "test_launcher_args.moc"
