// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "core/version.h"

#include <QtTest>

using namespace qtPilot;

/// The probe reported a hardcoded "0.1.0" from two handlers while the project
/// was at 0.3.1. These tests pin the properties that made that possible: the
/// version has to come from the build, and it has to be the same value
/// everywhere it is reported.
class TestProbeVersion : public QObject {
  Q_OBJECT

 private slots:
  /// QTPILOT_EXPECTED_VERSION is passed from CMake's PROJECT_VERSION, so this
  /// fails if the generated header ever stops tracking the project.
  void versionMatchesProjectVersion() {
    QCOMPARE(QString::fromUtf8(kVersion), QStringLiteral(QTPILOT_EXPECTED_VERSION));
  }

  void versionIsNotTheOldHardcodedValue() {
    QVERIFY2(QString::fromUtf8(kVersion) != QStringLiteral("0.1.0"),
             "kVersion is the stale literal the generated header exists to remove");
  }

  void versionIsNonEmptyAndDotted() {
    const QString v = QString::fromUtf8(kVersion);
    QVERIFY(!v.isEmpty());
    QVERIFY2(v.contains(QLatin1Char('.')), qPrintable("not a dotted version: " + v));
  }

  /// Pinned so that bumping it is a deliberate edit with a failing test to
  /// update, rather than something that drifts silently away from the Python
  /// client's SUPPORTED_PROTOCOL_VERSION.
  void protocolVersionIsPinned() { QCOMPARE(kProtocolVersion, 1); }
};

QTEST_MAIN(TestProbeVersion)
#include "test_probe_version.moc"
