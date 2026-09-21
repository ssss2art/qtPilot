// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "common/qt_matchers.h"
#include "interaction/key_name_mapper.h"

#include <QtTest>

using namespace qtPilot;
using namespace qtPilot::test;

inline void PrintTo(const KeyCombo& combo, std::ostream* os) {
  *os << "KeyCombo(key=" << combo.key << ", modifiers=" << static_cast<int>(combo.modifiers) << ")";
}

MATCHER_P2(MatchesKeyCombo, expectedKey, expectedModifiers, "") {
  if (arg.key != expectedKey) {
    *result_listener << "key was " << arg.key << ", expected " << expectedKey;
    return false;
  }
  if (arg.modifiers != expectedModifiers) {
    *result_listener << "modifiers were " << static_cast<int>(arg.modifiers) << ", expected "
                     << static_cast<int>(expectedModifiers);
    return false;
  }
  return true;
}

/// @brief Unit tests for KeyNameMapper: key name resolution and combo parsing.
///
/// Verifies that Chrome/xdotool key names are correctly mapped to Qt::Key values,
/// including case-insensitive lookup, single-character keys, unknown keys,
/// and modifier+key combination parsing.
class TestKeyNameMapper : public QObject {
  Q_OBJECT

 private slots:
  // Key resolution tests
  void testResolveNavigationKeys();
  void testResolveArrowKeys();
  void testResolveFunctionKeys();
  void testResolveModifierKeys();
  void testResolveCaseInsensitive();
  void testResolveUnknown();
  void testResolveSingleChar();
  void testResolveNamedPunctuation();

  // Combo parsing tests
  void testParseKeyCombo_Simple();
  void testParseKeyCombo_WithModifiers();
  void testParseKeyCombo_ChromeStyle();
  void testParseKeyCombo_NamedPunctuation();

  // Monadic C++23 tests
  void testResolveExpectedMonadic();
  void testParseKeyComboExpectedMonadic();
};

// ========================================================================
// Key Resolution Tests
// ========================================================================

void TestKeyNameMapper::testResolveNavigationKeys() {
  // Return and Enter both map to Key_Return
  QEXPECT_THAT(KeyNameMapper::resolve("Return"), Eq(Qt::Key_Return));
  QEXPECT_THAT(KeyNameMapper::resolve("Enter"), Eq(Qt::Key_Return));

  // Tab
  QEXPECT_THAT(KeyNameMapper::resolve("Tab"), Eq(Qt::Key_Tab));

  // Escape variants
  QEXPECT_THAT(KeyNameMapper::resolve("Escape"), Eq(Qt::Key_Escape));
  QEXPECT_THAT(KeyNameMapper::resolve("Esc"), Eq(Qt::Key_Escape));

  // Backspace variants
  QEXPECT_THAT(KeyNameMapper::resolve("BackSpace"), Eq(Qt::Key_Backspace));
  QEXPECT_THAT(KeyNameMapper::resolve("Backspace"), Eq(Qt::Key_Backspace));

  // Delete
  QEXPECT_THAT(KeyNameMapper::resolve("Delete"), Eq(Qt::Key_Delete));

  // Space variants
  QEXPECT_THAT(KeyNameMapper::resolve("Space"), Eq(Qt::Key_Space));
  QEXPECT_THAT(KeyNameMapper::resolve("space"), Eq(Qt::Key_Space));
}

void TestKeyNameMapper::testResolveArrowKeys() {
  // xdotool-style
  QEXPECT_THAT(KeyNameMapper::resolve("Up"), Eq(Qt::Key_Up));
  QEXPECT_THAT(KeyNameMapper::resolve("Down"), Eq(Qt::Key_Down));
  QEXPECT_THAT(KeyNameMapper::resolve("Left"), Eq(Qt::Key_Left));
  QEXPECT_THAT(KeyNameMapper::resolve("Right"), Eq(Qt::Key_Right));

  // Chrome-style
  QEXPECT_THAT(KeyNameMapper::resolve("ArrowUp"), Eq(Qt::Key_Up));
  QEXPECT_THAT(KeyNameMapper::resolve("ArrowDown"), Eq(Qt::Key_Down));
  QEXPECT_THAT(KeyNameMapper::resolve("ArrowLeft"), Eq(Qt::Key_Left));
  QEXPECT_THAT(KeyNameMapper::resolve("ArrowRight"), Eq(Qt::Key_Right));
}

void TestKeyNameMapper::testResolveFunctionKeys() {
  QEXPECT_THAT(KeyNameMapper::resolve("F1"), Eq(Qt::Key_F1));
  QEXPECT_THAT(KeyNameMapper::resolve("F2"), Eq(Qt::Key_F2));
  QEXPECT_THAT(KeyNameMapper::resolve("F3"), Eq(Qt::Key_F3));
  QEXPECT_THAT(KeyNameMapper::resolve("F4"), Eq(Qt::Key_F4));
  QEXPECT_THAT(KeyNameMapper::resolve("F5"), Eq(Qt::Key_F5));
  QEXPECT_THAT(KeyNameMapper::resolve("F6"), Eq(Qt::Key_F6));
  QEXPECT_THAT(KeyNameMapper::resolve("F7"), Eq(Qt::Key_F7));
  QEXPECT_THAT(KeyNameMapper::resolve("F8"), Eq(Qt::Key_F8));
  QEXPECT_THAT(KeyNameMapper::resolve("F9"), Eq(Qt::Key_F9));
  QEXPECT_THAT(KeyNameMapper::resolve("F10"), Eq(Qt::Key_F10));
  QEXPECT_THAT(KeyNameMapper::resolve("F11"), Eq(Qt::Key_F11));
  QEXPECT_THAT(KeyNameMapper::resolve("F12"), Eq(Qt::Key_F12));
}

void TestKeyNameMapper::testResolveModifierKeys() {
  // Shift variants
  QEXPECT_THAT(KeyNameMapper::resolve("Shift"), Eq(Qt::Key_Shift));
  QEXPECT_THAT(KeyNameMapper::resolve("Shift_L"), Eq(Qt::Key_Shift));

  // Control variants
  QEXPECT_THAT(KeyNameMapper::resolve("Control"), Eq(Qt::Key_Control));
  QEXPECT_THAT(KeyNameMapper::resolve("Control_L"), Eq(Qt::Key_Control));

  // Alt variants
  QEXPECT_THAT(KeyNameMapper::resolve("Alt"), Eq(Qt::Key_Alt));
  QEXPECT_THAT(KeyNameMapper::resolve("Alt_L"), Eq(Qt::Key_Alt));

  // Super variants
  QEXPECT_THAT(KeyNameMapper::resolve("Super"), Eq(Qt::Key_Super_L));
  QEXPECT_THAT(KeyNameMapper::resolve("Super_L"), Eq(Qt::Key_Super_L));

  // Meta
  QEXPECT_THAT(KeyNameMapper::resolve("Meta"), Eq(Qt::Key_Meta));
}

void TestKeyNameMapper::testResolveCaseInsensitive() {
  // Return - different cases
  QEXPECT_THAT(KeyNameMapper::resolve("return"), Eq(Qt::Key_Return));
  QEXPECT_THAT(KeyNameMapper::resolve("RETURN"), Eq(Qt::Key_Return));
  QEXPECT_THAT(KeyNameMapper::resolve("Return"), Eq(Qt::Key_Return));

  // Escape - different cases
  QEXPECT_THAT(KeyNameMapper::resolve("escape"), Eq(Qt::Key_Escape));
  QEXPECT_THAT(KeyNameMapper::resolve("ESCAPE"), Eq(Qt::Key_Escape));
  QEXPECT_THAT(KeyNameMapper::resolve("Escape"), Eq(Qt::Key_Escape));

  // Tab - different cases
  QEXPECT_THAT(KeyNameMapper::resolve("tab"), Eq(Qt::Key_Tab));
  QEXPECT_THAT(KeyNameMapper::resolve("TAB"), Eq(Qt::Key_Tab));
  QEXPECT_THAT(KeyNameMapper::resolve("Tab"), Eq(Qt::Key_Tab));

  // Function keys are also case-insensitive
  QEXPECT_THAT(KeyNameMapper::resolve("f1"), Eq(Qt::Key_F1));
  QEXPECT_THAT(KeyNameMapper::resolve("F1"), Eq(Qt::Key_F1));
}

void TestKeyNameMapper::testResolveUnknown() {
  QEXPECT_THAT(KeyNameMapper::resolve("NotAKey"), Eq(Qt::Key_unknown));
  QEXPECT_THAT(KeyNameMapper::resolve("FooBar"), Eq(Qt::Key_unknown));
  QEXPECT_THAT(KeyNameMapper::resolve(""), Eq(Qt::Key_unknown));
}

void TestKeyNameMapper::testResolveSingleChar() {
  // Lowercase letters -> Key_A .. Key_Z
  QEXPECT_THAT(KeyNameMapper::resolve("a"), Eq(Qt::Key_A));
  QEXPECT_THAT(KeyNameMapper::resolve("z"), Eq(Qt::Key_Z));

  // Uppercase letters
  QEXPECT_THAT(KeyNameMapper::resolve("A"), Eq(Qt::Key_A));
  QEXPECT_THAT(KeyNameMapper::resolve("Z"), Eq(Qt::Key_Z));

  // Digits
  QEXPECT_THAT(KeyNameMapper::resolve("0"), Eq(Qt::Key_0));
  QEXPECT_THAT(KeyNameMapper::resolve("1"), Eq(Qt::Key_1));
  QEXPECT_THAT(KeyNameMapper::resolve("9"), Eq(Qt::Key_9));

  // Literal printable characters
  QEXPECT_THAT(KeyNameMapper::resolve("?"), Eq(Qt::Key_Question));
  QEXPECT_THAT(KeyNameMapper::resolve(" "), Eq(Qt::Key_Space));
  QEXPECT_THAT(KeyNameMapper::resolve("/"), Eq(Qt::Key_Slash));
}

void TestKeyNameMapper::testResolveNamedPunctuation() {
  QEXPECT_THAT(KeyNameMapper::resolve("Plus"), Eq(Qt::Key_Plus));
  QEXPECT_THAT(KeyNameMapper::resolve("Minus"), Eq(Qt::Key_Minus));
  QEXPECT_THAT(KeyNameMapper::resolve("Question"), Eq(Qt::Key_Question));
  QEXPECT_THAT(KeyNameMapper::resolve("QuestionMark"), Eq(Qt::Key_Question));
}

// ========================================================================
// Combo Parsing Tests
// ========================================================================

void TestKeyNameMapper::testParseKeyCombo_Simple() {
  // Single key, no modifiers
  QEXPECT_THAT(KeyNameMapper::parseKeyCombo("Return"),
               MatchesKeyCombo(Qt::Key_Return, Qt::NoModifier));
  QEXPECT_THAT(KeyNameMapper::parseKeyCombo("F5"), MatchesKeyCombo(Qt::Key_F5, Qt::NoModifier));
  QEXPECT_THAT(KeyNameMapper::parseKeyCombo("Escape"),
               MatchesKeyCombo(Qt::Key_Escape, Qt::NoModifier));
  QEXPECT_THAT(KeyNameMapper::parseKeyCombo("?"),
               MatchesKeyCombo(Qt::Key_Question, Qt::NoModifier));
  QEXPECT_THAT(KeyNameMapper::parseKeyCombo(" "), MatchesKeyCombo(Qt::Key_Space, Qt::NoModifier));
}

void TestKeyNameMapper::testParseKeyCombo_WithModifiers() {
  // ctrl+c
  QEXPECT_THAT(KeyNameMapper::parseKeyCombo("ctrl+c"),
               MatchesKeyCombo(Qt::Key_C, Qt::ControlModifier));

  // ctrl+shift+s
  QEXPECT_THAT(KeyNameMapper::parseKeyCombo("ctrl+shift+s"),
               MatchesKeyCombo(Qt::Key_S, Qt::ControlModifier | Qt::ShiftModifier));

  // alt+F4
  QEXPECT_THAT(KeyNameMapper::parseKeyCombo("alt+F4"),
               MatchesKeyCombo(Qt::Key_F4, Qt::AltModifier));
}

void TestKeyNameMapper::testParseKeyCombo_ChromeStyle() {
  // ctrl+shift+ArrowUp
  QEXPECT_THAT(KeyNameMapper::parseKeyCombo("ctrl+shift+ArrowUp"),
               MatchesKeyCombo(Qt::Key_Up, Qt::ControlModifier | Qt::ShiftModifier));

  // ctrl+ArrowDown
  QEXPECT_THAT(KeyNameMapper::parseKeyCombo("ctrl+ArrowDown"),
               MatchesKeyCombo(Qt::Key_Down, Qt::ControlModifier));
}

void TestKeyNameMapper::testParseKeyCombo_NamedPunctuation() {
  QEXPECT_THAT(KeyNameMapper::parseKeyCombo("meta+Plus"),
               MatchesKeyCombo(Qt::Key_Plus, Qt::MetaModifier));
  QEXPECT_THAT(KeyNameMapper::parseKeyCombo("cmd+Plus"),
               MatchesKeyCombo(Qt::Key_Plus, Qt::MetaModifier));
  QEXPECT_THAT(KeyNameMapper::parseKeyCombo("command+Plus"),
               MatchesKeyCombo(Qt::Key_Plus, Qt::MetaModifier));
  QEXPECT_THAT(KeyNameMapper::parseKeyCombo("ctrl+Minus"),
               MatchesKeyCombo(Qt::Key_Minus, Qt::ControlModifier));
  QEXPECT_THAT(KeyNameMapper::parseKeyCombo("QuestionMark"),
               MatchesKeyCombo(Qt::Key_Question, Qt::NoModifier));
}

void TestKeyNameMapper::testResolveExpectedMonadic() {
  auto valid = KeyNameMapper::resolveExpected("Return");
  QVERIFY(valid.has_value());
  QEXPECT_THAT(*valid, Eq(Qt::Key_Return));

  auto validChar = KeyNameMapper::resolveExpected("a");
  QVERIFY(validChar.has_value());
  QEXPECT_THAT(*validChar, Eq(Qt::Key_A));

  auto invalid = KeyNameMapper::resolveExpected("UnknownKeyXYZ");
  QVERIFY(!invalid.has_value());
  QVERIFY(!invalid.error().isEmpty());
}

void TestKeyNameMapper::testParseKeyComboExpectedMonadic() {
  auto valid = KeyNameMapper::parseKeyComboExpected("ctrl+shift+s");
  QVERIFY(valid.has_value());
  QEXPECT_THAT(*valid, MatchesKeyCombo(Qt::Key_S, Qt::ControlModifier | Qt::ShiftModifier));

  auto validQuestion = KeyNameMapper::parseKeyComboExpected("?");
  QVERIFY(validQuestion.has_value());
  QEXPECT_THAT(*validQuestion, MatchesKeyCombo(Qt::Key_Question, Qt::NoModifier));

  auto validSpace = KeyNameMapper::parseKeyComboExpected(" ");
  QVERIFY(validSpace.has_value());
  QEXPECT_THAT(*validSpace, MatchesKeyCombo(Qt::Key_Space, Qt::NoModifier));

  auto validCtrlQuestion = KeyNameMapper::parseKeyComboExpected("Ctrl+?");
  QVERIFY(validCtrlQuestion.has_value());
  QEXPECT_THAT(*validCtrlQuestion, MatchesKeyCombo(Qt::Key_Question, Qt::ControlModifier));

  auto invalidMod = KeyNameMapper::parseKeyComboExpected("invalidmod+s");
  QVERIFY(!invalidMod.has_value());

  auto invalidKey = KeyNameMapper::parseKeyComboExpected("ctrl+invalidkey");
  QVERIFY(!invalidKey.has_value());

  auto empty = KeyNameMapper::parseKeyComboExpected("");
  QVERIFY(!empty.has_value());
}

QTEST_GUILESS_MAIN(TestKeyNameMapper)
#include "test_key_name_mapper.moc"
