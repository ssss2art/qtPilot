// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include <QtTest>
#include <QSignalSpy>
#include <QPushButton>
#include <QLineEdit>
#include <QMainWindow>
#include <QVBoxLayout>
#include <QApplication>
#include "interaction/input_simulator.h"
#include "interaction/screenshot.h"
#include "interaction/hit_test.h"
#include "core/object_registry.h"

using namespace qtPilot;

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
    void testChildAt();

private:
    QMainWindow* m_window = nullptr;
    QPushButton* m_button = nullptr;
    QLineEdit* m_lineEdit = nullptr;
};

void TestUIInteraction::initTestCase()
{
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

void TestUIInteraction::cleanupTestCase()
{
    delete m_window;
    m_window = nullptr;
    m_button = nullptr;
    m_lineEdit = nullptr;
}

// === InputSimulator tests ===

void TestUIInteraction::testMouseClick()
{
    // Setup signal spy to detect click
    QSignalSpy spy(m_button, &QPushButton::clicked);
    QVERIFY(spy.isValid());

    // Click the button using InputSimulator
    InputSimulator::mouseClick(m_button);

    // Verify click was detected
    QCOMPARE(spy.count(), 1);
}

void TestUIInteraction::testMouseClickPosition()
{
    // Click at a specific position (10, 10) from top-left
    QSignalSpy spy(m_button, &QPushButton::clicked);
    QVERIFY(spy.isValid());

    InputSimulator::mouseClick(m_button, InputSimulator::MouseButton::Left, QPoint(10, 10));

    // Verify click was detected
    QCOMPARE(spy.count(), 1);
}

void TestUIInteraction::testSendText()
{
    // Clear the line edit first
    m_lineEdit->clear();
    QCOMPARE(m_lineEdit->text(), QString(""));

    // Type text into line edit
    InputSimulator::sendText(m_lineEdit, "Hello World");

    // Verify text was entered
    QCOMPARE(m_lineEdit->text(), QString("Hello World"));
}

void TestUIInteraction::testSendKeySequence()
{
    // Setup: put some text in line edit
    m_lineEdit->setText("Select Me");
    m_lineEdit->setFocus();
    QApplication::processEvents();

    // Send Ctrl+A to select all
    InputSimulator::sendKeySequence(m_lineEdit, "Ctrl+A");
    QApplication::processEvents();

    // Verify all text is selected
    QCOMPARE(m_lineEdit->selectedText(), QString("Select Me"));
}

// === Screenshot tests ===

void TestUIInteraction::testCaptureWidget()
{
    // Capture the button
    QByteArray base64 = Screenshot::captureWidget(m_button);

    // Verify we got some data
    QVERIFY(!base64.isEmpty());

    // Decode and verify it's a PNG (starts with PNG signature)
    QByteArray decoded = QByteArray::fromBase64(base64);
    QVERIFY(decoded.size() > 8);

    // PNG magic bytes: 89 50 4E 47 0D 0A 1A 0A
    QCOMPARE(decoded[0], static_cast<char>(0x89));
    QCOMPARE(decoded[1], 'P');
    QCOMPARE(decoded[2], 'N');
    QCOMPARE(decoded[3], 'G');
}

void TestUIInteraction::testCaptureRegion()
{
    // Capture a 50x50 region of the window
    QByteArray base64 = Screenshot::captureRegion(m_window, QRect(0, 0, 50, 50));

    // Verify we got some data
    QVERIFY(!base64.isEmpty());

    // Decode and verify it's a PNG
    QByteArray decoded = QByteArray::fromBase64(base64);
    QVERIFY(decoded.size() > 8);
    QCOMPARE(decoded[0], static_cast<char>(0x89));
    QCOMPARE(decoded[1], 'P');
    QCOMPARE(decoded[2], 'N');
    QCOMPARE(decoded[3], 'G');
}

// === HitTest tests ===

void TestUIInteraction::testWidgetGeometry()
{
    // Get geometry of button
    QJsonObject geo = HitTest::widgetGeometry(m_button);

    // Verify all required fields are present
    QVERIFY(geo.contains("local"));
    QVERIFY(geo.contains("global"));
    QVERIFY(geo.contains("devicePixelRatio"));

    // Verify local geometry
    QJsonObject local = geo["local"].toObject();
    QVERIFY(local.contains("x"));
    QVERIFY(local.contains("y"));
    QVERIFY(local.contains("width"));
    QVERIFY(local.contains("height"));
    QVERIFY(local["width"].toInt() > 0);
    QVERIFY(local["height"].toInt() > 0);

    // Verify global geometry
    QJsonObject global = geo["global"].toObject();
    QVERIFY(global.contains("x"));
    QVERIFY(global.contains("y"));
    QVERIFY(global.contains("width"));
    QVERIFY(global.contains("height"));

    // Verify devicePixelRatio is reasonable (usually 1.0, 1.5, 2.0)
    double dpr = geo["devicePixelRatio"].toDouble();
    QVERIFY(dpr >= 1.0);
    QVERIFY(dpr <= 4.0);
}

void TestUIInteraction::testChildAt()
{
    // Get the central widget
    QWidget* central = m_window->centralWidget();
    QVERIFY(central != nullptr);

    // Get layout to find button position
    QVBoxLayout* layout = qobject_cast<QVBoxLayout*>(central->layout());
    QVERIFY(layout != nullptr);

    // Map button center to central widget coordinates
    QPoint buttonCenter = m_button->rect().center();
    QPoint buttonCenterInCentral = m_button->mapTo(central, buttonCenter);

    // Find child at button position
    QWidget* found = HitTest::childAt(central, buttonCenterInCentral);

    // Should find the button (or at least not return nullptr)
    QVERIFY(found != nullptr);
    // The found widget should be the button or contain the button
    QVERIFY(found == m_button || found->findChild<QPushButton*>() == m_button);
}

QTEST_MAIN(TestUIInteraction)
#include "test_ui_interaction.moc"
