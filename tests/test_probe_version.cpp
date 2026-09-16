// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "core/version.h"

#include <QtTest>

#include "common/qt_matchers.h"

using namespace qtPilot;
using namespace qtPilot::test;

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
    QEXPECT_THAT(QString::fromUtf8(kVersion), QStrEq(QTPILOT_EXPECTED_VERSION));
  }

  void versionIsNotTheOldHardcodedValue() {
    QEXPECT_THAT(QString::fromUtf8(kVersion), Not(QStrEq("0.1.0")));
  }

  void versionIsNonEmptyAndDotted() {
    const QString v = QString::fromUtf8(kVersion);
    QEXPECT_THAT(v, AllOf(QIsNotEmpty(), QStrContains(".")));
  }

  /// Pinned so that bumping it is a deliberate edit with a failing test to
  /// update, rather than something that drifts silently away from the Python
  /// client's SUPPORTED_PROTOCOL_VERSION.
  void protocolVersionIsPinned() {
    QEXPECT_THAT(kProtocolVersion, Eq(1));
  }
};

QTEST_MAIN(TestProbeVersion)
#include "test_probe_version.moc"
