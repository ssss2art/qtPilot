// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "common/qt_matchers.h"
#include "core/object_registry.h"
#include "interaction/hit_test.h"
#include "interaction/input_simulator.h"
#include "interaction/screenshot.h"

#include <QApplication>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QSignalSpy>
#include <QVBoxLayout>
#include <QtTest>

using namespace qtPilot;
using namespace qtPilot::test;

class TestUIInteraction : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase();
  void cleanupTestCase();

  // InputSimulator tests
  void testMouseClick();
  void testMouseClickPosition();
  void testSendText();
  void testSendKeySequence();

  // Screenshot tests
  void testCaptureWidget();
  void testCaptureRegion();

  // HitTest tests
  void testWidgetGeometry();
  void testWidgetGeometryExpectedMonadic();
  void testChildAt();

 private:
  QMainWindow* m_window = nullptr;
  QPushButton* m_button = nullptr;
  QLineEdit* m_lineEdit = nullptr;
};

void TestUIInteraction::initTestCase() {
  // Create test window with button and line edit
  m_window = new QMainWindow();
  m_window->setObjectName("testWindow");
  m_window->setGeometry(100, 100, 400, 300);

  QWidget* central = new QWidget(m_window);
  QVBoxLayout* layout = new QVBoxLayout(central);

  m_button = new QPushButton("Test Button", central);
  m_button->setObjectName("testButton");
  m_button->setMinimumSize(100, 30);
  layout->addWidget(m_button);

  m_lineEdit = new QLineEdit(central);
  m_lineEdit->setObjectName("testLineEdit");
  m_lineEdit->setMinimumSize(200, 30);
  layout->addWidget(m_lineEdit);

  m_window->setCentralWidget(central);
  m_window->show();

  // Process events to ensure widget is visible and rendered
  QApplication::processEvents();
}

void TestUIInteraction::cleanupTestCase() {
  delete m_window;
  m_window = nullptr;
  m_button = nullptr;
  m_lineEdit = nullptr;
}

// === InputSimulator tests ===

void TestUIInteraction::testMouseClick() {
  // Setup signal spy to detect click
  QSignalSpy spy(m_button, &QPushButton::clicked);
  QEXPECT_THAT(spy.isValid(), IsTrue());

  // Click the button using InputSimulator
  InputSimulator::mouseClick(m_button);

  // Verify click was detected
  QEXPECT_THAT(spy.count(), Eq(1));
}

void TestUIInteraction::testMouseClickPosition() {
  // Click at a specific position (10, 10) from top-left
  QSignalSpy spy(m_button, &QPushButton::clicked);
  QEXPECT_THAT(spy.isValid(), IsTrue());

  InputSimulator::mouseClick(m_button, InputSimulator::MouseButton::Left, QPoint(10, 10));

  // Verify click was detected
  QEXPECT_THAT(spy.count(), Eq(1));
}

void TestUIInteraction::testSendText() {
  // Clear the line edit first
  m_lineEdit->clear();
  QEXPECT_THAT(m_lineEdit->text(), QIsEmpty());

  // Type text into line edit
  InputSimulator::sendText(m_lineEdit, "Hello World");

  // Verify text was entered
  QEXPECT_THAT(m_lineEdit->text(), QStrEq("Hello World"));
}

void TestUIInteraction::testSendKeySequence() {
  // Setup: put some text in line edit
  m_lineEdit->setText("Select Me");
  m_lineEdit->setFocus();
  QApplication::processEvents();

  // Send Ctrl+A to select all
  InputSimulator::sendKeySequence(m_lineEdit, "Ctrl+A");
  QApplication::processEvents();

  // Verify all text is selected
  QEXPECT_THAT(m_lineEdit->selectedText(), QStrEq("Select Me"));
}

// === Screenshot tests ===

void TestUIInteraction::testCaptureWidget() {
  // Capture the button
  QByteArray base64 = Screenshot::captureWidget(m_button);

  // Verify we got some data
  QEXPECT_THAT(base64, QIsNotEmpty());

  // Decode and verify it's a PNG (starts with PNG signature)
  QByteArray decoded = QByteArray::fromBase64(base64);
  QEXPECT_THAT(decoded.size(), Gt(8));
  QEXPECT_THAT(decoded.startsWith("\x89PNG"), IsTrue());
}

void TestUIInteraction::testCaptureRegion() {
  // Capture a 50x50 region of the window
  QByteArray base64 = Screenshot::captureRegion(m_window, QRect(0, 0, 50, 50));

  // Verify we got some data
  QEXPECT_THAT(base64, QIsNotEmpty());

  // Decode and verify it's a PNG
  QByteArray decoded = QByteArray::fromBase64(base64);
  QEXPECT_THAT(decoded.size(), Gt(8));
  QEXPECT_THAT(decoded.startsWith("\x89PNG"), IsTrue());
}

// === HitTest tests ===

void TestUIInteraction::testWidgetGeometry() {
  // Get geometry of button
  QJsonObject geo = HitTest::widgetGeometry(m_button);

  // Verify all required fields are present
  QEXPECT_THAT(geo, AllOf(
      HasJsonField("local"),
      HasJsonField("global"),
      HasJsonField("devicePixelRatio")));

  // Verify local geometry
  QJsonObject local = geo["local"].toObject();
  QEXPECT_THAT(local, AllOf(
      HasJsonField("x"),
      HasJsonField("y"),
      HasJsonField("width", Gt(0)),
      HasJsonField("height", Gt(0))));

  // Verify global geometry
  QJsonObject global = geo["global"].toObject();
  QEXPECT_THAT(global, AllOf(
      HasJsonField("x"),
      HasJsonField("y"),
      HasJsonField("width"),
      HasJsonField("height")));

  // Verify devicePixelRatio is reasonable (usually 1.0, 1.5, 2.0)
  double dpr = geo["devicePixelRatio"].toDouble();
  QEXPECT_THAT(dpr, AllOf(Ge(1.0), Le(4.0)));
}

void TestUIInteraction::testWidgetGeometryExpectedMonadic() {
  auto res = HitTest::widgetGeometryExpected(m_button);
  QVERIFY(res.has_value());
  QEXPECT_THAT(res->contains(QStringLiteral("local")), IsTrue());
  QEXPECT_THAT(res->contains(QStringLiteral("global")), IsTrue());
  QEXPECT_THAT(res->contains(QStringLiteral("devicePixelRatio")), IsTrue());

  auto nullRes = HitTest::widgetGeometryExpected(nullptr);
  QVERIFY(!nullRes.has_value());
  QVERIFY(!nullRes.error().isEmpty());
}

void TestUIInteraction::testChildAt() {
  // Get the central widget
  QWidget* central = m_window->centralWidget();
  QEXPECT_THAT(central, NotNull());

  // Get layout to find button position
  QVBoxLayout* layout = qobject_cast<QVBoxLayout*>(central->layout());
  QEXPECT_THAT(layout, NotNull());

  // Map button center to central widget coordinates
  QPoint buttonCenter = m_button->rect().center();
  QPoint buttonCenterInCentral = m_button->mapTo(central, buttonCenter);

  // Find child at button position
  QWidget* found = HitTest::childAt(central, buttonCenterInCentral);

  // Should find the button (or at least not return nullptr)
  QEXPECT_THAT(found, NotNull());
  // The found widget should be the button or contain the button
  QEXPECT_THAT(found == m_button || found->findChild<QPushButton*>() == m_button, IsTrue());
}

QTEST_MAIN(TestUIInteraction)
#include "test_ui_interaction.moc"
