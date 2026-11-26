// Copyright (c) 2026 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "../src/launcher/windows_command_line.h"

#include <algorithm>
#include <cstring>

#include <QtTest>

class TestWindowsCommandLine : public QObject {
  Q_OBJECT

 private slots:
  void preservesArgumentBoundaries_data();
  void preservesArgumentBoundaries();
  void quotesTrailingBackslashAndCmdMetacharacters_data();
  void quotesTrailingBackslashAndCmdMetacharacters();
  void buildsElevatedCmdParametersWithQuotingAndEnv();
  void forwardsSecurityEnvVarsAcrossElevation();
};

void TestWindowsCommandLine::preservesArgumentBoundaries_data() {
  QTest::addColumn<QString>("argument");
  QTest::addColumn<QString>("encoded");

  QTest::newRow("empty") << QString() << QStringLiteral(R"("")");
  QTest::newRow("tab") << QStringLiteral("first\tsecond") << QStringLiteral("\"first\tsecond\"");
  QTest::newRow("space and trailing slash")
      << QStringLiteral(R"(a path\)") << QStringLiteral(R"("a path\\")");
  QTest::newRow("slash before quote")
      << QStringLiteral(R"(quoted\"tail\)") << QStringLiteral(R"("quoted\\\"tail\\")");
  QTest::newRow("embedded quote") << QStringLiteral(R"(a"b)") << QStringLiteral(R"("a\"b")");
  QTest::newRow("literal slashes") << QStringLiteral(R"(a\b\\c)") << QStringLiteral(R"("a\b\\c")");
}

void TestWindowsCommandLine::preservesArgumentBoundaries() {
  QFETCH(QString, argument);
  QFETCH(QString, encoded);
  // Exact encodings from the Microsoft C-runtime rules, independently of a local OS parser.
  QCOMPARE(qtPilot::quoteWindowsArgument(argument), encoded);
}

void TestWindowsCommandLine::quotesTrailingBackslashAndCmdMetacharacters_data() {
  QTest::addColumn<QString>("argument");
  QTest::addColumn<QString>("encoded");

  QTest::newRow("trailing backslash")
      << QStringLiteral(R"(C:\Qt\6.8.0\)") << QStringLiteral(R"("C:\Qt\6.8.0\\")");
  QTest::newRow("ampersand metacharacter")
      << QStringLiteral("foo&calc.exe") << QStringLiteral(R"("foo&calc.exe")");
  QTest::newRow("pipe metacharacter")
      << QStringLiteral("foo|bar") << QStringLiteral(R"("foo|bar")");
  QTest::newRow("percent metacharacter")
      << QStringLiteral("%TEMP%\\file") << QStringLiteral(R"("%TEMP%\file")");
}

void TestWindowsCommandLine::quotesTrailingBackslashAndCmdMetacharacters() {
  QFETCH(QString, argument);
  QFETCH(QString, encoded);
  QCOMPARE(qtPilot::quoteWindowsArgument(argument), encoded);
}

void TestWindowsCommandLine::buildsElevatedCmdParametersWithQuotingAndEnv() {
  const QString executable = QStringLiteral(R"(C:\Program Files\qtPilot\qtPilot-launcher.exe)");
  const QStringList args = {
      QStringLiteral("--run-as-admin"),
      QStringLiteral("--qt-dir"),
      QStringLiteral(R"(C:\Qt 6.8\)"),
      QStringLiteral("foo&bar|baz%1"),
  };
  const QList<QPair<QString, QString>> envVars = {
      {QStringLiteral("QT_PLUGIN_PATH"), QStringLiteral(R"(C:\Qt 6.8\plugins;D:\extra&dir)")},
      {QStringLiteral("QTPILOT_AUTH_TOKEN_FILE"), QStringLiteral(R"(C:\tokens\auth"bad.txt)")},
  };

  const QString cmdLine = qtPilot::buildElevatedCmdParameters(executable, args, envVars);

  // --run-as-admin must be replaced by --elevated at the front.
  QVERIFY(!cmdLine.contains(QStringLiteral("--run-as-admin")));
  QVERIFY(cmdLine.startsWith(QStringLiteral("/c \"")));
  QVERIFY(cmdLine.endsWith(QStringLiteral("\"")));

  // Executable and arguments are encoded with quoteWindowsArgument().
  QVERIFY(cmdLine.contains(QStringLiteral(R"("C:\Program Files\qtPilot\qtPilot-launcher.exe")")));
  QVERIFY(cmdLine.contains(QStringLiteral(R"("--elevated")")));
  QVERIFY(cmdLine.contains(QStringLiteral(R"("C:\Qt 6.8\\")")));
  QVERIFY(cmdLine.contains(QStringLiteral(R"("foo&bar|baz%1")")));

  // Env vars are forwarded via `set "VAR=val"` without embedded quotes breaking out.
  QVERIFY(
      cmdLine.contains(QStringLiteral(R"(set "QT_PLUGIN_PATH=C:\Qt 6.8\plugins;D:\extra&dir")")));
  QVERIFY(
      cmdLine.contains(QStringLiteral(R"(set "QTPILOT_AUTH_TOKEN_FILE=C:\tokens\authbad.txt")")));

  // Without env vars, launcher command is still wrapped in outer quotes for cmd.exe /c.
  const QString noEnvCmd = qtPilot::buildElevatedCmdParameters(executable, args, {});
  QCOMPARE(
      noEnvCmd,
      QStringLiteral(
          R"(/c ""C:\Program Files\qtPilot\qtPilot-launcher.exe" "--elevated" "--qt-dir" "C:\Qt 6.8\\" "foo&bar|baz%1"")"));
}

void TestWindowsCommandLine::forwardsSecurityEnvVarsAcrossElevation() {
  const auto containsVar = [](const char* target) {
    return std::any_of(qtPilot::kElevatedEnvVarsToForward.begin(),
                       qtPilot::kElevatedEnvVarsToForward.end(),
                       [target](const char* entry) { return std::strcmp(entry, target) == 0; });
  };

  QVERIFY(containsVar("QTPILOT_PROFILE"));
  QVERIFY(containsVar("QTPILOT_AUTH_TOKEN_FILE"));
  QVERIFY(containsVar("QTPILOT_TLS_CERT"));
  QVERIFY(containsVar("QTPILOT_TLS_KEY"));
  QVERIFY(containsVar("QTPILOT_TLS_CA"));
}

QTEST_GUILESS_MAIN(TestWindowsCommandLine)
#include "test_windows_command_line.moc"
