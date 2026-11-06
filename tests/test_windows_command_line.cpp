// Copyright (c) 2026 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "../src/launcher/windows_command_line.h"

#include <QtTest>

class TestWindowsCommandLine : public QObject {
  Q_OBJECT

 private slots:
  void preservesArgumentBoundaries_data();
  void preservesArgumentBoundaries();
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

QTEST_GUILESS_MAIN(TestWindowsCommandLine)
#include "test_windows_command_line.moc"
