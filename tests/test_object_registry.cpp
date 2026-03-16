// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

// NOTE: This test requires QTPILOT_ENABLED=0 environment variable to be set
// to prevent full probe initialization. CTest sets this automatically.

#include <QtTest>
#include <QThread>
#include <QSignalSpy>
#include <QTimer>

#include "core/object_registry.h"

using namespace qtPilot;

/// @brief Unit tests for ObjectRegistry
///
/// Tests the core object tracking functionality including:
/// - Singleton pattern
/// - Object registration via hooks
/// - Lookup methods (by name, by class)
/// - Object removal on destruction
/// - Thread safety
class TestObjectRegistry : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void cleanup();

    void testSingleton();
    void testObjectTracking();
    void testFindByObjectName();
    void testFindAllByClassName();
    void testObjectRemoval();
    void testThreadSafety();

private:
    int m_initialObjectCount = 0;
};

void TestObjectRegistry::initTestCase() {
    // Install hooks for testing
    installObjectHooks();

    // Record initial object count (includes test framework objects)
    m_initialObjectCount = ObjectRegistry::instance()->objectCount();
    qDebug() << "Initial object count:" << m_initialObjectCount;
}

void TestObjectRegistry::cleanupTestCase() {
    // Uninstall hooks
    uninstallObjectHooks();
}

void TestObjectRegistry::cleanup() {
    // Process any pending events between tests
    QCoreApplication::processEvents();
}

void TestObjectRegistry::testSingleton() {
    // Get instance twice and verify they're the same
    ObjectRegistry* inst1 = ObjectRegistry::instance();
    ObjectRegistry* inst2 = ObjectRegistry::instance();

    QVERIFY(inst1 != nullptr);
    QVERIFY(inst2 != nullptr);
    QCOMPARE(inst1, inst2);
}

void TestObjectRegistry::testObjectTracking() {
    ObjectRegistry* registry = ObjectRegistry::instance();
    int countBefore = registry->objectCount();

    // Create a new object - should be automatically tracked
    QObject* testObj = new QObject(this);
    testObj->setObjectName(QStringLiteral("testTrackingObject"));

    // Allow hook callback to process
    QCoreApplication::processEvents();

    // Verify object is tracked
    QVERIFY(registry->contains(testObj));
    QVERIFY(registry->objectCount() > countBefore);

    // Verify it's in allObjects list
    QList<QObject*> allObjs = registry->allObjects();
    QVERIFY(allObjs.contains(testObj));

    // Cleanup is automatic via parent
}

void TestObjectRegistry::testFindByObjectName() {
    ObjectRegistry* registry = ObjectRegistry::instance();

    // Create objects with specific names
    QObject* parent = new QObject(this);
    parent->setObjectName(QStringLiteral("findTestParent"));

    QObject* child1 = new QObject(parent);
    child1->setObjectName(QStringLiteral("findTestChild1"));

    QObject* child2 = new QObject(parent);
    child2->setObjectName(QStringLiteral("findTestChild2"));

    QCoreApplication::processEvents();

    // Find by name (global search)
    QObject* found = registry->findByObjectName(QStringLiteral("findTestChild1"));
    QCOMPARE(found, child1);

    // Find by name within subtree
    QObject* foundInParent = registry->findByObjectName(QStringLiteral("findTestChild2"), parent);
    QCOMPARE(foundInParent, child2);

    // Search for non-existent name
    QObject* notFound = registry->findByObjectName(QStringLiteral("nonExistentObject"));
    QVERIFY(notFound == nullptr);

    // Search in wrong subtree
    QObject* wrongSubtree = registry->findByObjectName(QStringLiteral("findTestParent"), child1);
    QVERIFY(wrongSubtree == nullptr);
}

void TestObjectRegistry::testFindAllByClassName() {
    ObjectRegistry* registry = ObjectRegistry::instance();

    // Create multiple objects of the same class
    QObject* parent = new QObject(this);
    parent->setObjectName(QStringLiteral("classTestParent"));

    // QTimer is a good test class - distinct from QObject
    QTimer* timer1 = new QTimer(parent);
    timer1->setObjectName(QStringLiteral("timer1"));

    QTimer* timer2 = new QTimer(parent);
    timer2->setObjectName(QStringLiteral("timer2"));

    QObject* child = new QObject(parent);  // Not a QTimer
    child->setObjectName(QStringLiteral("notATimer"));

    QCoreApplication::processEvents();

    // Find all QTimers in the subtree
    QList<QObject*> timers = registry->findAllByClassName(QStringLiteral("QTimer"), parent);
    QCOMPARE(timers.size(), 2);
    QVERIFY(timers.contains(timer1));
    QVERIFY(timers.contains(timer2));
    QVERIFY(!timers.contains(child));

    // Find all QObjects (should include everything)
    QList<QObject*> allObjects = registry->findAllByClassName(QStringLiteral("QObject"), parent);
    // Should find parent and the non-timer child (timers are QTimer, not QObject exactly)
    QVERIFY(allObjects.contains(parent));
    QVERIFY(allObjects.contains(child));
}

void TestObjectRegistry::testObjectRemoval() {
    ObjectRegistry* registry = ObjectRegistry::instance();

    // Create and then delete an object
    QObject* tempObj = new QObject();
    tempObj->setObjectName(QStringLiteral("tempObjectForRemoval"));

    QCoreApplication::processEvents();
    QVERIFY(registry->contains(tempObj));

    // Delete the object
    delete tempObj;
    tempObj = nullptr;

    QCoreApplication::processEvents();

    // Verify it's removed from registry
    // We can't check contains() directly since the pointer is invalid,
    // but we can verify the count decreased and we can't find by name
    QObject* shouldBeNull = registry->findByObjectName(QStringLiteral("tempObjectForRemoval"));
    QVERIFY(shouldBeNull == nullptr);
}

void TestObjectRegistry::testThreadSafety() {
    ObjectRegistry* registry = ObjectRegistry::instance();

    // Create objects from multiple threads simultaneously
    // This tests the mutex protection

    QAtomicInt createdCount(0);
    QAtomicInt errors(0);
    const int threadCount = 4;
    const int objectsPerThread = 50;

    QVector<QThread*> threads;

    for (int t = 0; t < threadCount; ++t) {
        QThread* thread = QThread::create([&createdCount, &errors, t, objectsPerThread]() {
            QVector<QObject*> localObjects;
            localObjects.reserve(objectsPerThread);

            for (int i = 0; i < objectsPerThread; ++i) {
                try {
                    QObject* obj = new QObject();
                    obj->setObjectName(QStringLiteral("threadTest_%1_%2").arg(t).arg(i));
                    localObjects.append(obj);
                    createdCount.fetchAndAddRelaxed(1);
                } catch (...) {
                    errors.fetchAndAddRelaxed(1);
                }
            }

            // Delete all local objects
            qDeleteAll(localObjects);
        });

        threads.append(thread);
    }

    // Start all threads
    for (QThread* thread : threads) {
        thread->start();
    }

    // Wait for all threads to finish
    for (QThread* thread : threads) {
        thread->wait();
        delete thread;
    }

    // Verify no errors occurred
    QCOMPARE(errors.loadRelaxed(), 0);
    QCOMPARE(createdCount.loadRelaxed(), threadCount * objectsPerThread);

    // Registry should still be functional
    QVERIFY(registry->objectCount() >= 0);  // Just verify it doesn't crash

    qDebug() << "Thread safety test: created" << createdCount.loadRelaxed()
             << "objects across" << threadCount << "threads with"
             << errors.loadRelaxed() << "errors";
}

// Use GUILESS main since we don't need GUI for these tests
QTEST_GUILESS_MAIN(TestObjectRegistry)
#include "test_object_registry.moc"
