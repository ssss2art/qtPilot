// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

// NOTE: This test requires QTPILOT_ENABLED=0 environment variable to be set
// to prevent full probe initialization. CTest sets this automatically.

#include "common/qt_matchers.h"
#include "core/object_registry.h"

#include <QSignalSpy>
#include <QThread>
#include <QTimer>
#include <QtTest>

using namespace qtPilot;
using namespace qtPilot::test;

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
  void init();
  void cleanup();

  void testSingleton();
  void testObjectTracking();
  void testFindByObjectName();
  void testFindAllByClassName();
  void testObjectRemoval();
  void testDisconnectedRegistrationIsLazy();
  void testConnectedRegistrationPublishesObjectAdded();
  void testDestroyedObjectSuppressesQueuedObjectAdded();
  void testObjectStormRapidCreation();
  void testScanExistingObjectsIsLazy();
  void testAllObjectsWithRoot();
  void testThreadSafety();
  void testFindByIdExpectedMonadic();
  void testFindByObjectNameExpectedMonadic();

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

void TestObjectRegistry::init() {
  ObjectRegistry::instance()->setClientConnected(false);
  ObjectRegistry::instance()->setLifecycleNotificationsEnabled(false);
}

void TestObjectRegistry::cleanup() {
  // Process any pending events between tests
  QCoreApplication::processEvents();
}

void TestObjectRegistry::testSingleton() {
  // Get instance twice and verify they're the same
  ObjectRegistry* inst1 = ObjectRegistry::instance();
  ObjectRegistry* inst2 = ObjectRegistry::instance();

  QEXPECT_THAT(inst1, NotNull());
  QEXPECT_THAT(inst2, NotNull());
  QEXPECT_THAT(inst1, Eq(inst2));
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
  QEXPECT_THAT(registry->contains(testObj), IsTrue());
  QEXPECT_THAT(registry->objectCount(), Gt(countBefore));

  // Verify it's in allObjects list
  QList<QObject*> allObjs = registry->allObjects();
  QEXPECT_THAT(allObjs, Contains(testObj));

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
  QEXPECT_THAT(found, Eq(child1));

  // Find by name within subtree
  QObject* foundInParent = registry->findByObjectName(QStringLiteral("findTestChild2"), parent);
  QEXPECT_THAT(foundInParent, Eq(child2));

  // Search for non-existent name
  QObject* notFound = registry->findByObjectName(QStringLiteral("nonExistentObject"));
  QEXPECT_THAT(notFound, IsNull());

  // Search in wrong subtree
  QObject* wrongSubtree = registry->findByObjectName(QStringLiteral("findTestParent"), child1);
  QEXPECT_THAT(wrongSubtree, IsNull());
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
  QEXPECT_THAT(timers, SizeIs(2));
  QEXPECT_THAT(timers, Contains(timer1));
  QEXPECT_THAT(timers, Contains(timer2));
  QEXPECT_THAT(timers, Not(Contains(child)));

  // Search is subclass-aware: querying the base class "QObject" matches every
  // tracked object, including the QTimer instances (QTimer derives QObject).
  QList<QObject*> allObjects = registry->findAllByClassName(QStringLiteral("QObject"), parent);
  QEXPECT_THAT(allObjects,
               AllOf(Contains(parent), Contains(child), Contains(timer1), Contains(timer2)));
}

void TestObjectRegistry::testObjectRemoval() {
  ObjectRegistry* registry = ObjectRegistry::instance();

  // Create and then delete an object
  QObject* tempObj = new QObject();
  tempObj->setObjectName(QStringLiteral("tempObjectForRemoval"));

  QCoreApplication::processEvents();
  QEXPECT_THAT(registry->contains(tempObj), IsTrue());

  // Delete the object
  delete tempObj;
  tempObj = nullptr;

  QCoreApplication::processEvents();

  // Verify it's removed from registry
  // We can't check contains() directly since the pointer is invalid,
  // but we can verify the count decreased and we can't find by name
  QObject* shouldBeNull = registry->findByObjectName(QStringLiteral("tempObjectForRemoval"));
  QEXPECT_THAT(shouldBeNull, IsNull());
}

void TestObjectRegistry::testDisconnectedRegistrationIsLazy() {
  ObjectRegistry* registry = ObjectRegistry::instance();
  QSignalSpy addedSpy(registry, &ObjectRegistry::objectAdded);

  auto* obj = new QObject(this);
  obj->setObjectName(QStringLiteral("lazyRegistrationObject"));
  QCoreApplication::processEvents();

  QEXPECT_THAT(registry->contains(obj), IsTrue());
  QEXPECT_THAT(addedSpy.count(), Eq(0));

  const QString id = registry->objectId(obj);
  QEXPECT_THAT(id, QIsNotEmpty());
  QEXPECT_THAT(registry->findById(id), Eq(obj));
}

void TestObjectRegistry::testConnectedRegistrationPublishesObjectAdded() {
  ObjectRegistry* registry = ObjectRegistry::instance();
  registry->setClientConnected(true);
  registry->setLifecycleNotificationsEnabled(true);
  QSignalSpy addedSpy(registry, &ObjectRegistry::objectAdded);

  auto* obj = new QObject(this);
  obj->setObjectName(QStringLiteral("connectedRegistrationObject"));

  bool found = false;
  QTRY_VERIFY_WITH_TIMEOUT(([&]() {
                             for (const auto& emission : addedSpy) {
                               if (qvariant_cast<QObject*>(emission.at(0)) == obj) {
                                 found = true;
                                 return true;
                               }
                             }
                             return false;
                           })(),
                           1000);
  QEXPECT_THAT(found, IsTrue());
}

void TestObjectRegistry::testDestroyedObjectSuppressesQueuedObjectAdded() {
  ObjectRegistry* registry = ObjectRegistry::instance();
  registry->setClientConnected(true);
  registry->setLifecycleNotificationsEnabled(true);
  QSignalSpy addedSpy(registry, &ObjectRegistry::objectAdded);

  auto* obj = new QObject();
  QObject* destroyedAddress = obj;
  delete obj;
  QCoreApplication::processEvents();

  for (const auto& emission : addedSpy) {
    QEXPECT_THAT(qvariant_cast<QObject*>(emission.at(0)), Ne(destroyedAddress));
  }
}

void TestObjectRegistry::testObjectStormRapidCreation() {
  ObjectRegistry* registry = ObjectRegistry::instance();
  registry->setClientConnected(true);
  registry->setLifecycleNotificationsEnabled(false);

  QSignalSpy addedSpy(registry, &ObjectRegistry::objectAdded);
  QSignalSpy removedSpy(registry, &ObjectRegistry::objectRemoved);

  // Rapidly allocate and delete thousands of objects (object storm simulation)
  constexpr int kStormCount = 3000;
  QList<QObject*> batch;
  batch.reserve(kStormCount);
  for (int i = 0; i < kStormCount; ++i) {
    batch.append(new QObject(this));
  }

  // All objects are tracked immediately
  QEXPECT_THAT(registry->objectCount(), Ge(kStormCount));

  // Event loop processing should NOT have queued thousands of notifications
  QCoreApplication::processEvents();
  QEXPECT_THAT(addedSpy.size(), Eq(0));
  QEXPECT_THAT(removedSpy.size(), Eq(0));

  // Now enable lifecycle notifications and verify they resume
  registry->setLifecycleNotificationsEnabled(true);
  auto* singleObj = new QObject(this);
  QTRY_VERIFY_WITH_TIMEOUT(addedSpy.size() == 1, 1000);
  QEXPECT_THAT(qvariant_cast<QObject*>(addedSpy.at(0).at(0)), Eq(singleObj));

  // Deleting an object emits objectRemoved when lifecycle is enabled
  delete singleObj;
  QTRY_VERIFY_WITH_TIMEOUT(removedSpy.size() == 1, 1000);

  qDeleteAll(batch);
  batch.clear();
}

void TestObjectRegistry::testScanExistingObjectsIsLazy() {
  ObjectRegistry* registry = ObjectRegistry::instance();
  uninstallObjectHooks();

  std::unique_ptr<QObject> root(new QObject());
  root->setObjectName(QStringLiteral("scannedRoot"));
  auto* child1 = new QObject(root.get());
  child1->setObjectName(QStringLiteral("scannedChild1"));
  auto* child2 = new QObject(root.get());
  child2->setObjectName(QStringLiteral("scannedChild2"));

  registry->scanExistingObjects(root.get());

  // Tracked immediately via pointer set
  QEXPECT_THAT(registry->contains(root.get()), IsTrue());
  QEXPECT_THAT(registry->contains(child1), IsTrue());
  QEXPECT_THAT(registry->contains(child2), IsTrue());

  // Introspection lazily generates and caches IDs on demand
  QString rootId = registry->objectId(root.get());
  QEXPECT_THAT(rootId, QIsNotEmpty());
  QEXPECT_THAT(registry->findById(rootId), Eq(root.get()));

  installObjectHooks();
}

void TestObjectRegistry::testAllObjectsWithRoot() {
  ObjectRegistry* registry = ObjectRegistry::instance();

  auto* parentA = new QObject(this);
  parentA->setObjectName(QStringLiteral("parentA"));
  auto* childA = new QObject(parentA);
  childA->setObjectName(QStringLiteral("childA"));

  auto* parentB = new QObject(this);
  parentB->setObjectName(QStringLiteral("parentB"));
  auto* childB = new QObject(parentB);
  childB->setObjectName(QStringLiteral("childB"));

  QCoreApplication::processEvents();

  QList<QObject*> subtreeA = registry->allObjects(parentA);
  QEXPECT_THAT(subtreeA, AllOf(Contains(parentA), Contains(childA)));
  QEXPECT_THAT(subtreeA, Not(Contains(parentB)));
  QEXPECT_THAT(subtreeA, Not(Contains(childB)));

  QList<QObject*> subtreeB = registry->allObjects(parentB);
  QEXPECT_THAT(subtreeB, AllOf(Contains(parentB), Contains(childB)));
  QEXPECT_THAT(subtreeB, Not(Contains(parentA)));
  QEXPECT_THAT(subtreeB, Not(Contains(childA)));
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
    QThread* thread = QThread::create([&createdCount, &errors, t]() {
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
  QEXPECT_THAT(errors.loadRelaxed(), Eq(0));
  QEXPECT_THAT(createdCount.loadRelaxed(), Eq(threadCount * objectsPerThread));

  // Registry should still be functional
  QEXPECT_THAT(registry->objectCount(), Ge(0));  // Just verify it doesn't crash

  qDebug() << "Thread safety test: created" << createdCount.loadRelaxed() << "objects across"
           << threadCount << "threads with" << errors.loadRelaxed() << "errors";
}

void TestObjectRegistry::testFindByIdExpectedMonadic() {
  auto* registry = ObjectRegistry::instance();
  registry->setClientConnected(true);

  QObject parent;
  parent.setObjectName(QStringLiteral("regExpectedParent"));
  QObject* child = new QObject(&parent);
  child->setObjectName(QStringLiteral("regExpectedChild"));

  QString id = registry->objectId(child);

  // Success
  auto successRes = registry->findByIdExpected(id);
  QVERIFY(successRes.has_value());
  QCOMPARE(*successRes, child);

  // Failure: empty ID
  auto emptyRes = registry->findByIdExpected(QString());
  QVERIFY(!emptyRes.has_value());
  QVERIFY(!emptyRes.error().isEmpty());

  // Failure: nonexistent ID
  auto nonExistentRes = registry->findByIdExpected(QStringLiteral("nonexistent/id"));
  QVERIFY(!nonExistentRes.has_value());
  QVERIFY(!nonExistentRes.error().isEmpty());
}

void TestObjectRegistry::testFindByObjectNameExpectedMonadic() {
  auto* registry = ObjectRegistry::instance();

  QObject target;
  target.setObjectName(QStringLiteral("uniqueMonadicName"));

  // Success
  auto successRes = registry->findByObjectNameExpected(QStringLiteral("uniqueMonadicName"));
  QVERIFY(successRes.has_value());
  QCOMPARE(*successRes, &target);

  // Failure: empty name
  auto emptyRes = registry->findByObjectNameExpected(QString());
  QVERIFY(!emptyRes.has_value());

  // Failure: unknown name
  auto unknownRes = registry->findByObjectNameExpected(QStringLiteral("completelyUnknownName404"));
  QVERIFY(!unknownRes.has_value());
}

// Use GUILESS main since we don't need GUI for these tests
QTEST_GUILESS_MAIN(TestObjectRegistry)
#include "test_object_registry.moc"
