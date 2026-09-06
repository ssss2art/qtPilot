// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "transport/bind_policy.h"

#include <QtTest>

using namespace qtPilot;

/// Tests for the network exposure policy.
///
/// Two properties are being pinned, and they pull in opposite directions:
///
///   1. The default must reach the network. Driving instrumented applications
///      on other hosts is the product, and discovery is how they are found, so
///      a default of Loopback would be a silent outage rather than a hardening.
///   2. An explicit restriction must never be widened. Anyone who sets this
///      variable is trying to narrow the probe, so an unrecognised value falls
///      back to Loopback rather than to the Lan default -- a visible refused
///      connection beats silently ignoring the operator's intent.
class TestBindPolicy : public QObject {
  Q_OBJECT

 private slots:
  void init() { qunsetenv("QTPILOT_BIND_ADDRESS"); }
  void cleanup() { qunsetenv("QTPILOT_BIND_ADDRESS"); }

  /// The core requirement: unconfigured means reachable across the network.
  void defaultsToLanWhenUnset() {
    QCOMPARE(configuredExposure(), NetworkExposure::Lan);
    QCOMPARE(listenAddress(), QHostAddress(QHostAddress::Any));
  }

  /// An empty value is "unset", not "invalid" -- shells export empty strings
  /// readily, and that must not quietly cut the probe off the network.
  void emptyValueIsTreatedAsUnset() {
    qputenv("QTPILOT_BIND_ADDRESS", QByteArray(""));
    QCOMPARE(configuredExposure(), NetworkExposure::Lan);
    QCOMPARE(listenAddress(), QHostAddress(QHostAddress::Any));
  }

  void lanSpellings_data() {
    QTest::addColumn<QByteArray>("value");
    QTest::newRow("any") << QByteArray("any");
    QTest::newRow("all") << QByteArray("all");
    QTest::newRow("lan") << QByteArray("lan");
    QTest::newRow("wildcard addr") << QByteArray("0.0.0.0");
    QTest::newRow("star") << QByteArray("*");
    QTest::newRow("mixed case") << QByteArray("AnY");
    QTest::newRow("surrounding space") << QByteArray("  any  ");
  }

  void lanSpellings() {
    QFETCH(QByteArray, value);
    qputenv("QTPILOT_BIND_ADDRESS", value);
    QCOMPARE(configuredExposure(), NetworkExposure::Lan);
    QCOMPARE(listenAddress(), QHostAddress(QHostAddress::Any));
  }

  void loopbackSpellings_data() {
    QTest::addColumn<QByteArray>("value");
    QTest::newRow("word") << QByteArray("loopback");
    QTest::newRow("localhost") << QByteArray("localhost");
    QTest::newRow("v4 literal") << QByteArray("127.0.0.1");
    QTest::newRow("v6 literal") << QByteArray("::1");
    QTest::newRow("mixed case") << QByteArray("LoopBack");
    QTest::newRow("surrounding space") << QByteArray("  loopback  ");
  }

  void loopbackSpellings() {
    QFETCH(QByteArray, value);
    qputenv("QTPILOT_BIND_ADDRESS", value);
    QCOMPARE(configuredExposure(), NetworkExposure::Loopback);
    QCOMPARE(listenAddress(), QHostAddress(QHostAddress::LocalHost));
  }

  /// A typo must not be resolved as "wide open". This is the one case that
  /// deliberately does NOT fall back to the default.
  void unrecognisedValuesRestrictRatherThanWiden_data() {
    QTest::addColumn<QByteArray>("value");
    QTest::newRow("typo of loopback") << QByteArray("loopbak");
    QTest::newRow("typo of any") << QByteArray("anyy");
    QTest::newRow("truthy") << QByteArray("true");
    QTest::newRow("numeric") << QByteArray("1");
    QTest::newRow("no") << QByteArray("no");
    QTest::newRow("specific host") << QByteArray("192.168.1.10");
    QTest::newRow("garbage") << QByteArray("!!!");
  }

  void unrecognisedValuesRestrictRatherThanWiden() {
    QFETCH(QByteArray, value);
    qputenv("QTPILOT_BIND_ADDRESS", value);
    QCOMPARE(configuredExposure(), NetworkExposure::Loopback);
    QCOMPARE(listenAddress(), QHostAddress(QHostAddress::LocalHost));
  }

  /// Discovery is how remote instances are found, so a LAN-bound probe has to
  /// broadcast. A loopback-bound one must not: advertising a process nobody can
  /// connect to is noise that also discloses it.
  void announceFollowsExposure() {
    QCOMPARE(announceAddress(), QHostAddress(QHostAddress::Broadcast));
    qputenv("QTPILOT_BIND_ADDRESS", QByteArray("loopback"));
    QCOMPARE(announceAddress(), QHostAddress(QHostAddress::LocalHost));
  }
};

QTEST_MAIN(TestBindPolicy)
#include "test_bind_policy.moc"
