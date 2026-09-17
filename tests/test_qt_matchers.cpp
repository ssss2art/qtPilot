// Copyright (c) 2024-2026 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "common/qt_matchers.h"

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QRectF>
#include <QTransform>
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
  // Domain Matchers: fractional (qreal) JSON rects
  // =========================================================================
  void testJsonRectEqMatchesFractionalRects() {
    // Graphics-scene geometry is qreal, and any scaled or rotated view makes
    // fractional values the norm rather than the exception.
    const QJsonObject rect{
        {QStringLiteral("x"), 175.5},
        {QStringLiteral("y"), 20.25},
        {QStringLiteral("width"), 50.5},
        {QStringLiteral("height"), 25.125},
    };

    QEXPECT_THAT(rect, JsonRectEq(175.5, 20.25, 50.5, 25.125));
    QEXPECT_THAT(rect, JsonRectEq(QRectF(175.5, 20.25, 50.5, 25.125)));
    QEXPECT_THAT(rect, JsonRectSize(50.5, 25.125));

    // Whole numbers still read correctly -- JSON has one number type, so an
    // integral value arrives as a double either way.
    const QJsonObject whole{
        {QStringLiteral("x"), 10},
        {QStringLiteral("y"), 20},
        {QStringLiteral("width"), 300},
        {QStringLiteral("height"), 200},
    };
    QEXPECT_THAT(whole, JsonRectEq(10, 20, 300, 200));
  }

  void testJsonRectEqRejectsWrongValues() {
    // A matcher that cannot fail is worse than no matcher, so pin the negative.
    const QJsonObject rect{
        {QStringLiteral("x"), 175.0},
        {QStringLiteral("y"), 175.0},
        {QStringLiteral("width"), 50.0},
        {QStringLiteral("height"), 25.0},
    };

    // One pixel wide is exactly the drift that integer polygon corners produce.
    QEXPECT_THAT(rect, ::testing::Not(JsonRectEq(175.0, 175.0, 51.0, 25.0)));
    QEXPECT_THAT(rect, ::testing::Not(JsonRectEq(0.0, 175.0, 50.0, 25.0)));
    QEXPECT_THAT(rect, ::testing::Not(JsonRectSize(51.0, 25.0)));

    // Not a rect at all, and a rect missing a key, are both mismatches rather
    // than a crash or a default-zero pass.
    QEXPECT_THAT(QJsonValue(42), ::testing::Not(JsonRectEq(0.0, 0.0, 0.0, 0.0)));

    // Named local, not an inline literal: the preprocessor splits macro
    // arguments on commas inside braces, so QJsonObject{{...}, {...}} as a
    // macro argument does not compile.
    const QJsonObject partial{{QStringLiteral("x"), 1.0}};
    QEXPECT_THAT(partial, ::testing::Not(JsonRectEq(1.0, 0.0, 0.0, 0.0)));
  }

  void testJsonRectEqToleratesTransformRoundingButNotRealDrift() {
    // Rotating a rect through QTransform rarely lands on exact binary values,
    // so the matcher allows a hair of floating-point slop...
    const QJsonObject rotated{
        {QStringLiteral("x"), 279.99999999999994},
        {QStringLiteral("y"), 300.0},
        {QStringLiteral("width"), 20.000000000000004},
        {QStringLiteral("height"), 40.0},
    };
    QEXPECT_THAT(rotated, JsonRectEq(280.0, 300.0, 20.0, 40.0));

    // ...while still catching a genuine off-by-one.
    QEXPECT_THAT(rotated, ::testing::Not(JsonRectEq(280.0, 300.0, 21.0, 40.0)));
  }

  // RectEq is int-valued and reads JSON with toInt(), which yields 0 for any
  // non-integral number. Pinned so nobody reaches for it on scene geometry and
  // gets a silent all-zero comparison instead of a failure.
  void testRectEqIsIntegerOnlyAndNotForSceneGeometry() {
    const QJsonObject fractional{
        {QStringLiteral("x"), 175.5},
        {QStringLiteral("y"), 20.25},
        {QStringLiteral("width"), 50.5},
        {QStringLiteral("height"), 25.125},
    };

    // Every field reads back as 0, so this fractional rect "equals" the origin.
    QEXPECT_THAT(fractional, RectEq(0, 0, 0, 0));
    QEXPECT_THAT(fractional, ::testing::Not(RectEq(175, 20, 50, 25)));

    // JsonRectEq is the matcher that tells the truth about the same object.
    QEXPECT_THAT(fractional, JsonRectEq(175.5, 20.25, 50.5, 25.125));
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

QTEST_GUILESS_MAIN(TestQtMatchers)
#include "test_qt_matchers.moc"
