// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "api/computer_use_mode_api.h"
#include "api/error_codes.h"
#include "core/object_registry.h"
#include "core/object_resolver.h"
#include "transport/jsonrpc_handler.h"

#include <QApplication>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalSpy>
#include <QVBoxLayout>
#include <QtTest>

using namespace qtPilot;

/// @brief Integration tests for the Computer Use Mode API (cu.* methods).
///
/// Tests all 13 cu.* JSON-RPC methods end-to-end through the JSON-RPC handler:
/// screenshot, click, rightClick, middleClick, doubleClick, mouseMove, drag,
/// mouseDown, mouseUp, type, key, scroll, cursorPosition.
///
/// Also verifies error handling for out-of-bounds coordinates, no focused widget,
/// and include_screenshot option.
class TestComputerUseApi : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase();
  void cleanupTestCase();
  void init();
  void cleanup();

  // CU-01: Screenshot
  void testScreenshot();

  // CU-02 through CU-04: Clicks
  void testClick();
  void testRightClick();
  void testMiddleClick();
  void testDoubleClick();

  // CU-05: Mouse move
  void testMouseMove();

  // CU-06: Drag
  void testDrag();

  // CU-07: Mouse down/up
  void testMouseDown();
  void testMouseUp();

  // CU-08: Type
  void testType();

  // CU-09: Key
  void testKey();
  void testKeyChromeNames();

  // CU-10: Scroll
  void testScroll();

  // CU-11: Cursor position
  void testCursorPosition();

  // Error handling
  void testClickOutOfBounds();
  void testTypeNoFocusedWidget();
  void testIncludeScreenshot();

 private:
  /// @brief Make a JSON-RPC call and return the full parsed response object.
  QJsonObject callRaw(const QString& method, const QJsonObject& params);

  /// @brief Make a JSON-RPC call and return the envelope result (unwrapped from JSON-RPC).
  QJsonObject callEnvelope(const QString& method, const QJsonObject& params);

  /// @brief Make a JSON-RPC call and return the inner result value from the envelope.
  QJsonValue callResult(const QString& method, const QJsonObject& params);

  /// @brief Make a JSON-RPC call expecting an error, return the error object.
  QJsonObject callExpectError(const QString& method, const QJsonObject& params);

  JsonRpcHandler* m_handler = nullptr;
  ComputerUseModeApi* m_api = nullptr;
  QMainWindow* m_testWindow = nullptr;
  QPushButton* m_testButton = nullptr;
  QLineEdit* m_testLineEdit = nullptr;
  QScrollArea* m_scrollArea = nullptr;
  int m_requestId = 1;
};

void TestComputerUseApi::initTestCase() {
  installObjectHooks();
}

void TestComputerUseApi::cleanupTestCase() {
  uninstallObjectHooks();
}

void TestComputerUseApi::init() {
  m_handler = new JsonRpcHandler(this);
  m_api = new ComputerUseModeApi(m_handler, this);

  // Create test widget tree
  m_testWindow = new QMainWindow();
  m_testWindow->setObjectName("cuTestWindow");
  m_testWindow->setFixedSize(400, 300);

  QWidget* central = new QWidget(m_testWindow);
  QVBoxLayout* layout = new QVBoxLayout(central);

  m_testButton = new QPushButton("clickMe", central);
  m_testButton->setObjectName("cuClickBtn");
  m_testButton->setFixedSize(100, 30);
  layout->addWidget(m_testButton);

  m_testLineEdit = new QLineEdit(central);
  m_testLineEdit->setObjectName("cuInputField");
  m_testLineEdit->setFixedSize(200, 30);
  layout->addWidget(m_testLineEdit);

  m_scrollArea = new QScrollArea(central);
  m_scrollArea->setObjectName("cuScrollArea");
  m_scrollArea->setFixedSize(200, 100);
  QWidget* scrollContent = new QWidget();
  scrollContent->setMinimumSize(400, 400);
  m_scrollArea->setWidget(scrollContent);
  layout->addWidget(m_scrollArea);

  m_testWindow->setCentralWidget(central);
  m_testWindow->show();
  QApplication::processEvents();
}

void TestComputerUseApi::cleanup() {
  ObjectResolver::clearNumericIds();

  delete m_testWindow;
  m_testWindow = nullptr;
  m_testButton = nullptr;
  m_testLineEdit = nullptr;
  m_scrollArea = nullptr;

  delete m_api;
  m_api = nullptr;
  delete m_handler;
  m_handler = nullptr;
}

QJsonObject TestComputerUseApi::callRaw(const QString& method, const QJsonObject& params) {
  QJsonObject request;
  request["jsonrpc"] = "2.0";
  request["method"] = method;
  request["params"] = params;
  request["id"] = m_requestId++;

  QString requestStr = QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact));
  QString responseStr = m_handler->HandleMessage(requestStr);

  return QJsonDocument::fromJson(responseStr.toUtf8()).object();
}

QJsonObject TestComputerUseApi::callEnvelope(const QString& method, const QJsonObject& params) {
  QJsonObject response = callRaw(method, params);
  QJsonValue resultVal = response["result"];
  if (resultVal.isObject()) {
    return resultVal.toObject();
  }
  return QJsonObject();
}

QJsonValue TestComputerUseApi::callResult(const QString& method, const QJsonObject& params) {
  QJsonObject envelope = callEnvelope(method, params);
  return envelope["result"];
}

QJsonObject TestComputerUseApi::callExpectError(const QString& method, const QJsonObject& params) {
  QJsonObject response = callRaw(method, params);
  return response["error"].toObject();
}

// ========================================================================
// CU-01: Screenshot
// ========================================================================

void TestComputerUseApi::testScreenshot() {
  // On minimal/headless platforms or without screen capture permission,
  // grabWindow() returns a null pixmap. The probe now throws (returning a
  // JSON-RPC error) instead of crashing. Accept either outcome.
  QJsonObject response = callRaw("cu.screenshot", QJsonObject());

  if (response.contains("error")) {
    // Error response — expected on minimal platform / no screen capture permission
    QJsonObject error = response["error"].toObject();
    QVERIFY(error.contains("message"));
    QString msg = error["message"].toString();
    QVERIFY2(msg.contains("null pixmap") || msg.contains("screen capture"),
             qPrintable("Unexpected error: " + msg));
    qWarning("Screenshot returned error (expected on minimal platform): %s", qPrintable(msg));
    return;
  }

  // Success path
  QJsonValue resultVal = response["result"];
  QVERIFY(resultVal.isObject());

  QJsonObject envelope = resultVal.toObject();
  QJsonValue innerResult = envelope["result"];
  QVERIFY(innerResult.isObject());

  QJsonObject obj = innerResult.toObject();

  // Must have image, width, height keys in response
  QVERIFY(obj.contains("image"));
  QVERIFY(obj.contains("width"));
  QVERIFY(obj.contains("height"));

  QString image = obj["image"].toString();
  if (!image.isEmpty()) {
    QVERIFY(obj["width"].toInt() > 0);
    QVERIFY(obj["height"].toInt() > 0);

    // Decode base64 and verify PNG magic bytes
    QByteArray decoded = QByteArray::fromBase64(image.toLatin1());
    QVERIFY(decoded.size() >= 4);
    QCOMPARE(static_cast<unsigned char>(decoded[0]), static_cast<unsigned char>(0x89));
    QCOMPARE(static_cast<unsigned char>(decoded[1]), static_cast<unsigned char>(0x50));
    QCOMPARE(static_cast<unsigned char>(decoded[2]), static_cast<unsigned char>(0x4E));
    QCOMPARE(static_cast<unsigned char>(decoded[3]), static_cast<unsigned char>(0x47));
  } else {
    qWarning("Screenshot returned empty image (expected on minimal platform)");
  }
}

// ========================================================================
// CU-02 through CU-04: Click variants
// ========================================================================

void TestComputerUseApi::testClick() {
  // Get button position relative to window
  QPoint btnCenter = m_testButton->mapTo(m_testWindow, m_testButton->rect().center());

  bool clicked = false;
  connect(m_testButton, &QPushButton::clicked, this, [&clicked]() { clicked = true; });

  QJsonValue result =
      callResult("cu.click", QJsonObject{{"x", btnCenter.x()}, {"y", btnCenter.y()}});
  QApplication::processEvents();

  QVERIFY(result.isObject());
  QCOMPARE(result.toObject()["success"].toBool(), true);
  QVERIFY(clicked);
}

void TestComputerUseApi::testRightClick() {
  QPoint btnCenter = m_testButton->mapTo(m_testWindow, m_testButton->rect().center());

  QJsonValue result =
      callResult("cu.rightClick", QJsonObject{{"x", btnCenter.x()}, {"y", btnCenter.y()}});
  QApplication::processEvents();

  QVERIFY(result.isObject());
  QCOMPARE(result.toObject()["success"].toBool(), true);
}

void TestComputerUseApi::testMiddleClick() {
  QPoint btnCenter = m_testButton->mapTo(m_testWindow, m_testButton->rect().center());

  QJsonValue result =
      callResult("cu.middleClick", QJsonObject{{"x", btnCenter.x()}, {"y", btnCenter.y()}});
  QApplication::processEvents();

  QVERIFY(result.isObject());
  QCOMPARE(result.toObject()["success"].toBool(), true);
}

void TestComputerUseApi::testDoubleClick() {
  QPoint btnCenter = m_testButton->mapTo(m_testWindow, m_testButton->rect().center());

  QJsonValue result =
      callResult("cu.doubleClick", QJsonObject{{"x", btnCenter.x()}, {"y", btnCenter.y()}});
  QApplication::processEvents();

  QVERIFY(result.isObject());
  QCOMPARE(result.toObject()["success"].toBool(), true);
}

// ========================================================================
// CU-05: Mouse move
// ========================================================================

void TestComputerUseApi::testMouseMove() {
  QJsonValue result = callResult("cu.mouseMove", QJsonObject{{"x", 100}, {"y", 100}});

  QVERIFY(result.isObject());
  QCOMPARE(result.toObject()["success"].toBool(), true);
}

// ========================================================================
// CU-06: Drag
// ========================================================================

void TestComputerUseApi::testDrag() {
  QJsonValue result = callResult(
      "cu.drag", QJsonObject{{"startX", 10}, {"startY", 10}, {"endX", 100}, {"endY", 100}});

  QVERIFY(result.isObject());
  QCOMPARE(result.toObject()["success"].toBool(), true);
}

// ========================================================================
// CU-07: Mouse down/up
// ========================================================================

void TestComputerUseApi::testMouseDown() {
  QPoint btnCenter = m_testButton->mapTo(m_testWindow, m_testButton->rect().center());

  QJsonValue result =
      callResult("cu.mouseDown", QJsonObject{{"x", btnCenter.x()}, {"y", btnCenter.y()}});
  QApplication::processEvents();

  QVERIFY(result.isObject());
  QCOMPARE(result.toObject()["success"].toBool(), true);
}

void TestComputerUseApi::testMouseUp() {
  QPoint btnCenter = m_testButton->mapTo(m_testWindow, m_testButton->rect().center());

  QJsonValue result =
      callResult("cu.mouseUp", QJsonObject{{"x", btnCenter.x()}, {"y", btnCenter.y()}});
  QApplication::processEvents();

  QVERIFY(result.isObject());
  QCOMPARE(result.toObject()["success"].toBool(), true);
}

// ========================================================================
// CU-08: Type
// ========================================================================

void TestComputerUseApi::testType() {
  m_testLineEdit->clear();
  m_testLineEdit->setFocus();
  QApplication::processEvents();

  QJsonValue result = callResult("cu.type", QJsonObject{{"text", "Hello"}});
  QApplication::processEvents();

  QVERIFY(result.isObject());
  QCOMPARE(result.toObject()["success"].toBool(), true);
  QCOMPARE(m_testLineEdit->text(), QString("Hello"));
}

// ========================================================================
// CU-09: Key
// ========================================================================

void TestComputerUseApi::testKey() {
  // Type some text first
  m_testLineEdit->clear();
  m_testLineEdit->setFocus();
  QApplication::processEvents();

  m_testLineEdit->setText("SomeText");
  QApplication::processEvents();

  // Select all with ctrl+a
  QJsonValue result1 = callResult("cu.key", QJsonObject{{"key", "ctrl+a"}});
  QApplication::processEvents();
  QVERIFY(result1.isObject());
  QCOMPARE(result1.toObject()["success"].toBool(), true);

  // Delete selected text
  QJsonValue result2 = callResult("cu.key", QJsonObject{{"key", "Delete"}});
  QApplication::processEvents();
  QVERIFY(result2.isObject());
  QCOMPARE(result2.toObject()["success"].toBool(), true);

  QCOMPARE(m_testLineEdit->text(), QString(""));
}

void TestComputerUseApi::testKeyChromeNames() {
  // Ensure focus exists for key events
  m_testLineEdit->setFocus();
  QApplication::processEvents();

  // Chrome key names should not error
  QJsonValue result1 = callResult("cu.key", QJsonObject{{"key", "Return"}});
  QVERIFY(result1.isObject());
  QCOMPARE(result1.toObject()["success"].toBool(), true);

  QJsonValue result2 = callResult("cu.key", QJsonObject{{"key", "Escape"}});
  QVERIFY(result2.isObject());
  QCOMPARE(result2.toObject()["success"].toBool(), true);

  QJsonValue result3 = callResult("cu.key", QJsonObject{{"key", "ArrowUp"}});
  QVERIFY(result3.isObject());
  QCOMPARE(result3.toObject()["success"].toBool(), true);
}

// ========================================================================
// CU-10: Scroll
// ========================================================================

void TestComputerUseApi::testScroll() {
  // Scroll at center of window
  QJsonValue result = callResult(
      "cu.scroll", QJsonObject{{"x", 200}, {"y", 150}, {"direction", "down"}, {"amount", 3}});

  QVERIFY(result.isObject());
  QCOMPARE(result.toObject()["success"].toBool(), true);
}

// ========================================================================
// CU-11: Cursor position
// ========================================================================

void TestComputerUseApi::testCursorPosition() {
  QJsonValue result = callResult("cu.cursorPosition", QJsonObject());
  QVERIFY(result.isObject());

  QJsonObject obj = result.toObject();
  QVERIFY(obj.contains("x"));
  QVERIFY(obj.contains("y"));
  QVERIFY(obj.contains("className"));

  // x and y should be numbers
  QVERIFY(obj["x"].isDouble());
  QVERIFY(obj["y"].isDouble());
}

// ========================================================================
// Error handling
// ========================================================================

void TestComputerUseApi::testClickOutOfBounds() {
  QJsonObject error = callExpectError("cu.click", QJsonObject{{"x", 9999}, {"y", 9999}});

  QCOMPARE(error["code"].toInt(), ErrorCode::kCoordinateOutOfBounds);
  QVERIFY(!error["message"].toString().isEmpty());
}

void TestComputerUseApi::testTypeNoFocusedWidget() {
  // Hide all widgets so nothing has focus
  m_testWindow->hide();
  QApplication::processEvents();

  // Clear focus explicitly
  if (QApplication::focusWidget()) {
    QApplication::focusWidget()->clearFocus();
  }
  QApplication::processEvents();

  // cu.type requires a focused widget - but we need a visible window for getActiveWindow()
  // The error should be kNoFocusedWidget or kNoActiveWindow
  QJsonObject error = callExpectError("cu.type", QJsonObject{{"text", "hello"}});

  // Either error is acceptable: no active window (-32060) or no focused widget (-32062)
  int code = error["code"].toInt();
  QVERIFY(code == ErrorCode::kNoFocusedWidget || code == ErrorCode::kNoActiveWindow);
  QVERIFY(!error["message"].toString().isEmpty());
}

void TestComputerUseApi::testIncludeScreenshot() {
  QPoint btnCenter = m_testButton->mapTo(m_testWindow, m_testButton->rect().center());

  // On minimal/headless platforms, the screenshot portion of include_screenshot
  // may fail (null pixmap), causing the method to return an error. Accept either.
  QJsonObject response = callRaw(
      "cu.click",
      QJsonObject{{"x", btnCenter.x()}, {"y", btnCenter.y()}, {"include_screenshot", true}});
  QApplication::processEvents();

  if (response.contains("error")) {
    // Error from screenshot capture — expected on minimal platform
    QString msg = response["error"].toObject()["message"].toString();
    QVERIFY2(msg.contains("null pixmap") || msg.contains("screen capture"),
             qPrintable("Unexpected error: " + msg));
    qWarning("include_screenshot returned error (expected on minimal platform): %s",
             qPrintable(msg));
    return;
  }

  QJsonObject envelope = response["result"].toObject();
  QJsonValue result = envelope["result"];
  QVERIFY(result.isObject());
  QJsonObject obj = result.toObject();
  QCOMPARE(obj["success"].toBool(), true);
  // On minimal platform, screenshot may be empty string. Verify key exists.
  QVERIFY(obj.contains("screenshot"));
}

QTEST_MAIN(TestComputerUseApi)
#include "test_computer_use_api.moc"
