// Copyright (c) 2026 qtPilot Contributors
// SPDX-License-Identifier: MIT
#include "common/qt_matchers.h"
#include "transport/network_policy.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

using namespace qtPilot;
using namespace qtPilot::test;

MATCHER_P3(HasNetworkBoundary, exposure, discovery, secure, "has the specified network boundary") {
  if (!arg.has_value()) {
    *result_listener << "configuration rejected: " << arg.error().toStdString();
    return false;
  }
  return ExplainMatchResult(Eq(exposure), arg->exposure, result_listener) &&
         ExplainMatchResult(Eq(discovery), arg->discoveryEnabled, result_listener) &&
         ExplainMatchResult(Eq(secure), arg->tlsEnabled(), result_listener);
}

class TestNetworkPolicy : public QObject {
  Q_OBJECT
 private slots:
  void policy_data() {
    QTest::addColumn<QString>("settings");
    QTest::addColumn<bool>("accepted");
    QTest::addColumn<bool>("lan");
    QTest::addColumn<bool>("discovery");
    QTest::addColumn<bool>("tls");
    QTest::newRow("legacy LAN") << QStringLiteral("{}") << true << true << true << false;
    QTest::newRow("legacy loopback still announces")
        << QStringLiteral(R"({"QTPILOT_BIND_ADDRESS":"loopback"})") << true << false << true
        << false;
    QTest::newRow("legacy typo still restricts")
        << QStringLiteral(R"({"QTPILOT_BIND_ADDRESS":"loopbak"})") << true << false << true
        << false;
    QTest::newRow("explicit local disables announcements")
        << QStringLiteral(R"({"QTPILOT_PROFILE":"local"})") << true << false << false << false;
    QTest::newRow("local conflict cannot widen")
        << QStringLiteral(R"({"QTPILOT_PROFILE":"local","QTPILOT_BIND_ADDRESS":"lan"})") << false
        << false << false << false;
    QTest::newRow("invalid profile cannot become legacy")
        << QStringLiteral(R"({"QTPILOT_PROFILE":"locla"})") << false << false << false << false;
    QTest::newRow("authenticated LAN requires encryption")
        << QStringLiteral(R"({"QTPILOT_AUTH_TOKEN_FILE":"token"})") << false << false << false
        << false;
    QTest::newRow("local token can use plaintext")
        << QStringLiteral(R"({"QTPILOT_PROFILE":"local","QTPILOT_AUTH_TOKEN_FILE":"token"})")
        << true << false << false << false;
    QTest::newRow("trusted network cannot omit auth")
        << QStringLiteral(R"({"QTPILOT_PROFILE":"trusted-network"})") << false << false << false
        << false;
    QTest::newRow("remote cannot omit TLS")
        << QStringLiteral(R"({"QTPILOT_PROFILE":"remote","QTPILOT_AUTH_TOKEN_FILE":"token"})")
        << false << false << false << false;
    QTest::newRow("trusted network retains LAN discovery")
        << QStringLiteral(
               R"({"QTPILOT_PROFILE":"trusted-network","QTPILOT_AUTH_TOKEN_FILE":"token","QTPILOT_TLS_CERT_FILE":"cert","QTPILOT_TLS_KEY_FILE":"key"})")
        << true << true << true << true;
    QTest::newRow("remote does not expose automatically")
        << QStringLiteral(
               R"({"QTPILOT_PROFILE":"remote","QTPILOT_AUTH_TOKEN_FILE":"token","QTPILOT_TLS_CERT_FILE":"cert","QTPILOT_TLS_KEY_FILE":"key"})")
        << true << false << false << true;
    QTest::newRow("remote permits explicit LAN bind")
        << QStringLiteral(
               R"({"QTPILOT_PROFILE":"remote","QTPILOT_BIND_ADDRESS":"lan","QTPILOT_AUTH_TOKEN_FILE":"token","QTPILOT_TLS_CERT_FILE":"cert","QTPILOT_TLS_KEY_FILE":"key"})")
        << true << true << false << true;
    QTest::newRow("partial TLS refuses startup")
        << QStringLiteral(R"({"QTPILOT_TLS_CERT_FILE":"cert"})") << false << false << false
        << false;
    QTest::newRow("explicit empty token file is an error")
        << QStringLiteral(R"({"QTPILOT_AUTH_TOKEN_FILE":""})") << false << false << false << false;
    QTest::newRow("profile rejects bind typo")
        << QStringLiteral(R"({"QTPILOT_PROFILE":"local","QTPILOT_BIND_ADDRESS":"loopbak"})")
        << false << false << false << false;
    QTest::newRow("trusted-network cannot narrow discovery invisibly")
        << QStringLiteral(
               R"({"QTPILOT_PROFILE":"trusted-network","QTPILOT_BIND_ADDRESS":"loopback","QTPILOT_AUTH_TOKEN_FILE":"token","QTPILOT_TLS_CERT_FILE":"cert","QTPILOT_TLS_KEY_FILE":"key"})")
        << false << false << false << false;
  }

  void policy() {
    QFETCH(QString, settings);
    QFETCH(bool, accepted);
    QFETCH(bool, lan);
    QFETCH(bool, discovery);
    QFETCH(bool, tls);
    const auto fields = QJsonDocument::fromJson(settings.toUtf8()).object();
    QProcessEnvironment environment;
    for (auto it = fields.begin(); it != fields.end(); ++it) {
      environment.insert(it.key(), it.value().toString());
    }
    const auto result = resolveNetworkPolicy(environment);
    if (accepted) {
      QEXPECT_THAT(result,
                   HasNetworkBoundary(lan ? NetworkExposure::Lan : NetworkExposure::Loopback,
                                      discovery, tls));
      QEXPECT_THAT(result->authenticationRequired(),
                   Eq(environment.contains(QStringLiteral("QTPILOT_AUTH_TOKEN_FILE"))));
    } else {
      QEXPECT_THAT(result.has_value(), IsFalse());
      QVERIFY(!result.error().isEmpty());
    }
  }
};
QTEST_GUILESS_MAIN(TestNetworkPolicy)
#include "test_network_policy.moc"
