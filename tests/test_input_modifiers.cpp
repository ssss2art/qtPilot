// Copyright (c) 2024-2026 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "api/computer_use_mode_api.h"
#include "api/error_codes.h"
#include "api/native_mode_api.h"
#include "common/qt_matchers.h"
#include "core/object_registry.h"
#include "interaction/modifier_parser.h"
#include "transport/jsonrpc_handler.h"

#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QtTest>

using namespace qtPilot;
using namespace qtPilot::test;

namespace {

/// @brief A widget that remembers the modifiers of the mouse events it receives.
///
/// The point of the feature is that the modifiers actually reach the target, so
/// the tests assert on what the widget was handed rather than on what the API
/// echoed back.
class ModifierRecordingWidget : public QWidget {
  Q_OBJECT

 public:
  using QWidget::QWidget;

  Qt::KeyboardModifiers lastPressModifiers = Qt::NoModifier;
  Qt::KeyboardModifiers lastReleaseModifiers = Qt::NoModifier;
  Qt::KeyboardModifiers lastDoubleClickModifiers = Qt::NoModifier;
  Qt::KeyboardModifiers lastMoveModifiers = Qt::NoModifier;
  Qt::KeyboardModifiers lastWheelModifiers = Qt::NoModifier;
  int pressCount = 0;
  int moveCount = 0;

  void forget() {
    lastPressModifiers = Qt::NoModifier;
    lastReleaseModifiers = Qt::NoModifier;
    lastDoubleClickModifiers = Qt::NoModifier;
    lastMoveModifiers = Qt::NoModifier;
    lastWheelModifiers = Qt::NoModifier;
    pressCount = 0;
    moveCount = 0;
  }

 protected:
  void mousePressEvent(QMouseEvent* event) override {
    lastPressModifiers = event->modifiers();
    ++pressCount;
    QWidget::mousePressEvent(event);
  }
  void mouseReleaseEvent(QMouseEvent* event) override {
    lastReleaseModifiers = event->modifiers();
    QWidget::mouseReleaseEvent(event);
  }
  void mouseDoubleClickEvent(QMouseEvent* event) override {
    lastDoubleClickModifiers = event->modifiers();
    QWidget::mouseDoubleClickEvent(event);
  }
  void mouseMoveEvent(QMouseEvent* event) override {
    lastMoveModifiers = event->modifiers();
    ++moveCount;
    QWidget::mouseMoveEvent(event);
  }
  void wheelEvent(QWheelEvent* event) override {
    lastWheelModifiers = event->modifiers();
    QWidget::wheelEvent(event);
  }
};

/// @brief A widget that remembers the key events it receives.
class KeyRecordingWidget : public QWidget {
  Q_OBJECT

 public:
  using QWidget::QWidget;

  Qt::KeyboardModifiers lastKeyModifiers = Qt::NoModifier;
  QString typedText;
  int keyPressCount = 0;

  void forget() {
    lastKeyModifiers = Qt::NoModifier;
    typedText.clear();
    keyPressCount = 0;
  }

 protected:
  void keyPressEvent(QKeyEvent* event) override {
    lastKeyModifiers = event->modifiers();
    typedText += event->text();
    ++keyPressCount;
    QWidget::keyPressEvent(event);
  }
};

}  // namespace

// ============================================================================
// Matchers
// ============================================================================

/// @brief Matches Qt::KeyboardModifiers against an expected flag combination.
///
/// Prints both sides by name, so a failure reads "modifiers were Ctrl|Shift,
/// expected Ctrl" rather than comparing two integers.
MATCHER_P(HasModifiers, expected, "") {
  const Qt::KeyboardModifiers actual = arg;
  const Qt::KeyboardModifiers want = expected;
  auto describe = [](Qt::KeyboardModifiers mods) -> std::string {
    if (mods == Qt::NoModifier) {
      return "NoModifier";
    }
    QStringList parts;
    if (mods & Qt::ShiftModifier)
      parts << QStringLiteral("Shift");
    if (mods & Qt::ControlModifier)
      parts << QStringLiteral("Ctrl");
    if (mods & Qt::AltModifier)
      parts << QStringLiteral("Alt");
    if (mods & Qt::MetaModifier)
      parts << QStringLiteral("Meta");
    if (mods & Qt::KeypadModifier)
      parts << QStringLiteral("Keypad");
    return parts.join(QStringLiteral("|")).toStdString();
  };
  if (actual != want) {
    *result_listener << "modifiers were " << describe(actual) << ", expected " << describe(want);
    return false;
  }
  *result_listener << "modifiers are " << describe(actual);
  return true;
}

/// @brief Exhaustive coverage of the shared `modifiers` input parameter.
///
/// Three layers, because each can break on its own:
/// 1. ModifierParser - every accepted spelling, every rejection.
/// 2. qt.ui.* - modifiers reach a widget and a graphics item.
/// 3. cu.* - modifiers reach every coordinate-addressed mouse method.
class TestInputModifiers : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase();
  void cleanupTestCase();
  void init();

  // --- ModifierParser: accepted spellings ---
  void testParseAbsentIsNoModifier();
  void testParseNullIsNoModifier();
  void testParseEmptyStringIsNoModifier();
  void testParseEmptyArrayIsNoModifier();
  void testParseSingleName_data();
  void testParseSingleName();
  void testParseIsCaseInsensitive_data();
  void testParseIsCaseInsensitive();
  void testParseJoinedString_data();
  void testParseJoinedString();
  void testParseArrayOfNames();
  void testParseAllModifiersAtOnce();
  void testParseIgnoresSurroundingWhitespace();
  void testParseIsIdempotentOnRepeats();
  void testAcceptedNamesCoversEveryAlias();

  // --- ModifierParser: rejections ---
  void testParseRejectsUnknownName();
  void testParseRejectsNumber();
  void testParseRejectsBool();
  void testParseRejectsObject();
  void testParseRejectsNonStringArrayElement();
  void testParseRejectsEmptySegmentInJoinedString();
  void testRejectionNamesTheOffendingValue();

  // --- qt.ui.* delivery ---
  void testUiClickDeliversModifiers();
  void testUiClickWithoutModifiersIsNoModifier();
  void testUiClickDeliversModifiersToRelease();
  void testUiDoubleClickDeliversModifiers();
  void testUiClickRejectsBadModifiers();

  // --- qt.ui.sendKeys ---
  void testSendKeysTextCarriesModifiers();
  void testSendKeysTextWithoutModifiersIsNoModifier();
  void testSendKeysSequenceStillCarriesItsOwnModifiers();
  void testSendKeysRejectsModifiersCombinedWithSequence();
  void testSendKeysRejectsBadModifiers();

  // --- cu.* delivery ---
  void testCuClickDeliversModifiers();
  void testCuRightClickDeliversModifiers();
  void testCuDoubleClickDeliversModifiers();
  void testCuMouseDownDeliversModifiers();
  void testCuMouseUpDeliversModifiers();
  void testCuMouseMoveDeliversModifiers();
  void testCuDragDeliversModifiersThroughout();
  void testCuScrollDeliversModifiers();
  void testCuClickRejectsBadModifiers();

 private:
  QJsonObject callRaw(const QString& method, const QJsonObject& params);
  QJsonObject callOk(const QString& method, const QJsonObject& params);
  void pumpQueuedInput();

  JsonRpcHandler* m_handler = nullptr;
  NativeModeApi* m_nativeApi = nullptr;
  ComputerUseModeApi* m_cuApi = nullptr;
  QWidget* m_window = nullptr;
  ModifierRecordingWidget* m_target = nullptr;
  KeyRecordingWidget* m_keyTarget = nullptr;
  int m_requestId = 1;
};

void TestInputModifiers::initTestCase() {
  m_window = new QWidget();
  m_window->setObjectName(QStringLiteral("modifierWindow"));
  m_window->setGeometry(100, 100, 400, 300);

  auto* layout = new QVBoxLayout(m_window);
  m_target = new ModifierRecordingWidget(m_window);
  m_target->setObjectName(QStringLiteral("modifierTarget"));
  m_target->setMinimumSize(200, 150);
  m_target->setMouseTracking(true);
  layout->addWidget(m_target);

  m_keyTarget = new KeyRecordingWidget(m_window);
  m_keyTarget->setObjectName(QStringLiteral("keyTarget"));
  m_keyTarget->setFocusPolicy(Qt::StrongFocus);
  m_keyTarget->setMinimumSize(200, 60);
  layout->addWidget(m_keyTarget);

  m_window->show();
  QVERIFY(QTest::qWaitForWindowExposed(m_window));

  m_handler = new JsonRpcHandler(this);
  // Both constructors register their whole method surface on the handler.
  m_nativeApi = new NativeModeApi(m_handler, this);
  m_cuApi = new ComputerUseModeApi(m_handler, this);
}

void TestInputModifiers::cleanupTestCase() {
  delete m_window;
  m_window = nullptr;
}

void TestInputModifiers::init() {
  if (m_target) {
    m_target->forget();
  }
  if (m_keyTarget) {
    m_keyTarget->forget();
  }
}

QJsonObject TestInputModifiers::callRaw(const QString& method, const QJsonObject& params) {
  QJsonObject request;
  request[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
  request[QStringLiteral("method")] = method;
  request[QStringLiteral("params")] = params;
  request[QStringLiteral("id")] = m_requestId++;

  const QString requestStr =
      QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact));
  return QJsonDocument::fromJson(m_handler->HandleMessage(requestStr).toUtf8()).object();
}

QJsonObject TestInputModifiers::callOk(const QString& method, const QJsonObject& params) {
  const QJsonObject response = callRaw(method, params);
  QCHECK_THAT(response, IsJsonRpcSuccess());
  pumpQueuedInput();
  return response;
}

/// qt.ui.click queues the synthesized event so the RPC can return first.
void TestInputModifiers::pumpQueuedInput() {
  QCoreApplication::processEvents();
  QTest::qWait(20);
  QCoreApplication::processEvents();
}

// ============================================================================
// ModifierParser: accepted spellings
// ============================================================================

void TestInputModifiers::testParseAbsentIsNoModifier() {
  QEXPECT_THAT(ModifierParser::parse(QJsonValue(QJsonValue::Undefined), QStringLiteral("t")),
               HasModifiers(Qt::NoModifier));
}

void TestInputModifiers::testParseNullIsNoModifier() {
  QEXPECT_THAT(ModifierParser::parse(QJsonValue(QJsonValue::Null), QStringLiteral("t")),
               HasModifiers(Qt::NoModifier));
}

void TestInputModifiers::testParseEmptyStringIsNoModifier() {
  QEXPECT_THAT(ModifierParser::parse(QJsonValue(QString()), QStringLiteral("t")),
               HasModifiers(Qt::NoModifier));
}

void TestInputModifiers::testParseEmptyArrayIsNoModifier() {
  QEXPECT_THAT(ModifierParser::parse(QJsonValue(QJsonArray()), QStringLiteral("t")),
               HasModifiers(Qt::NoModifier));
}

void TestInputModifiers::testParseSingleName_data() {
  QTest::addColumn<QString>("name");
  QTest::addColumn<Qt::KeyboardModifiers>("expected");

  QTest::newRow("ctrl") << "ctrl" << Qt::KeyboardModifiers(Qt::ControlModifier);
  QTest::newRow("control") << "control" << Qt::KeyboardModifiers(Qt::ControlModifier);
  QTest::newRow("shift") << "shift" << Qt::KeyboardModifiers(Qt::ShiftModifier);
  QTest::newRow("alt") << "alt" << Qt::KeyboardModifiers(Qt::AltModifier);
  QTest::newRow("option") << "option" << Qt::KeyboardModifiers(Qt::AltModifier);
  QTest::newRow("meta") << "meta" << Qt::KeyboardModifiers(Qt::MetaModifier);
  QTest::newRow("super") << "super" << Qt::KeyboardModifiers(Qt::MetaModifier);
  QTest::newRow("win") << "win" << Qt::KeyboardModifiers(Qt::MetaModifier);
  QTest::newRow("cmd") << "cmd" << Qt::KeyboardModifiers(Qt::ControlModifier);
  QTest::newRow("command") << "command" << Qt::KeyboardModifiers(Qt::ControlModifier);
  QTest::newRow("keypad") << "keypad" << Qt::KeyboardModifiers(Qt::KeypadModifier);
}

void TestInputModifiers::testParseSingleName() {
  QFETCH(QString, name);
  QFETCH(Qt::KeyboardModifiers, expected);
  QEXPECT_THAT(ModifierParser::parse(QJsonValue(name), QStringLiteral("t")),
               HasModifiers(expected));
}

void TestInputModifiers::testParseIsCaseInsensitive_data() {
  QTest::addColumn<QString>("name");
  QTest::newRow("upper") << "CTRL";
  QTest::newRow("mixed") << "CtRl";
  QTest::newRow("title") << "Ctrl";
  QTest::newRow("lower") << "ctrl";
}

void TestInputModifiers::testParseIsCaseInsensitive() {
  QFETCH(QString, name);
  QEXPECT_THAT(ModifierParser::parse(QJsonValue(name), QStringLiteral("t")),
               HasModifiers(Qt::ControlModifier));
}

void TestInputModifiers::testParseJoinedString_data() {
  QTest::addColumn<QString>("joined");
  QTest::addColumn<Qt::KeyboardModifiers>("expected");

  QTest::newRow("two") << "ctrl+shift"
                       << Qt::KeyboardModifiers(Qt::ControlModifier | Qt::ShiftModifier);
  QTest::newRow("three") << "ctrl+shift+alt"
                         << Qt::KeyboardModifiers(Qt::ControlModifier | Qt::ShiftModifier |
                                                  Qt::AltModifier);
  QTest::newRow("aliases mix") << "control+option"
                               << Qt::KeyboardModifiers(Qt::ControlModifier | Qt::AltModifier);
  QTest::newRow("order does not matter")
      << "shift+ctrl" << Qt::KeyboardModifiers(Qt::ControlModifier | Qt::ShiftModifier);
}

void TestInputModifiers::testParseJoinedString() {
  QFETCH(QString, joined);
  QFETCH(Qt::KeyboardModifiers, expected);
  QEXPECT_THAT(ModifierParser::parse(QJsonValue(joined), QStringLiteral("t")),
               HasModifiers(expected));
}

void TestInputModifiers::testParseArrayOfNames() {
  const QJsonArray names{QStringLiteral("ctrl"), QStringLiteral("shift")};
  QEXPECT_THAT(ModifierParser::parse(QJsonValue(names), QStringLiteral("t")),
               HasModifiers(Qt::ControlModifier | Qt::ShiftModifier));
}

void TestInputModifiers::testParseAllModifiersAtOnce() {
  const QJsonArray names{QStringLiteral("ctrl"), QStringLiteral("shift"), QStringLiteral("alt"),
                         QStringLiteral("meta"), QStringLiteral("keypad")};
  QEXPECT_THAT(ModifierParser::parse(QJsonValue(names), QStringLiteral("t")),
               HasModifiers(Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier |
                            Qt::MetaModifier | Qt::KeypadModifier));
}

void TestInputModifiers::testParseIgnoresSurroundingWhitespace() {
  QEXPECT_THAT(
      ModifierParser::parse(QJsonValue(QStringLiteral(" ctrl + shift ")), QStringLiteral("t")),
      HasModifiers(Qt::ControlModifier | Qt::ShiftModifier));
}

void TestInputModifiers::testParseIsIdempotentOnRepeats() {
  QEXPECT_THAT(
      ModifierParser::parse(QJsonValue(QStringLiteral("ctrl+ctrl+control")), QStringLiteral("t")),
      HasModifiers(Qt::ControlModifier));
}

void TestInputModifiers::testAcceptedNamesCoversEveryAlias() {
  const QStringList accepted = ModifierParser::acceptedNames();
  // Every name the parser documents must actually parse, and the list must be
  // the single source of truth used by the error payload.
  QEXPECT_THAT(accepted, QIsNotEmpty());
  for (const QString& name : accepted) {
    bool threw = false;
    try {
      ModifierParser::parse(QJsonValue(name), QStringLiteral("t"));
    } catch (const JsonRpcException&) {
      threw = true;
    }
    QVERIFY2(!threw, qPrintable(QStringLiteral("accepted name rejected: %1").arg(name)));
  }
}

// ============================================================================
// ModifierParser: rejections
// ============================================================================

void TestInputModifiers::testParseRejectsUnknownName() {
  QVERIFY_THROWS_EXCEPTION(
      JsonRpcException,
      ModifierParser::parse(QJsonValue(QStringLiteral("hyper")), QStringLiteral("t")));
}

void TestInputModifiers::testParseRejectsNumber() {
  QVERIFY_THROWS_EXCEPTION(JsonRpcException,
                           ModifierParser::parse(QJsonValue(4), QStringLiteral("t")));
}

void TestInputModifiers::testParseRejectsBool() {
  QVERIFY_THROWS_EXCEPTION(JsonRpcException,
                           ModifierParser::parse(QJsonValue(true), QStringLiteral("t")));
}

void TestInputModifiers::testParseRejectsObject() {
  QJsonObject obj;
  obj[QStringLiteral("ctrl")] = true;
  QVERIFY_THROWS_EXCEPTION(JsonRpcException,
                           ModifierParser::parse(QJsonValue(obj), QStringLiteral("t")));
}

void TestInputModifiers::testParseRejectsNonStringArrayElement() {
  const QJsonArray names{QStringLiteral("ctrl"), 7};
  QVERIFY_THROWS_EXCEPTION(JsonRpcException,
                           ModifierParser::parse(QJsonValue(names), QStringLiteral("t")));
}

void TestInputModifiers::testParseRejectsEmptySegmentInJoinedString() {
  QVERIFY_THROWS_EXCEPTION(
      JsonRpcException,
      ModifierParser::parse(QJsonValue(QStringLiteral("ctrl++shift")), QStringLiteral("t")));
}

void TestInputModifiers::testRejectionNamesTheOffendingValue() {
  bool sawPayload = false;
  try {
    ModifierParser::parse(QJsonValue(QStringLiteral("hyper")), QStringLiteral("qt.ui.click"));
  } catch (const JsonRpcException& ex) {
    const QJsonObject data = ex.data();
    QEXPECT_THAT(data, HasJsonField(QStringLiteral("modifiers")));
    QEXPECT_THAT(data, HasJsonField(QStringLiteral("accepted")));
    QEXPECT_THAT(data.value(QStringLiteral("method")).toString(), QStrEq("qt.ui.click"));
    sawPayload = true;
  }
  QVERIFY2(sawPayload, "expected a JsonRpcException carrying a diagnostic payload");
}

// ============================================================================
// qt.ui.* delivery
// ============================================================================

void TestInputModifiers::testUiClickDeliversModifiers() {
  const QString objectId = ObjectRegistry::instance()->objectId(m_target);
  QJsonObject params;
  params[QStringLiteral("objectId")] = objectId;
  params[QStringLiteral("modifiers")] = QStringLiteral("ctrl+shift");
  callOk(QStringLiteral("qt.ui.click"), params);

  QEXPECT_THAT(m_target->lastPressModifiers, HasModifiers(Qt::ControlModifier | Qt::ShiftModifier));
}

void TestInputModifiers::testUiClickWithoutModifiersIsNoModifier() {
  const QString objectId = ObjectRegistry::instance()->objectId(m_target);
  QJsonObject params;
  params[QStringLiteral("objectId")] = objectId;
  callOk(QStringLiteral("qt.ui.click"), params);

  QEXPECT_THAT(m_target->lastPressModifiers, HasModifiers(Qt::NoModifier));
}

void TestInputModifiers::testUiClickDeliversModifiersToRelease() {
  const QString objectId = ObjectRegistry::instance()->objectId(m_target);
  QJsonObject params;
  params[QStringLiteral("objectId")] = objectId;
  params[QStringLiteral("modifiers")] = QStringLiteral("alt");
  callOk(QStringLiteral("qt.ui.click"), params);

  // A press that reports the modifier and a release that forgets it leaves the
  // app's own press/release pairing inconsistent, so both are pinned.
  QEXPECT_THAT(m_target->lastReleaseModifiers, HasModifiers(Qt::AltModifier));
}

void TestInputModifiers::testUiDoubleClickDeliversModifiers() {
  const QString objectId = ObjectRegistry::instance()->objectId(m_target);
  QJsonObject params;
  params[QStringLiteral("objectId")] = objectId;
  params[QStringLiteral("modifiers")] = QJsonArray{QStringLiteral("shift")};
  callOk(QStringLiteral("qt.ui.doubleClick"), params);

  QEXPECT_THAT(m_target->lastDoubleClickModifiers, HasModifiers(Qt::ShiftModifier));
}

void TestInputModifiers::testUiClickRejectsBadModifiers() {
  const QString objectId = ObjectRegistry::instance()->objectId(m_target);
  QJsonObject params;
  params[QStringLiteral("objectId")] = objectId;
  params[QStringLiteral("modifiers")] = QStringLiteral("hyper");

  QEXPECT_THAT(callRaw(QStringLiteral("qt.ui.click"), params),
               IsJsonRpcError(static_cast<int>(JsonRpcError::kInvalidParams)));
}

// ============================================================================
// qt.ui.sendKeys
//
// `sequence` already spells its own modifiers ("Ctrl+S"), so `modifiers` exists
// for `text`, which had no way to say them at all. Supplying both is refused
// rather than merged: two sources for the same thing is a silent-conflict bug
// waiting to happen, and the caller almost certainly meant one or the other.
// ============================================================================

void TestInputModifiers::testSendKeysTextCarriesModifiers() {
  const QString objectId = ObjectRegistry::instance()->objectId(m_keyTarget);
  QJsonObject params;
  params[QStringLiteral("objectId")] = objectId;
  params[QStringLiteral("text")] = QStringLiteral("a");
  params[QStringLiteral("modifiers")] = QStringLiteral("ctrl");
  callOk(QStringLiteral("qt.ui.sendKeys"), params);

  QEXPECT_THAT(m_keyTarget->lastKeyModifiers, HasModifiers(Qt::ControlModifier));
}

void TestInputModifiers::testSendKeysTextWithoutModifiersIsNoModifier() {
  const QString objectId = ObjectRegistry::instance()->objectId(m_keyTarget);
  QJsonObject params;
  params[QStringLiteral("objectId")] = objectId;
  params[QStringLiteral("text")] = QStringLiteral("a");
  callOk(QStringLiteral("qt.ui.sendKeys"), params);

  QEXPECT_THAT(m_keyTarget->lastKeyModifiers, HasModifiers(Qt::NoModifier));
}

void TestInputModifiers::testSendKeysSequenceStillCarriesItsOwnModifiers() {
  const QString objectId = ObjectRegistry::instance()->objectId(m_keyTarget);
  QJsonObject params;
  params[QStringLiteral("objectId")] = objectId;
  params[QStringLiteral("sequence")] = QStringLiteral("Ctrl+Shift+A");
  callOk(QStringLiteral("qt.ui.sendKeys"), params);

  // Unchanged behaviour: the sequence string is the source of truth here.
  QEXPECT_THAT(m_keyTarget->lastKeyModifiers,
               HasModifiers(Qt::ControlModifier | Qt::ShiftModifier));
}

void TestInputModifiers::testSendKeysRejectsModifiersCombinedWithSequence() {
  const QString objectId = ObjectRegistry::instance()->objectId(m_keyTarget);
  QJsonObject params;
  params[QStringLiteral("objectId")] = objectId;
  params[QStringLiteral("sequence")] = QStringLiteral("Ctrl+S");
  params[QStringLiteral("modifiers")] = QStringLiteral("shift");

  QEXPECT_THAT(callRaw(QStringLiteral("qt.ui.sendKeys"), params),
               IsJsonRpcError(static_cast<int>(JsonRpcError::kInvalidParams)));
}

void TestInputModifiers::testSendKeysRejectsBadModifiers() {
  const QString objectId = ObjectRegistry::instance()->objectId(m_keyTarget);
  QJsonObject params;
  params[QStringLiteral("objectId")] = objectId;
  params[QStringLiteral("text")] = QStringLiteral("a");
  params[QStringLiteral("modifiers")] = QStringLiteral("hyper");

  QEXPECT_THAT(callRaw(QStringLiteral("qt.ui.sendKeys"), params),
               IsJsonRpcError(static_cast<int>(JsonRpcError::kInvalidParams)));
}

// ============================================================================
// cu.* delivery
// ============================================================================

namespace {

QJsonObject cuPoint(int x, int y, const QString& modifiers) {
  QJsonObject params;
  params[QStringLiteral("x")] = x;
  params[QStringLiteral("y")] = y;
  if (!modifiers.isEmpty()) {
    params[QStringLiteral("modifiers")] = modifiers;
  }
  return params;
}

}  // namespace

void TestInputModifiers::testCuClickDeliversModifiers() {
  const QPoint local = m_target->mapTo(m_window, m_target->rect().center());
  callOk(QStringLiteral("cu.click"), cuPoint(local.x(), local.y(), QStringLiteral("ctrl")));

  QEXPECT_THAT(m_target->lastPressModifiers, HasModifiers(Qt::ControlModifier));
}

void TestInputModifiers::testCuRightClickDeliversModifiers() {
  const QPoint local = m_target->mapTo(m_window, m_target->rect().center());
  callOk(QStringLiteral("cu.rightClick"), cuPoint(local.x(), local.y(), QStringLiteral("shift")));

  QEXPECT_THAT(m_target->lastPressModifiers, HasModifiers(Qt::ShiftModifier));
}

void TestInputModifiers::testCuDoubleClickDeliversModifiers() {
  const QPoint local = m_target->mapTo(m_window, m_target->rect().center());
  callOk(QStringLiteral("cu.doubleClick"), cuPoint(local.x(), local.y(), QStringLiteral("alt")));

  QEXPECT_THAT(m_target->lastDoubleClickModifiers, HasModifiers(Qt::AltModifier));
}

void TestInputModifiers::testCuMouseDownDeliversModifiers() {
  const QPoint local = m_target->mapTo(m_window, m_target->rect().center());
  callOk(QStringLiteral("cu.mouseDown"), cuPoint(local.x(), local.y(), QStringLiteral("ctrl")));

  QEXPECT_THAT(m_target->lastPressModifiers, HasModifiers(Qt::ControlModifier));

  // Leave no button held for the tests that follow.
  callOk(QStringLiteral("cu.mouseUp"), cuPoint(local.x(), local.y(), QString()));
}

void TestInputModifiers::testCuMouseUpDeliversModifiers() {
  const QPoint local = m_target->mapTo(m_window, m_target->rect().center());
  callOk(QStringLiteral("cu.mouseDown"), cuPoint(local.x(), local.y(), QStringLiteral("ctrl")));
  callOk(QStringLiteral("cu.mouseUp"), cuPoint(local.x(), local.y(), QStringLiteral("ctrl")));

  QEXPECT_THAT(m_target->lastReleaseModifiers, HasModifiers(Qt::ControlModifier));
}

void TestInputModifiers::testCuMouseMoveDeliversModifiers() {
  const QPoint local = m_target->mapTo(m_window, m_target->rect().center());
  callOk(QStringLiteral("cu.mouseMove"), cuPoint(local.x(), local.y(), QStringLiteral("shift")));

  QEXPECT_THAT(m_target->lastMoveModifiers, HasModifiers(Qt::ShiftModifier));
}

void TestInputModifiers::testCuDragDeliversModifiersThroughout() {
  const QPoint start = m_target->mapTo(m_window, QPoint(20, 20));
  const QPoint end = m_target->mapTo(m_window, QPoint(120, 100));

  QJsonObject params;
  params[QStringLiteral("startX")] = start.x();
  params[QStringLiteral("startY")] = start.y();
  params[QStringLiteral("endX")] = end.x();
  params[QStringLiteral("endY")] = end.y();
  params[QStringLiteral("modifiers")] = QStringLiteral("ctrl");
  callOk(QStringLiteral("cu.drag"), params);

  // The press starts the gesture and the release ends it; an app that reads the
  // modifier at either end has to see it, so both are asserted.
  QEXPECT_THAT(m_target->lastPressModifiers, HasModifiers(Qt::ControlModifier));
  QEXPECT_THAT(m_target->lastReleaseModifiers, HasModifiers(Qt::ControlModifier));
}

void TestInputModifiers::testCuScrollDeliversModifiers() {
  const QPoint local = m_target->mapTo(m_window, m_target->rect().center());
  QJsonObject params = cuPoint(local.x(), local.y(), QStringLiteral("ctrl"));
  params[QStringLiteral("direction")] = QStringLiteral("down");
  params[QStringLiteral("amount")] = 3;
  callOk(QStringLiteral("cu.scroll"), params);

  // Ctrl+scroll is the near-universal zoom gesture, so this one carries weight.
  QEXPECT_THAT(m_target->lastWheelModifiers, HasModifiers(Qt::ControlModifier));
}

void TestInputModifiers::testCuClickRejectsBadModifiers() {
  const QPoint local = m_target->mapTo(m_window, m_target->rect().center());
  QEXPECT_THAT(
      callRaw(QStringLiteral("cu.click"), cuPoint(local.x(), local.y(), QStringLiteral("hyper"))),
      IsJsonRpcError(static_cast<int>(JsonRpcError::kInvalidParams)));
}

QTEST_MAIN(TestInputModifiers)
#include "test_input_modifiers.moc"
