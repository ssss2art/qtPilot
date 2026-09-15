// Copyright (c) 2024-2026 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "common/qt_matchers.h"

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QtTest>

using namespace qtPilot::test;

class TestQtMatchers : public QObject {
  Q_OBJECT

 private slots:
  // =========================================================================
  // Pretty-Printers
  // =========================================================================
  void testPrettyPrinters() {
    // QString
    QEXPECT_THAT(::testing::PrintToString(QStringLiteral("hello")),
                 ::testing::HasSubstr("\"hello\""));

    // QByteArray
    QEXPECT_THAT(::testing::PrintToString(QByteArray("world")),
                 ::testing::HasSubstr("\"world\""));

    // QPoint
    QEXPECT_THAT(::testing::PrintToString(QPoint(12, 34)),
                 ::testing::HasSubstr("QPoint(12, 34)"));

    // QSize
    QEXPECT_THAT(::testing::PrintToString(QSize(100, 200)),
                 ::testing::HasSubstr("QSize(100, 200)"));

    // QRect
    QEXPECT_THAT(::testing::PrintToString(QRect(5, 10, 50, 60)),
                 ::testing::HasSubstr("QRect(5, 10, 50, 60)"));

    // QColor
    QEXPECT_THAT(::testing::PrintToString(QColor(255, 0, 128, 255)),
                 ::testing::HasSubstr("QColor(255, 0, 128, 255)"));

    // QHostAddress
    QEXPECT_THAT(::testing::PrintToString(QHostAddress(QStringLiteral("127.0.0.1"))),
                 ::testing::HasSubstr("127.0.0.1"));

    // QVariant
    QVariant v(42);
    QEXPECT_THAT(::testing::PrintToString(v), ::testing::HasSubstr("42"));
  }

  // =========================================================================
  // QString Matchers
  // =========================================================================
  void testStringMatchers() {
    QString str = QStringLiteral("qtPilot testing suite");
    QEXPECT_THAT(str, QStrEq("qtPilot testing suite"));
    QEXPECT_THAT(str, QStrNe("other"));
    QEXPECT_THAT(str, QStrContains("testing"));
    QEXPECT_THAT(str, QStrStartsWith("qtPilot"));
    QEXPECT_THAT(str, QStrEndsWith("suite"));

    // QJsonValue strings
    QJsonValue jsonStr(QStringLiteral("json string"));
    QEXPECT_THAT(jsonStr, QStrEq("json string"));
    QEXPECT_THAT(jsonStr, QStrContains("str"));
  }

  // =========================================================================
  // Emptiness Matchers
  // =========================================================================
  void testEmptinessMatchers() {
    QString emptyStr;
    QString nonEmptyStr = QStringLiteral("data");
    QEXPECT_THAT(emptyStr, QIsEmpty());
    QEXPECT_THAT(nonEmptyStr, QIsNotEmpty());

    QJsonObject emptyObj;
    QJsonObject nonEmptyObj{{QStringLiteral("key"), QStringLiteral("val")}};
    QEXPECT_THAT(emptyObj, QIsEmpty());
    QEXPECT_THAT(nonEmptyObj, QIsNotEmpty());

    QJsonArray emptyArr;
    QJsonArray nonEmptyArr{1, 2, 3};
    QEXPECT_THAT(emptyArr, QIsEmpty());
    QEXPECT_THAT(nonEmptyArr, QIsNotEmpty());
  }

  // =========================================================================
  // JSON Object & Field Matchers
  // =========================================================================
  void testJsonFieldMatchers() {
    QJsonObject obj{
        {QStringLiteral("name"), QStringLiteral("probe")},
        {QStringLiteral("count"), 5},
        {QStringLiteral("ratio"), 3.14},
        {QStringLiteral("active"), true},
        {QStringLiteral("meta"), QJsonObject{{QStringLiteral("id"), 101}}},
    };

    QEXPECT_THAT(obj, HasJsonField("name"));
    QEXPECT_THAT(obj, DoesNotHaveJsonField("missing"));
    QEXPECT_THAT(obj, JsonField("name", QStrEq("probe")));
    QEXPECT_THAT(obj, JsonField("count", Eq(5)));
    QEXPECT_THAT(obj, JsonField("count", Gt(3)));
    QEXPECT_THAT(obj, JsonField("ratio", Gt(3.0)));
    QEXPECT_THAT(obj, JsonField("active", QIsTrue()));
    QEXPECT_THAT(obj, JsonField("meta", JsonField("id", Eq(101))));
  }

  // =========================================================================
  // JSON Array Matchers
  // =========================================================================
  void testJsonArrayMatchers() {
    QJsonArray arr{QStringLiteral("apple"), QStringLiteral("banana"), QStringLiteral("cherry")};
    QEXPECT_THAT(arr, JsonArraySize(3));
    QEXPECT_THAT(arr, JsonArrayContains(QStrEq("banana")));
    QEXPECT_THAT(arr, JsonArrayContains(QStrStartsWith("ch")));
  }

  // =========================================================================
  // Domain Matchers: JSON-RPC 2.0 Protocol
  // =========================================================================
  void testJsonRpcSuccessMatchers() {
    QJsonObject resp{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), 42},
        {QStringLiteral("result"), QJsonObject{{QStringLiteral("status"), QStringLiteral("ok")}}},
    };

    QEXPECT_THAT(resp, IsJsonRpcSuccess());
    QEXPECT_THAT(resp, HasJsonRpcId(Eq(42)));
    QEXPECT_THAT(resp, IsJsonRpcSuccess(JsonField("status", QStrEq("ok"))));
  }

  void testJsonRpcErrorMatchers() {
    QJsonObject errResp{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), 1},
        {QStringLiteral("error"),
         QJsonObject{
             {QStringLiteral("code"), -32601},
             {QStringLiteral("message"), QStringLiteral("Method not found")},
         }},
    };

    QEXPECT_THAT(errResp, IsJsonRpcError());
    QEXPECT_THAT(errResp, IsJsonRpcError(-32601));
    QEXPECT_THAT(errResp, IsJsonRpcError(-32601, QStrContains("Method not found")));
  }

  void testJsonRpcNotificationMatchers() {
    QJsonObject notify{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("method"), QStringLiteral("probe.objectCreated")},
        {QStringLiteral("params"), QJsonObject{{QStringLiteral("id"), QStringLiteral("obj1")}}},
    };

    QEXPECT_THAT(notify, IsJsonRpcNotification("probe.objectCreated"));
    QEXPECT_THAT(notify, IsJsonRpcNotification("probe.objectCreated",
                                               JsonField("id", QStrEq("obj1"))));
  }

  // =========================================================================
  // Domain Matchers: Geometry (Native Qt & JSON)
  // =========================================================================
  void testGeometryMatchers() {
    // Native Qt QPoint
    QPoint pt(10, 20);
    QEXPECT_THAT(pt, PointEq(10, 20));
    QEXPECT_THAT(pt, PointEq(QPoint(10, 20)));

    // JSON point
    QJsonObject ptObj{{QStringLiteral("x"), 10}, {QStringLiteral("y"), 20}};
    QEXPECT_THAT(ptObj, PointEq(10, 20));

    // Native Qt QSize
    QSize sz(800, 600);
    QEXPECT_THAT(sz, SizeEq(800, 600));
    QEXPECT_THAT(sz, SizeEq(QSize(800, 600)));

    // JSON size
    QJsonObject szObj{{QStringLiteral("width"), 800}, {QStringLiteral("height"), 600}};
    QEXPECT_THAT(szObj, SizeEq(800, 600));

    // Native Qt QRect
    QRect rect(10, 20, 300, 200);
    QEXPECT_THAT(rect, RectEq(10, 20, 300, 200));
    QEXPECT_THAT(rect, RectEq(QRect(10, 20, 300, 200)));
    QEXPECT_THAT(rect, RectContains(15, 25));
    QEXPECT_THAT(rect, RectContains(QPoint(150, 100)));

    // JSON rect
    QJsonObject rectObj{
        {QStringLiteral("x"), 10},
        {QStringLiteral("y"), 20},
        {QStringLiteral("width"), 300},
        {QStringLiteral("height"), 200},
    };
    QEXPECT_THAT(rectObj, RectEq(10, 20, 300, 200));
  }

  // =========================================================================
  // Domain Matchers: QObject Introspection
  // =========================================================================
  void testQObjectMatchers() {
    QObject obj;
    obj.setObjectName(QStringLiteral("mainController"));
    obj.setProperty("customTag", QStringLiteral("alpha"));
    obj.setProperty("counter", 99);

    QEXPECT_THAT(&obj, HasObjectName("mainController"));
    QEXPECT_THAT(&obj, HasObjectName(QStrStartsWith("main")));
    QEXPECT_THAT(&obj, HasClassName("QObject"));
    QEXPECT_THAT(&obj, HasProperty("customTag", QStrEq("alpha")));
    QEXPECT_THAT(&obj, HasProperty("counter", Eq(99)));

    // JSON property bags
    QJsonObject metaNode{
        {QStringLiteral("className"), QStringLiteral("QPushButton")},
        {QStringLiteral("properties"),
         QJsonObject{
             {QStringLiteral("text"), QStringLiteral("Submit")},
             {QStringLiteral("enabled"), true},
         }},
    };
    QEXPECT_THAT(metaNode, HasProperty("text", QStrEq("Submit")));
    QEXPECT_THAT(metaNode, HasProperty("enabled", QIsTrue()));
  }

  // =========================================================================
  // Domain Matchers: Payloads (PNG & Base64)
  // =========================================================================
  void testPayloadMatchers() {
    // Valid PNG magic bytes (\x89PNG\r\n\x1a\n)
    QByteArray pngData = QByteArray::fromHex("89504e470d0a1a0a0000000d49484452");
    QEXPECT_THAT(pngData, IsValidPng());

    // Base64 PNG string
    QString pngBase64 = QString::fromLatin1(pngData.toBase64());
    QEXPECT_THAT(pngBase64, IsValidPng());
    QEXPECT_THAT(pngBase64, IsValidBase64());

    // Screenshot JSON payload
    QJsonObject screenshotPayload{
        {QStringLiteral("format"), QStringLiteral("png")},
        {QStringLiteral("data"), pngBase64},
    };
    QEXPECT_THAT(screenshotPayload, IsValidPng());
  }
};

QTEST_MAIN(TestQtMatchers)
#include "test_qt_matchers.moc"
