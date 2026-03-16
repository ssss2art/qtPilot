// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "interaction/key_name_mapper.h"

#include <QtTest>

using namespace qtPilot;

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

  // Combo parsing tests
  void testParseKeyCombo_Simple();
  void testParseKeyCombo_WithModifiers();
  void testParseKeyCombo_ChromeStyle();
};

// ========================================================================
// Key Resolution Tests
// ========================================================================

void TestKeyNameMapper::testResolveNavigationKeys() {
  // Return and Enter both map to Key_Return
  QCOMPARE(KeyNameMapper::resolve("Return"), Qt::Key_Return);
  QCOMPARE(KeyNameMapper::resolve("Enter"), Qt::Key_Return);

  // Tab
  QCOMPARE(KeyNameMapper::resolve("Tab"), Qt::Key_Tab);

  // Escape variants
  QCOMPARE(KeyNameMapper::resolve("Escape"), Qt::Key_Escape);
  QCOMPARE(KeyNameMapper::resolve("Esc"), Qt::Key_Escape);

  // Backspace variants
  QCOMPARE(KeyNameMapper::resolve("BackSpace"), Qt::Key_Backspace);
  QCOMPARE(KeyNameMapper::resolve("Backspace"), Qt::Key_Backspace);

  // Delete
  QCOMPARE(KeyNameMapper::resolve("Delete"), Qt::Key_Delete);

  // Space variants
  QCOMPARE(KeyNameMapper::resolve("Space"), Qt::Key_Space);
  QCOMPARE(KeyNameMapper::resolve("space"), Qt::Key_Space);
}

void TestKeyNameMapper::testResolveArrowKeys() {
  // xdotool-style
  QCOMPARE(KeyNameMapper::resolve("Up"), Qt::Key_Up);
  QCOMPARE(KeyNameMapper::resolve("Down"), Qt::Key_Down);
  QCOMPARE(KeyNameMapper::resolve("Left"), Qt::Key_Left);
  QCOMPARE(KeyNameMapper::resolve("Right"), Qt::Key_Right);

  // Chrome-style
  QCOMPARE(KeyNameMapper::resolve("ArrowUp"), Qt::Key_Up);
  QCOMPARE(KeyNameMapper::resolve("ArrowDown"), Qt::Key_Down);
  QCOMPARE(KeyNameMapper::resolve("ArrowLeft"), Qt::Key_Left);
  QCOMPARE(KeyNameMapper::resolve("ArrowRight"), Qt::Key_Right);
}

void TestKeyNameMapper::testResolveFunctionKeys() {
  QCOMPARE(KeyNameMapper::resolve("F1"), Qt::Key_F1);
  QCOMPARE(KeyNameMapper::resolve("F2"), Qt::Key_F2);
  QCOMPARE(KeyNameMapper::resolve("F3"), Qt::Key_F3);
  QCOMPARE(KeyNameMapper::resolve("F4"), Qt::Key_F4);
  QCOMPARE(KeyNameMapper::resolve("F5"), Qt::Key_F5);
  QCOMPARE(KeyNameMapper::resolve("F6"), Qt::Key_F6);
  QCOMPARE(KeyNameMapper::resolve("F7"), Qt::Key_F7);
  QCOMPARE(KeyNameMapper::resolve("F8"), Qt::Key_F8);
  QCOMPARE(KeyNameMapper::resolve("F9"), Qt::Key_F9);
  QCOMPARE(KeyNameMapper::resolve("F10"), Qt::Key_F10);
  QCOMPARE(KeyNameMapper::resolve("F11"), Qt::Key_F11);
  QCOMPARE(KeyNameMapper::resolve("F12"), Qt::Key_F12);
}

void TestKeyNameMapper::testResolveModifierKeys() {
  // Shift variants
  QCOMPARE(KeyNameMapper::resolve("Shift"), Qt::Key_Shift);
  QCOMPARE(KeyNameMapper::resolve("Shift_L"), Qt::Key_Shift);

  // Control variants
  QCOMPARE(KeyNameMapper::resolve("Control"), Qt::Key_Control);
  QCOMPARE(KeyNameMapper::resolve("Control_L"), Qt::Key_Control);

  // Alt variants
  QCOMPARE(KeyNameMapper::resolve("Alt"), Qt::Key_Alt);
  QCOMPARE(KeyNameMapper::resolve("Alt_L"), Qt::Key_Alt);

  // Super variants
  QCOMPARE(KeyNameMapper::resolve("Super"), Qt::Key_Super_L);
  QCOMPARE(KeyNameMapper::resolve("Super_L"), Qt::Key_Super_L);

  // Meta
  QCOMPARE(KeyNameMapper::resolve("Meta"), Qt::Key_Meta);
}

void TestKeyNameMapper::testResolveCaseInsensitive() {
  // Return - different cases
  QCOMPARE(KeyNameMapper::resolve("return"), Qt::Key_Return);
  QCOMPARE(KeyNameMapper::resolve("RETURN"), Qt::Key_Return);
  QCOMPARE(KeyNameMapper::resolve("Return"), Qt::Key_Return);

  // Escape - different cases
  QCOMPARE(KeyNameMapper::resolve("escape"), Qt::Key_Escape);
  QCOMPARE(KeyNameMapper::resolve("ESCAPE"), Qt::Key_Escape);
  QCOMPARE(KeyNameMapper::resolve("Escape"), Qt::Key_Escape);

  // Tab - different cases
  QCOMPARE(KeyNameMapper::resolve("tab"), Qt::Key_Tab);
  QCOMPARE(KeyNameMapper::resolve("TAB"), Qt::Key_Tab);
  QCOMPARE(KeyNameMapper::resolve("Tab"), Qt::Key_Tab);

  // Function keys are also case-insensitive
  QCOMPARE(KeyNameMapper::resolve("f1"), Qt::Key_F1);
  QCOMPARE(KeyNameMapper::resolve("F1"), Qt::Key_F1);
}

void TestKeyNameMapper::testResolveUnknown() {
  QCOMPARE(KeyNameMapper::resolve("NotAKey"), Qt::Key_unknown);
  QCOMPARE(KeyNameMapper::resolve("FooBar"), Qt::Key_unknown);
  QCOMPARE(KeyNameMapper::resolve(""), Qt::Key_unknown);
}

void TestKeyNameMapper::testResolveSingleChar() {
  // Lowercase letters -> Key_A .. Key_Z
  QCOMPARE(KeyNameMapper::resolve("a"), Qt::Key_A);
  QCOMPARE(KeyNameMapper::resolve("z"), Qt::Key_Z);

  // Uppercase letters
  QCOMPARE(KeyNameMapper::resolve("A"), Qt::Key_A);
  QCOMPARE(KeyNameMapper::resolve("Z"), Qt::Key_Z);

  // Digits
  QCOMPARE(KeyNameMapper::resolve("0"), Qt::Key_0);
  QCOMPARE(KeyNameMapper::resolve("1"), Qt::Key_1);
  QCOMPARE(KeyNameMapper::resolve("9"), Qt::Key_9);
}

// ========================================================================
// Combo Parsing Tests
// ========================================================================

void TestKeyNameMapper::testParseKeyCombo_Simple() {
  // Single key, no modifiers
  KeyCombo combo = KeyNameMapper::parseKeyCombo("Return");
  QCOMPARE(combo.key, Qt::Key_Return);
  QCOMPARE(combo.modifiers, Qt::KeyboardModifiers(Qt::NoModifier));

  KeyCombo combo2 = KeyNameMapper::parseKeyCombo("F5");
  QCOMPARE(combo2.key, Qt::Key_F5);
  QCOMPARE(combo2.modifiers, Qt::KeyboardModifiers(Qt::NoModifier));

  KeyCombo combo3 = KeyNameMapper::parseKeyCombo("Escape");
  QCOMPARE(combo3.key, Qt::Key_Escape);
  QCOMPARE(combo3.modifiers, Qt::KeyboardModifiers(Qt::NoModifier));
}

void TestKeyNameMapper::testParseKeyCombo_WithModifiers() {
  // ctrl+c
  KeyCombo combo1 = KeyNameMapper::parseKeyCombo("ctrl+c");
  QCOMPARE(combo1.key, Qt::Key_C);
  QCOMPARE(combo1.modifiers, Qt::KeyboardModifiers(Qt::ControlModifier));

  // ctrl+shift+s
  KeyCombo combo2 = KeyNameMapper::parseKeyCombo("ctrl+shift+s");
  QCOMPARE(combo2.key, Qt::Key_S);
  QCOMPARE(combo2.modifiers, Qt::KeyboardModifiers(Qt::ControlModifier | Qt::ShiftModifier));

  // alt+F4
  KeyCombo combo3 = KeyNameMapper::parseKeyCombo("alt+F4");
  QCOMPARE(combo3.key, Qt::Key_F4);
  QCOMPARE(combo3.modifiers, Qt::KeyboardModifiers(Qt::AltModifier));
}

void TestKeyNameMapper::testParseKeyCombo_ChromeStyle() {
  // ctrl+shift+ArrowUp
  KeyCombo combo = KeyNameMapper::parseKeyCombo("ctrl+shift+ArrowUp");
  QCOMPARE(combo.key, Qt::Key_Up);
  QCOMPARE(combo.modifiers, Qt::KeyboardModifiers(Qt::ControlModifier | Qt::ShiftModifier));

  // ctrl+ArrowDown
  KeyCombo combo2 = KeyNameMapper::parseKeyCombo("ctrl+ArrowDown");
  QCOMPARE(combo2.key, Qt::Key_Down);
  QCOMPARE(combo2.modifiers, Qt::KeyboardModifiers(Qt::ControlModifier));
}

QTEST_GUILESS_MAIN(TestKeyNameMapper)
#include "test_key_name_mapper.moc"
