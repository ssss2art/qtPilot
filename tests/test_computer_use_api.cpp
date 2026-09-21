// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "api/computer_use_mode_api.h"
#include "api/error_codes.h"
#include "common/qt_matchers.h"
#include "core/object_registry.h"
#include "core/object_resolver.h"
#include "transport/jsonrpc_handler.h"

#include <QApplication>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalSpy>
#include <QVBoxLayout>
#include <QtTest>

using namespace qtPilot;
using namespace qtPilot::test;

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
  void keyReachesWidgetAppWithoutExplicitFocus();
  void initTestCase();
  void cleanupTestCase();
  void init();
  void cleanup();

  // CU-01: Screenshot
  void testScreenshot();

  // CU-02 through CU-04: Clicks
  void testClick();
  void testClickFocusesWidgetForType();
  void testClickRetainsTypeTargetWhenPlatformFocusIsCleared();
  void testTabMovesTheRetainedKeyboardTarget();
  void testNonTabKeyPreservesTheRetainedKeyboardTarget();
  void testScreenAbsoluteClick();
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
  void testKeyActivatesNamedPunctuationShortcuts();

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
  QLineEdit* m_testNextLineEdit = nullptr;
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

  m_testNextLineEdit = new QLineEdit(central);
  m_testNextLineEdit->setObjectName("cuNextInputField");
  m_testNextLineEdit->setFixedSize(200, 30);
  layout->addWidget(m_testNextLineEdit);

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
  m_testNextLineEdit = nullptr;
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
    QEXPECT_THAT(error, HasJsonField("message"));
    QString msg = error["message"].toString();
    QEXPECT_THAT(msg, AnyOf(QStrContains("null pixmap"), QStrContains("screen capture")));
    qWarning("Screenshot returned error (expected on minimal platform): %s", qPrintable(msg));
    return;
  }

  // Success path
  QJsonValue resultVal = response["result"];
  QEXPECT_THAT(resultVal.isObject(), IsTrue());

  QJsonObject envelope = resultVal.toObject();
  QJsonValue innerResult = envelope["result"];
  QEXPECT_THAT(innerResult.isObject(), IsTrue());

  QJsonObject obj = innerResult.toObject();

  // Must have image, width, height keys in response
  QEXPECT_THAT(obj, AllOf(HasJsonField("image"), HasJsonField("width"), HasJsonField("height")));

  QString image = obj["image"].toString();
  if (!image.isEmpty()) {
    QEXPECT_THAT(obj["width"].toInt(), Gt(0));
    QEXPECT_THAT(obj["height"].toInt(), Gt(0));

    // Verify PNG format
    QEXPECT_THAT(image, IsValidPng());
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

  QEXPECT_THAT(result.isObject(), IsTrue());
  QEXPECT_THAT(result.toObject(), HasJsonField("success", Eq(true)));
  QEXPECT_THAT(clicked, IsTrue());
}

void TestComputerUseApi::testClickFocusesWidgetForType() {
  m_testLineEdit->clear();
  m_testButton->setFocus();
  QApplication::processEvents();

  const QPoint editCenter = m_testLineEdit->mapTo(m_testWindow, m_testLineEdit->rect().center());
  QJsonValue clickResult =
      callResult("cu.click", QJsonObject{{"x", editCenter.x()}, {"y", editCenter.y()}});
  QJsonValue typeResult = callResult("cu.type", QJsonObject{{"text", "Focused"}});
  QApplication::processEvents();

  QEXPECT_THAT(clickResult.toObject(), HasJsonField("success", Eq(true)));
  QEXPECT_THAT(typeResult.toObject(), HasJsonField("success", Eq(true)));
  QEXPECT_THAT(m_testLineEdit->text(), QStrEq("Focused"));
}

void TestComputerUseApi::testClickRetainsTypeTargetWhenPlatformFocusIsCleared() {
  m_testLineEdit->clear();
  m_testButton->setFocus();
  QApplication::processEvents();

  const QPoint editCenter = m_testLineEdit->mapTo(m_testWindow, m_testLineEdit->rect().center());
  QJsonValue clickResult =
      callResult("cu.click", QJsonObject{{"x", editCenter.x()}, {"y", editCenter.y()}});
  m_testLineEdit->clearFocus();
  QApplication::processEvents();
  QJsonValue typeResult = callResult("cu.type", QJsonObject{{"text", "Retained"}});
  QApplication::processEvents();

  QEXPECT_THAT(clickResult.toObject(), HasJsonField("success", Eq(true)));
  QEXPECT_THAT(typeResult.toObject(), HasJsonField("success", Eq(true)));
  QEXPECT_THAT(m_testLineEdit->text(), QStrEq("Retained"));
}

void TestComputerUseApi::testTabMovesTheRetainedKeyboardTarget() {
  m_testLineEdit->clear();
  m_testNextLineEdit->clear();
  m_testButton->setFocus();
  QApplication::processEvents();

  const QPoint editCenter = m_testLineEdit->mapTo(m_testWindow, m_testLineEdit->rect().center());
  callResult("cu.click", QJsonObject{{"x", editCenter.x()}, {"y", editCenter.y()}});
  m_testLineEdit->clearFocus();
  QApplication::processEvents();

  QJsonValue keyResult = callResult("cu.key", QJsonObject{{"key", "Tab"}});
  QJsonValue typeResult = callResult("cu.type", QJsonObject{{"text", "Moved"}});
  QApplication::processEvents();
  QEXPECT_THAT(keyResult.toObject(), HasJsonField("success", Eq(true)));
  QEXPECT_THAT(typeResult.toObject(), HasJsonField("success", Eq(true)));
  QEXPECT_THAT(m_testNextLineEdit->text(), QStrEq("Moved"));
}

void TestComputerUseApi::testNonTabKeyPreservesTheRetainedKeyboardTarget() {
  m_testLineEdit->clear();
  m_testButton->setFocus();
  QApplication::processEvents();

  const QPoint editCenter = m_testLineEdit->mapTo(m_testWindow, m_testLineEdit->rect().center());
  callResult("cu.click", QJsonObject{{"x", editCenter.x()}, {"y", editCenter.y()}});
  m_testLineEdit->clearFocus();
  QApplication::processEvents();

  // Send a non-Tab key while platform focus is cleared.
  // This must not wipe out the retained keyboard target.
  QJsonValue nonTabKey = callResult("cu.key", QJsonObject{{"key", "Backspace"}});
  QJsonValue typeResult = callResult("cu.type", QJsonObject{{"text", "StillRetained"}});
  QApplication::processEvents();

  QEXPECT_THAT(nonTabKey.toObject(), HasJsonField("success", Eq(true)));
  QEXPECT_THAT(typeResult.toObject(), HasJsonField("success", Eq(true)));
  QEXPECT_THAT(m_testLineEdit->text(), QStrEq("StillRetained"));
}

void TestComputerUseApi::testScreenAbsoluteClick() {
  QPoint btnCenter = m_testButton->mapToGlobal(m_testButton->rect().center());

  bool clicked = false;
  connect(m_testButton, &QPushButton::clicked, this, [&clicked]() { clicked = true; });

  QJsonValue result =
      callResult("cu.click",
                 QJsonObject{{"x", btnCenter.x()}, {"y", btnCenter.y()}, {"screenAbsolute", true}});
  QApplication::processEvents();

  QEXPECT_THAT(result.isObject(), IsTrue());
  QEXPECT_THAT(result.toObject(), HasJsonField("success", Eq(true)));
  QEXPECT_THAT(clicked, IsTrue());
}

void TestComputerUseApi::testRightClick() {
  QPoint btnCenter = m_testButton->mapTo(m_testWindow, m_testButton->rect().center());

  QJsonValue result =
      callResult("cu.rightClick", QJsonObject{{"x", btnCenter.x()}, {"y", btnCenter.y()}});
  QApplication::processEvents();

  QEXPECT_THAT(result.isObject(), IsTrue());
  QEXPECT_THAT(result.toObject(), HasJsonField("success", Eq(true)));
}

void TestComputerUseApi::testMiddleClick() {
  QPoint btnCenter = m_testButton->mapTo(m_testWindow, m_testButton->rect().center());

  QJsonValue result =
      callResult("cu.middleClick", QJsonObject{{"x", btnCenter.x()}, {"y", btnCenter.y()}});
  QApplication::processEvents();

  QEXPECT_THAT(result.isObject(), IsTrue());
  QEXPECT_THAT(result.toObject(), HasJsonField("success", Eq(true)));
}

void TestComputerUseApi::testDoubleClick() {
  QPoint btnCenter = m_testButton->mapTo(m_testWindow, m_testButton->rect().center());

  QJsonValue result =
      callResult("cu.doubleClick", QJsonObject{{"x", btnCenter.x()}, {"y", btnCenter.y()}});
  QApplication::processEvents();

  QEXPECT_THAT(result.isObject(), IsTrue());
  QEXPECT_THAT(result.toObject(), HasJsonField("success", Eq(true)));
}

// ========================================================================
// CU-05: Mouse move
// ========================================================================

void TestComputerUseApi::testMouseMove() {
  QJsonValue result = callResult("cu.mouseMove", QJsonObject{{"x", 100}, {"y", 100}});

  QEXPECT_THAT(result.isObject(), IsTrue());
  QEXPECT_THAT(result.toObject(), HasJsonField("success", Eq(true)));
}

// ========================================================================
// CU-06: Drag
// ========================================================================

void TestComputerUseApi::testDrag() {
  QJsonValue result = callResult(
      "cu.drag", QJsonObject{{"startX", 10}, {"startY", 10}, {"endX", 100}, {"endY", 100}});

  QEXPECT_THAT(result.isObject(), IsTrue());
  QEXPECT_THAT(result.toObject(), HasJsonField("success", Eq(true)));
}

// ========================================================================
// CU-07: Mouse down/up
// ========================================================================

void TestComputerUseApi::testMouseDown() {
  QPoint btnCenter = m_testButton->mapTo(m_testWindow, m_testButton->rect().center());

  QJsonValue result =
      callResult("cu.mouseDown", QJsonObject{{"x", btnCenter.x()}, {"y", btnCenter.y()}});
  QApplication::processEvents();

  QEXPECT_THAT(result.isObject(), IsTrue());
  QEXPECT_THAT(result.toObject(), HasJsonField("success", Eq(true)));
}

void TestComputerUseApi::testMouseUp() {
  QPoint btnCenter = m_testButton->mapTo(m_testWindow, m_testButton->rect().center());

  QJsonValue result =
      callResult("cu.mouseUp", QJsonObject{{"x", btnCenter.x()}, {"y", btnCenter.y()}});
  QApplication::processEvents();

  QEXPECT_THAT(result.isObject(), IsTrue());
  QEXPECT_THAT(result.toObject(), HasJsonField("success", Eq(true)));
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

  QEXPECT_THAT(result.isObject(), IsTrue());
  QEXPECT_THAT(result.toObject(), HasJsonField("success", Eq(true)));
  QEXPECT_THAT(m_testLineEdit->text(), QStrEq("Hello"));
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
  QEXPECT_THAT(result1.isObject(), IsTrue());
  QEXPECT_THAT(result1.toObject(), HasJsonField("success", Eq(true)));

  // Delete selected text
  QJsonValue result2 = callResult("cu.key", QJsonObject{{"key", "Delete"}});
  QApplication::processEvents();
  QEXPECT_THAT(result2.isObject(), IsTrue());
  QEXPECT_THAT(result2.toObject(), HasJsonField("success", Eq(true)));

  QEXPECT_THAT(m_testLineEdit->text(), QIsEmpty());
}

void TestComputerUseApi::testKeyChromeNames() {
  // Ensure focus exists for key events
  m_testLineEdit->setFocus();
  QApplication::processEvents();

  // Chrome key names should not error
  QJsonValue result1 = callResult("cu.key", QJsonObject{{"key", "Return"}});
  QEXPECT_THAT(result1.isObject(), IsTrue());
  QEXPECT_THAT(result1.toObject(), HasJsonField("success", Eq(true)));

  QJsonValue result2 = callResult("cu.key", QJsonObject{{"key", "Escape"}});
  QEXPECT_THAT(result2.isObject(), IsTrue());
  QEXPECT_THAT(result2.toObject(), HasJsonField("success", Eq(true)));

  QJsonValue result3 = callResult("cu.key", QJsonObject{{"key", "ArrowUp"}});
  QEXPECT_THAT(result3.isObject(), IsTrue());
  QEXPECT_THAT(result3.toObject(), HasJsonField("success", Eq(true)));
}

void TestComputerUseApi::testKeyActivatesNamedPunctuationShortcuts() {
  QShortcut metaPlus(
      QKeySequence(static_cast<int>(Qt::MetaModifier) | static_cast<int>(Qt::Key_Plus)),
      m_testWindow);
  QShortcut controlMinus(
      QKeySequence(static_cast<int>(Qt::ControlModifier) | static_cast<int>(Qt::Key_Minus)),
      m_testWindow);
  QShortcut questionMark(QKeySequence(static_cast<int>(Qt::Key_Question)), m_testWindow);
  QSignalSpy metaPlusSpy(&metaPlus, &QShortcut::activated);
  QSignalSpy controlMinusSpy(&controlMinus, &QShortcut::activated);
  QSignalSpy questionMarkSpy(&questionMark, &QShortcut::activated);

  m_testButton->setFocus();
  QApplication::processEvents();

  QEXPECT_THAT(callResult("cu.key", QJsonObject{{"key", "meta+Plus"}}).isObject(), IsTrue());
  QTRY_COMPARE(metaPlusSpy.size(), 1);

  QEXPECT_THAT(callResult("cu.key", QJsonObject{{"key", "ctrl+Minus"}}).isObject(), IsTrue());
  QTRY_COMPARE(controlMinusSpy.size(), 1);

  QEXPECT_THAT(callResult("cu.key", QJsonObject{{"key", "QuestionMark"}}).isObject(), IsTrue());
  QTRY_COMPARE(questionMarkSpy.size(), 1);
}

// ========================================================================
// CU-10: Scroll
// ========================================================================

void TestComputerUseApi::testScroll() {
  // Scroll at center of window
  QJsonValue result = callResult(
      "cu.scroll", QJsonObject{{"x", 200}, {"y", 150}, {"direction", "down"}, {"amount", 3}});

  QEXPECT_THAT(result.isObject(), IsTrue());
  QEXPECT_THAT(result.toObject(), HasJsonField("success", Eq(true)));
}

// ========================================================================
// CU-11: Cursor position
// ========================================================================

void TestComputerUseApi::testCursorPosition() {
  QJsonValue result = callResult("cu.cursorPosition", QJsonObject());
  QEXPECT_THAT(result.isObject(), IsTrue());

  QJsonObject obj = result.toObject();
  QEXPECT_THAT(obj, AllOf(HasJsonField("x"), HasJsonField("y"), HasJsonField("className")));

  // x and y should be numbers
  QEXPECT_THAT(obj["x"].isDouble(), IsTrue());
  QEXPECT_THAT(obj["y"].isDouble(), IsTrue());
}

// ========================================================================
// Error handling
// ========================================================================

void TestComputerUseApi::testClickOutOfBounds() {
  QJsonObject error = callExpectError("cu.click", QJsonObject{{"x", 9999}, {"y", 9999}});

  QEXPECT_THAT(error,
               AllOf(HasJsonField("code", Eq(static_cast<int>(ErrorCode::kCoordinateOutOfBounds))),
                     HasJsonField("message", QIsNotEmpty())));
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
  QEXPECT_THAT(code, AnyOf(Eq(static_cast<int>(ErrorCode::kNoFocusedWidget)),
                           Eq(static_cast<int>(ErrorCode::kNoActiveWindow))));
  QEXPECT_THAT(error, HasJsonField("message", QIsNotEmpty()));
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
    QEXPECT_THAT(msg, AnyOf(QStrContains("null pixmap"), QStrContains("screen capture")));
    qWarning("include_screenshot returned error (expected on minimal platform): %s",
             qPrintable(msg));
    return;
  }

  QJsonObject envelope = response["result"].toObject();
  QJsonValue result = envelope["result"];
  QEXPECT_THAT(result.isObject(), IsTrue());
  QJsonObject obj = result.toObject();
  QEXPECT_THAT(obj, AllOf(HasJsonField("success", Eq(true)), HasJsonField("screenshot")));
}

// cu.key on a Widgets app where nothing has been clicked yet.
//
// getActiveTarget() already resolves this case: with no focused QWindow it falls
// back to QApplication::activeWindow(), then to the first visible top-level
// QWidget. But the caller tested t.isWindow(), which is true only for the QWindow
// branch — so a perfectly good QWidget target was resolved and then discarded,
// and the call failed with "No widget has keyboard focus".
//
// That is the state every harness-launched application starts in: nothing has
// been clicked, so no widget holds focus.
void TestComputerUseApi::keyReachesWidgetAppWithoutExplicitFocus() {
  if (QWidget* focused = QApplication::focusWidget()) {
    focused->clearFocus();
  }
  QApplication::processEvents();
  QEXPECT_THAT(QApplication::focusWidget(), IsNull());

  QJsonObject response = callRaw(QStringLiteral("cu.key"), QJsonObject{{"key", "ctrl+a"}});
  QEXPECT_THAT(response, DoesNotHaveJsonField("error"));
}

QTEST_MAIN(TestComputerUseApi)
#include "test_computer_use_api.moc"
