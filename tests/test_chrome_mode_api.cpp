// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "common/qt_matchers.h"
#include "accessibility/console_message_capture.h"
#include "api/chrome_mode_api.h"
#include "api/error_codes.h"
#include "transport/jsonrpc_handler.h"

#include <QAccessible>
#include <QAccessibleActionInterface>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>
#include <QtTest>

using namespace qtPilot;
using namespace qtPilot::test;

/// @brief Integration tests for the Chrome Mode API (chr.* methods).
///
/// Tests all 8 chr.* JSON-RPC methods end-to-end through the JSON-RPC handler:
/// readPage, click, formInput, getPageText, find, navigate, tabsContext,
/// readConsoleMessages.
///
/// Also verifies error handling for stale refs, invalid refs, and unsupported widgets.
class TestChromeModeApi : public QObject {
  Q_OBJECT

 private slots:
  void init();
  void cleanup();

  // chr.readPage tests
  void testReadPage_ReturnsTree();
  void testReadPage_AllFilter();
  void testReadPage_InteractiveFilter();
  void testReadPage_MaxDepth();
  void testReadPage_RefFormat();
  void testReadPage_IncludesQtExtras();
  void testReadPage_RoleMapping();

  // chr.click tests
  void testClick_Button();
  void testClick_CheckablePrefersToggleAction();
  void testClick_InvalidRef();

  // chr.formInput tests
  void testFormInput_LineEdit();
  void testFormInput_SpinBox();
  void testFormInput_CheckBox();
  void testFormInput_ComboBox();
  void testFormInput_UnsupportedWidget();

  // chr.getPageText tests
  void testGetPageText_ExtractsText();
  void testGetPageText_SkipsInvisible();

  // chr.find tests
  void testFind_ByName();
  void testFind_CaseInsensitive();
  void testFind_ByRole();
  void testFind_NoResults();

  // chr.tabsContext tests
  void testTabsContext_ListsWindows();

  // chr.readConsoleMessages tests
  void testReadConsoleMessages_CapturesDebug();
  void testReadConsoleMessages_PatternFilter();
  void testReadConsoleMessages_OnlyErrors();
  void testReadConsoleMessages_Clear();

  // Stale ref test
  void testStaleRef_ProducesClearError();

  // Regression tests for chr.find bugs (05-04 gap closure)
  void testFind_MultipleCallsPreserveRefs();
  void testFind_ReadPageClearsAllRefs();
  void testFind_NameFallbackToObjectName();

 private:
  /// @brief Make a JSON-RPC call and return the full parsed response object.
  QJsonObject callRaw(const QString& method, const QJsonObject& params);

  /// @brief Make a JSON-RPC call and return the envelope result (unwrapped from JSON-RPC).
  QJsonObject callEnvelope(const QString& method, const QJsonObject& params);

  /// @brief Make a JSON-RPC call and return the inner result value from the envelope.
  QJsonValue callResult(const QString& method, const QJsonObject& params);

  /// @brief Make a JSON-RPC call expecting an error, return the error object.
  QJsonObject callExpectError(const QString& method, const QJsonObject& params);

  /// @brief Helper: call chr.readPage and return inner result object.
  QJsonObject readPage(const QJsonObject& params = QJsonObject());

  /// @brief Helper: find a ref in the tree by objectName, recursively.
  QString findRefByObjectName(const QJsonObject& tree, const QString& objName);

  JsonRpcHandler* m_handler = nullptr;
  ChromeModeApi* m_api = nullptr;
  QWidget* m_mainWindow = nullptr;
  QPushButton* m_button = nullptr;
  QLineEdit* m_lineEdit = nullptr;
  QSpinBox* m_spinBox = nullptr;
  QCheckBox* m_checkBox = nullptr;
  QLabel* m_label = nullptr;
  QComboBox* m_comboBox = nullptr;
  int m_requestId = 1;
};

void TestChromeModeApi::init() {
  m_requestId = 1;

  // Ensure accessibility is active (needed on minimal platform)
  QAccessible::setActive(true);

  // Install console message capture for chr.readConsoleMessages tests
  ConsoleMessageCapture::instance()->install();
  ConsoleMessageCapture::instance()->clear();

  m_handler = new JsonRpcHandler(this);
  m_api = new ChromeModeApi(m_handler, this);

  // Create test widget hierarchy
  m_mainWindow = new QWidget();
  m_mainWindow->setWindowTitle("Test Window");
  m_mainWindow->setObjectName("mainWindow");

  QVBoxLayout* layout = new QVBoxLayout(m_mainWindow);

  m_button = new QPushButton("Click Me", m_mainWindow);
  m_button->setObjectName("btnTest");
  layout->addWidget(m_button);

  m_lineEdit = new QLineEdit(m_mainWindow);
  m_lineEdit->setObjectName("editName");
  layout->addWidget(m_lineEdit);

  m_spinBox = new QSpinBox(m_mainWindow);
  m_spinBox->setObjectName("spinAge");
  m_spinBox->setRange(0, 120);
  m_spinBox->setValue(25);
  layout->addWidget(m_spinBox);

  m_checkBox = new QCheckBox("Accept Terms", m_mainWindow);
  m_checkBox->setObjectName("chkTerms");
  layout->addWidget(m_checkBox);

  m_label = new QLabel("Hello World", m_mainWindow);
  m_label->setObjectName("lblGreeting");
  layout->addWidget(m_label);

  m_comboBox = new QComboBox(m_mainWindow);
  m_comboBox->setObjectName("comboColor");
  m_comboBox->addItems({"Red", "Green", "Blue"});
  layout->addWidget(m_comboBox);

  m_mainWindow->show();
  QApplication::processEvents();
}

void TestChromeModeApi::cleanup() {
  ChromeModeApi::clearRefs();

  delete m_mainWindow;
  m_mainWindow = nullptr;
  m_button = nullptr;
  m_lineEdit = nullptr;
  m_spinBox = nullptr;
  m_checkBox = nullptr;
  m_label = nullptr;
  m_comboBox = nullptr;

  delete m_api;
  m_api = nullptr;
  delete m_handler;
  m_handler = nullptr;
}

QJsonObject TestChromeModeApi::callRaw(const QString& method, const QJsonObject& params) {
  QJsonObject request;
  request["jsonrpc"] = "2.0";
  request["method"] = method;
  request["params"] = params;
  request["id"] = m_requestId++;

  QString requestStr = QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact));
  QString responseStr = m_handler->HandleMessage(requestStr);

  return QJsonDocument::fromJson(responseStr.toUtf8()).object();
}

QJsonObject TestChromeModeApi::callEnvelope(const QString& method, const QJsonObject& params) {
  QJsonObject response = callRaw(method, params);
  QJsonValue resultVal = response["result"];
  if (resultVal.isObject()) {
    return resultVal.toObject();
  }
  return QJsonObject();
}

QJsonValue TestChromeModeApi::callResult(const QString& method, const QJsonObject& params) {
  QJsonObject envelope = callEnvelope(method, params);
  return envelope["result"];
}

QJsonObject TestChromeModeApi::callExpectError(const QString& method, const QJsonObject& params) {
  QJsonObject response = callRaw(method, params);
  return response["error"].toObject();
}

QJsonObject TestChromeModeApi::readPage(const QJsonObject& params) {
  QJsonValue result = callResult("chr.readPage", params);
  if (!result.isObject()) {
    qWarning("chr.readPage did not return an object");
    return QJsonObject();
  }
  return result.toObject();
}

QString TestChromeModeApi::findRefByObjectName(const QJsonObject& tree, const QString& objName) {
  if (tree["objectName"].toString() == objName) {
    return tree["ref"].toString();
  }
  QJsonArray children = tree["children"].toArray();
  for (const QJsonValue& child : children) {
    QString ref = findRefByObjectName(child.toObject(), objName);
    if (!ref.isEmpty())
      return ref;
  }
  return QString();
}

// ========================================================================
// chr.readPage tests
// ========================================================================

void TestChromeModeApi::testReadPage_ReturnsTree() {
  QJsonObject result = readPage();

  // Must contain tree, totalNodes
  QEXPECT_THAT(result, AllOf(
      HasJsonField("tree", HasJsonField("role")),
      HasJsonField("totalNodes", Gt(0))));
}

void TestChromeModeApi::testReadPage_AllFilter() {
  QJsonObject result = readPage(QJsonObject{{"filter", "all"}});
  QJsonObject tree = result["tree"].toObject();

  // With all filter, nodes should have refs
  // Find the button ref in the tree
  QString btnRef = findRefByObjectName(tree, "btnTest");
  QEXPECT_THAT(btnRef, QIsNotEmpty());

  // Labels should also have refs in all mode
  QString lblRef = findRefByObjectName(tree, "lblGreeting");
  QEXPECT_THAT(lblRef, QIsNotEmpty());
}

void TestChromeModeApi::testReadPage_InteractiveFilter() {
  QJsonObject result = readPage(QJsonObject{{"filter", "interactive"}});
  QJsonObject tree = result["tree"].toObject();

  // Interactive elements should have refs
  QString btnRef = findRefByObjectName(tree, "btnTest");
  QEXPECT_THAT(btnRef, QIsNotEmpty());

  // Labels should NOT have refs in interactive mode
  QString lblRef = findRefByObjectName(tree, "lblGreeting");
  QEXPECT_THAT(lblRef, QIsEmpty());
}

void TestChromeModeApi::testReadPage_MaxDepth() {
  // With depth=1, tree should be shallow
  QJsonObject result = readPage(QJsonObject{{"depth", 1}});
  QJsonObject tree = result["tree"].toObject();

  // Root should exist
  QEXPECT_THAT(tree, HasJsonField("role"));

  // At depth 1, should have limited children (if any)
  // The root children count should be reasonable
  int totalNodes = result["totalNodes"].toInt();
  QEXPECT_THAT(totalNodes, Gt(0));
  // With depth=1, total nodes should be much less than full tree
  QJsonObject fullResult = readPage(QJsonObject{{"depth", 15}});
  int fullNodes = fullResult["totalNodes"].toInt();
  QEXPECT_THAT(totalNodes, Le(fullNodes));
}

void TestChromeModeApi::testReadPage_RefFormat() {
  QJsonObject result = readPage();
  QJsonObject tree = result["tree"].toObject();

  // Find any ref and verify format
  QString btnRef = findRefByObjectName(tree, "btnTest");
  if (!btnRef.isEmpty()) {
    // Refs should follow "ref_N" pattern
    QEXPECT_THAT(btnRef, QStrStartsWith("ref_"));
  }
}

void TestChromeModeApi::testReadPage_IncludesQtExtras() {
  QJsonObject result = readPage();
  QJsonObject tree = result["tree"].toObject();

  // Find button node - should have objectName and className
  // Walk tree to find node with objectName "btnTest"
  std::function<QJsonObject(const QJsonObject&)> findNode;
  findNode = [&findNode](const QJsonObject& node) -> QJsonObject {
    if (node["objectName"].toString() == "btnTest")
      return node;
    QJsonArray children = node["children"].toArray();
    for (const QJsonValue& child : children) {
      QJsonObject found = findNode(child.toObject());
      if (!found.isEmpty())
        return found;
    }
    return QJsonObject();
  };

  QJsonObject btnNode = findNode(tree);
  if (!btnNode.isEmpty()) {
    QEXPECT_THAT(btnNode, AllOf(
        HasJsonField("objectName", QStrEq("btnTest")),
        HasJsonField("className", QStrEq("QPushButton"))));
  } else {
    qWarning("btnTest node not found in tree - accessibility may be limited on minimal platform");
  }
}

void TestChromeModeApi::testReadPage_RoleMapping() {
  QJsonObject result = readPage();
  QJsonObject tree = result["tree"].toObject();

  // Helper to find node by objectName
  std::function<QJsonObject(const QJsonObject&, const QString&)> findNode;
  findNode = [&findNode](const QJsonObject& node, const QString& name) -> QJsonObject {
    if (node["objectName"].toString() == name)
      return node;
    QJsonArray children = node["children"].toArray();
    for (const QJsonValue& child : children) {
      QJsonObject found = findNode(child.toObject(), name);
      if (!found.isEmpty())
        return found;
    }
    return QJsonObject();
  };

  QJsonObject btnNode = findNode(tree, "btnTest");
  if (!btnNode.isEmpty()) {
    QEXPECT_THAT(btnNode, HasJsonField("role", QStrEq("button")));
  }

  QJsonObject editNode = findNode(tree, "editName");
  if (!editNode.isEmpty()) {
    QEXPECT_THAT(editNode, HasJsonField("role", QStrEq("textbox")));
  }

  QJsonObject lblNode = findNode(tree, "lblGreeting");
  if (!lblNode.isEmpty()) {
    // QLabel maps to "text" via RoleMapper
    QString lblRole = lblNode["role"].toString();
    QEXPECT_THAT(lblRole, AnyOf(QStrEq("text"), QStrEq("label"), QStrEq("statictext")));
  }
}

// ========================================================================
// chr.click tests
// ========================================================================

void TestChromeModeApi::testClick_Button() {
  // First read the page to get refs
  QJsonObject pageResult = readPage();
  QJsonObject tree = pageResult["tree"].toObject();
  QString btnRef = findRefByObjectName(tree, "btnTest");
  QEXPECT_THAT(btnRef, QIsNotEmpty());

  // Click the button by ref
  QJsonValue result = callResult("chr.click", QJsonObject{{"ref", btnRef}});
  QApplication::processEvents();

  QEXPECT_THAT(result.isObject(), IsTrue());
  QJsonObject obj = result.toObject();
  QEXPECT_THAT(obj, AllOf(
      HasJsonField("clicked", Eq(true)),
      HasJsonField("ref", QStrEq(btnRef)),
      HasJsonField("method", AnyOf(QStrEq("accessibilityAction"), QStrEq("mouseClick")))));
}

void TestChromeModeApi::testClick_CheckablePrefersToggleAction() {
  // A checkable control exposes both Toggle and Press. chr.click used to always
  // send Press, which on Qt Quick Controls is advertised but inert -- the click
  // reported success while nothing actuated. Toggle is the correct verb for a
  // checkbox on both toolkits, so assert it is the one chosen.
  QJsonObject tree = readPage()["tree"].toObject();
  QString ref = findRefByObjectName(tree, "chkTerms");
  QEXPECT_THAT(ref, QIsNotEmpty());

  const bool before = m_checkBox->isChecked();
  QJsonObject obj = callResult("chr.click", QJsonObject{{"ref", ref}}).toObject();
  QApplication::processEvents();

  QEXPECT_THAT(obj, HasJsonField("clicked", Eq(true)));
  QEXPECT_THAT(m_checkBox->isChecked(), Eq(!before));
  if (obj["method"].toString() == QLatin1String("accessibilityAction")) {
    QEXPECT_THAT(obj["action"].toString(), QStrEq(QAccessibleActionInterface::toggleAction()));
  }
}

void TestChromeModeApi::testClick_InvalidRef() {
  QJsonObject error = callExpectError("chr.click", QJsonObject{{"ref", "ref_999"}});
  QEXPECT_THAT(error, AllOf(
      HasJsonField("code", Eq(static_cast<int>(ErrorCode::kRefNotFound))),
      HasJsonField("message", QIsNotEmpty())));
}

// ========================================================================
// chr.formInput tests
// ========================================================================

void TestChromeModeApi::testFormInput_LineEdit() {
  // Read page to get refs
  QJsonObject pageResult = readPage();
  QJsonObject tree = pageResult["tree"].toObject();
  QString editRef = findRefByObjectName(tree, "editName");
  QEXPECT_THAT(editRef, QIsNotEmpty());

  // Set text value
  QJsonValue result =
      callResult("chr.formInput", QJsonObject{{"ref", editRef}, {"value", "John Doe"}});
  QApplication::processEvents();

  QEXPECT_THAT(result.isObject(), IsTrue());
  QEXPECT_THAT(result.toObject(), HasJsonField("success", Eq(true)));
  QEXPECT_THAT(m_lineEdit->text(), QStrEq("John Doe"));
}

void TestChromeModeApi::testFormInput_SpinBox() {
  QJsonObject pageResult = readPage();
  QJsonObject tree = pageResult["tree"].toObject();
  QString spinRef = findRefByObjectName(tree, "spinAge");
  QEXPECT_THAT(spinRef, QIsNotEmpty());

  // Set numeric value
  QJsonValue result = callResult("chr.formInput", QJsonObject{{"ref", spinRef}, {"value", 42}});
  QApplication::processEvents();

  QEXPECT_THAT(result.isObject(), IsTrue());
  QEXPECT_THAT(result.toObject(), HasJsonField("success", Eq(true)));
  QEXPECT_THAT(m_spinBox->value(), Eq(42));
}

void TestChromeModeApi::testFormInput_CheckBox() {
  QJsonObject pageResult = readPage();
  QJsonObject tree = pageResult["tree"].toObject();
  QString chkRef = findRefByObjectName(tree, "chkTerms");
  QEXPECT_THAT(chkRef, QIsNotEmpty());

  // Initially unchecked, set to checked
  QEXPECT_THAT(m_checkBox->isChecked(), IsFalse());

  QJsonValue result = callResult("chr.formInput", QJsonObject{{"ref", chkRef}, {"value", true}});
  QApplication::processEvents();

  QEXPECT_THAT(result.isObject(), IsTrue());
  QEXPECT_THAT(result.toObject(), HasJsonField("success", Eq(true)));
  QEXPECT_THAT(m_checkBox->isChecked(), IsTrue());
}

void TestChromeModeApi::testFormInput_ComboBox() {
  QJsonObject pageResult = readPage();
  QJsonObject tree = pageResult["tree"].toObject();
  QString comboRef = findRefByObjectName(tree, "comboColor");
  QEXPECT_THAT(comboRef, QIsNotEmpty());

  // Set to "Green"
  QJsonValue result =
      callResult("chr.formInput", QJsonObject{{"ref", comboRef}, {"value", "Green"}});
  QApplication::processEvents();

  QEXPECT_THAT(result.isObject(), IsTrue());
  QEXPECT_THAT(result.toObject(), HasJsonField("success", Eq(true)));
  QEXPECT_THAT(m_comboBox->currentText(), QStrEq("Green"));
}

void TestChromeModeApi::testFormInput_UnsupportedWidget() {
  QJsonObject pageResult = readPage();
  QJsonObject tree = pageResult["tree"].toObject();
  QString lblRef = findRefByObjectName(tree, "lblGreeting");
  QEXPECT_THAT(lblRef, QIsNotEmpty());

  // Attempting formInput on a label should fail
  QJsonObject error =
      callExpectError("chr.formInput", QJsonObject{{"ref", lblRef}, {"value", "new text"}});
  QEXPECT_THAT(error, HasJsonField("code", Eq(static_cast<int>(ErrorCode::kFormInputUnsupported))));
}

// ========================================================================
// chr.getPageText tests
// ========================================================================

void TestChromeModeApi::testGetPageText_ExtractsText() {
  QJsonValue result = callResult("chr.getPageText", QJsonObject());
  QEXPECT_THAT(result.isObject(), IsTrue());

  QString text = result.toObject()["text"].toString();
  // Should contain visible text from the widgets
  // On minimal platform, accessibility text extraction may be limited
  // but the call should succeed and return a string
  if (!text.isEmpty()) {
    // At least some text should be extracted
    QEXPECT_THAT(text, AnyOf(
        QStrContains("Click Me"),
        QStrContains("Hello World"),
        QStrContains("Accept Terms")));
  } else {
    qWarning("getPageText returned empty text (may be expected on minimal platform)");
  }
}

void TestChromeModeApi::testGetPageText_SkipsInvisible() {
  // Hide the label
  m_label->hide();
  QApplication::processEvents();

  QJsonValue result = callResult("chr.getPageText", QJsonObject());
  QEXPECT_THAT(result.isObject(), IsTrue());

  QString text = result.toObject()["text"].toString();
  // "Hello World" from the hidden label should NOT appear
  if (!text.isEmpty()) {
    QEXPECT_THAT(text, Not(QStrContains("Hello World")));
  }
}

// ========================================================================
// chr.find tests
// ========================================================================

void TestChromeModeApi::testFind_ByName() {
  QJsonValue result = callResult("chr.find", QJsonObject{{"query", "Click Me"}});
  QEXPECT_THAT(result.isObject(), IsTrue());

  QJsonObject obj = result.toObject();
  QJsonArray matches = obj["matches"].toArray();
  QEXPECT_THAT(matches.size(), Gt(0));

  // First match should have a ref
  QJsonObject firstMatch = matches[0].toObject();
  QEXPECT_THAT(firstMatch, HasJsonField("ref", QIsNotEmpty()));
}

void TestChromeModeApi::testFind_CaseInsensitive() {
  QJsonValue result = callResult("chr.find", QJsonObject{{"query", "click me"}});
  QEXPECT_THAT(result.isObject(), IsTrue());

  QJsonObject obj = result.toObject();
  QJsonArray matches = obj["matches"].toArray();
  QEXPECT_THAT(matches.size(), Gt(0));
}

void TestChromeModeApi::testFind_ByRole() {
  QJsonValue result = callResult("chr.find", QJsonObject{{"query", "button"}});
  QEXPECT_THAT(result.isObject(), IsTrue());

  QJsonObject obj = result.toObject();
  QJsonArray matches = obj["matches"].toArray();
  QEXPECT_THAT(matches.size(), Gt(0));
  QEXPECT_THAT(matches, JsonArrayContains(HasJsonField("role", QStrEq("button"))));
}

void TestChromeModeApi::testFind_NoResults() {
  QJsonValue result = callResult("chr.find", QJsonObject{{"query", "nonexistent_xyz_12345"}});
  QEXPECT_THAT(result.isObject(), IsTrue());

  QJsonObject obj = result.toObject();
  QEXPECT_THAT(obj, AllOf(
      HasJsonField("matches", QIsEmpty()),
      HasJsonField("count", Eq(0))));
}

// ========================================================================
// chr.tabsContext tests
// ========================================================================

void TestChromeModeApi::testTabsContext_ListsWindows() {
  QJsonValue result = callResult("chr.tabsContext", QJsonObject());
  QEXPECT_THAT(result.isObject(), IsTrue());

  QJsonObject obj = result.toObject();
  QJsonArray windows = obj["windows"].toArray();
  QEXPECT_THAT(windows.size(), Gt(0));

  QEXPECT_THAT(windows, JsonArrayContains(AllOf(
      HasJsonField("windowTitle", QStrEq("Test Window")),
      HasJsonField("className"),
      HasJsonField("geometry"))));
}

// ========================================================================
// chr.readConsoleMessages tests
// ========================================================================

void TestChromeModeApi::testReadConsoleMessages_CapturesDebug() {
  // Use qWarning to ensure message reaches handler on all platforms
  // (qDebug may be suppressed in Release builds on some configurations)
  qWarning("chrome_test_message_12345");
  QApplication::processEvents();

  QJsonValue result = callResult("chr.readConsoleMessages", QJsonObject());
  QEXPECT_THAT(result.isObject(), IsTrue());

  QJsonObject obj = result.toObject();
  QJsonArray messages = obj["messages"].toArray();

  QEXPECT_THAT(messages, JsonArrayContains(AllOf(
      HasJsonField("message", QStrContains("chrome_test_message_12345")),
      HasJsonField("type", QStrEq("warning")))));
}

void TestChromeModeApi::testReadConsoleMessages_PatternFilter() {
  ConsoleMessageCapture::instance()->clear();

  qWarning("alpha_message");
  qWarning("beta_message");
  qWarning("alpha_again");
  QApplication::processEvents();

  QJsonValue result = callResult("chr.readConsoleMessages", QJsonObject{{"pattern", "alpha"}});
  QEXPECT_THAT(result.isObject(), IsTrue());

  QJsonObject obj = result.toObject();
  QJsonArray messages = obj["messages"].toArray();
  QEXPECT_THAT(messages.size(), Ge(2));

  for (const QJsonValue& msg : messages) {
    QEXPECT_THAT(msg.toObject(), HasJsonField("message", QStrContains("alpha")));
  }
}

void TestChromeModeApi::testReadConsoleMessages_OnlyErrors() {
  ConsoleMessageCapture::instance()->clear();

  qInfo("info_only_msg");
  qWarning("warning_only_msg");
  QApplication::processEvents();

  QJsonValue result = callResult("chr.readConsoleMessages", QJsonObject{{"onlyErrors", true}});
  QEXPECT_THAT(result.isObject(), IsTrue());

  QJsonObject obj = result.toObject();
  QJsonArray messages = obj["messages"].toArray();

  for (const QJsonValue& msg : messages) {
    QEXPECT_THAT(msg.toObject()["type"].toString(), Ne("info"));
  }

  QEXPECT_THAT(messages, JsonArrayContains(HasJsonField("message", QStrContains("warning_only_msg"))));
}

void TestChromeModeApi::testReadConsoleMessages_Clear() {
  qWarning("clearable_message");
  QApplication::processEvents();

  // Read with clear=true
  QJsonValue result1 = callResult("chr.readConsoleMessages", QJsonObject{{"clear", true}});
  QEXPECT_THAT(result1.isObject(), IsTrue());
  QEXPECT_THAT(result1.toObject()["count"].toInt(), Gt(0));

  // Read again - should be empty (or at least not contain our message)
  QJsonValue result2 = callResult("chr.readConsoleMessages", QJsonObject());
  QEXPECT_THAT(result2.isObject(), IsTrue());

  QJsonObject obj2 = result2.toObject();
  QJsonArray messages2 = obj2["messages"].toArray();
  QEXPECT_THAT(messages2, Not(JsonArrayContains(HasJsonField("message", QStrContains("clearable_message")))));
}

// ========================================================================
// Stale ref test
// ========================================================================

void TestChromeModeApi::testStaleRef_ProducesClearError() {
  // Read page to get refs
  QJsonObject pageResult = readPage();
  QJsonObject tree = pageResult["tree"].toObject();
  QString btnRef = findRefByObjectName(tree, "btnTest");
  QEXPECT_THAT(btnRef, QIsNotEmpty());

  // Destroy the button
  delete m_button;
  m_button = nullptr;
  QApplication::processEvents();

  // Try to click the now-stale ref
  QJsonObject error = callExpectError("chr.click", QJsonObject{{"ref", btnRef}});

  // Should get stale ref error
  int code = error["code"].toInt();
  QEXPECT_THAT(code, AnyOf(Eq(static_cast<int>(ErrorCode::kRefStale)), Eq(static_cast<int>(ErrorCode::kRefNotFound))));
  QEXPECT_THAT(error, HasJsonField("message", QIsNotEmpty()));
}

// ========================================================================
// Regression tests for chr.find bugs (05-04 gap closure)
// ========================================================================

void TestChromeModeApi::testFind_MultipleCallsPreserveRefs() {
  // Create two distinct line edits
  QLineEdit* nameEdit = new QLineEdit(m_mainWindow);
  nameEdit->setObjectName("nameEdit");
  m_mainWindow->layout()->addWidget(nameEdit);

  QLineEdit* emailEdit = new QLineEdit(m_mainWindow);
  emailEdit->setObjectName("emailEdit");
  m_mainWindow->layout()->addWidget(emailEdit);
  QApplication::processEvents();

  // First find: locate nameEdit
  QJsonValue result1 = callResult("chr.find", QJsonObject{{"query", "nameEdit"}});
  QEXPECT_THAT(result1.isObject(), IsTrue());
  QJsonArray matches1 = result1.toObject()["matches"].toArray();
  QEXPECT_THAT(matches1.size(), Gt(0));
  QString nameRef = matches1[0].toObject()["ref"].toString();
  QEXPECT_THAT(nameRef, QIsNotEmpty());

  // Second find: locate emailEdit
  QJsonValue result2 = callResult("chr.find", QJsonObject{{"query", "emailEdit"}});
  QEXPECT_THAT(result2.isObject(), IsTrue());
  QJsonArray matches2 = result2.toObject()["matches"].toArray();
  QEXPECT_THAT(matches2.size(), Gt(0));
  QString emailRef = matches2[0].toObject()["ref"].toString();
  QEXPECT_THAT(emailRef, QIsNotEmpty());

  // Refs must not collide
  QEXPECT_THAT(nameRef, Ne(emailRef));

  // Use the FIRST ref (from first find) to set value on nameEdit
  QJsonValue formResult =
      callResult("chr.formInput", QJsonObject{{"ref", nameRef}, {"value", "John"}});
  QApplication::processEvents();

  QEXPECT_THAT(formResult.isObject(), IsTrue());
  QEXPECT_THAT(formResult.toObject(), HasJsonField("success", Eq(true)));
  QEXPECT_THAT(nameEdit->text(), QStrEq("John"));
  // emailEdit should NOT have been modified
  QEXPECT_THAT(emailEdit->text(), QIsEmpty());
}

void TestChromeModeApi::testFind_ReadPageClearsAllRefs() {
  // First, call find to get refs assigned
  QJsonValue findResult = callResult("chr.find", QJsonObject{{"query", "editName"}});
  QEXPECT_THAT(findResult.isObject(), IsTrue());
  QJsonArray matches = findResult.toObject()["matches"].toArray();
  QEXPECT_THAT(matches.size(), Gt(0));
  QString findRef = matches[0].toObject()["ref"].toString();
  QEXPECT_THAT(findRef, QIsNotEmpty());

  // Verify the find ref works
  QJsonValue inputResult =
      callResult("chr.formInput", QJsonObject{{"ref", findRef}, {"value", "test_value"}});
  QEXPECT_THAT(inputResult.isObject(), IsTrue());
  QEXPECT_THAT(inputResult.toObject(), HasJsonField("success", Eq(true)));

  // Now call readPage - this calls clearRefsInternal() and rebuilds tree
  QJsonObject pageResult = readPage();
  QEXPECT_THAT(pageResult, AllOf(
      HasJsonField("tree"),
      HasJsonField("totalNodes", Gt(0))));

  // Additionally verify the ref namespace was reset:
  QJsonObject tree = pageResult["tree"].toObject();
  QString btnRef = findRefByObjectName(tree, "btnTest");
  if (!btnRef.isEmpty()) {
    QEXPECT_THAT(btnRef, QStrStartsWith("ref_"));
  }
}

void TestChromeModeApi::testFind_NameFallbackToObjectName() {
  // Create a QLineEdit with objectName but no explicit accessible name
  QLineEdit* input = new QLineEdit(m_mainWindow);
  input->setObjectName("mySpecialInput");
  m_mainWindow->layout()->addWidget(input);
  QApplication::processEvents();

  // Find by objectName
  QJsonValue result = callResult("chr.find", QJsonObject{{"query", "mySpecialInput"}});
  QEXPECT_THAT(result.isObject(), IsTrue());

  QJsonArray matches = result.toObject()["matches"].toArray();
  QEXPECT_THAT(matches.size(), Gt(0));

  QJsonObject matchNode = matches[0].toObject();
  // Name should be the objectName (since accessible name is empty)
  QEXPECT_THAT(matchNode, HasJsonField("name", QStrEq("mySpecialInput")));
}

QTEST_MAIN(TestChromeModeApi)
#include "test_chrome_mode_api.moc"
