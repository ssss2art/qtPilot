// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

// NOTE: This test requires QTPILOT_ENABLED=0 environment variable to be set
// to prevent full probe initialization. CTest sets this automatically.

#include "core/object_registry.h"
#include "introspection/object_id.h"

#include <QPushButton>
#include <QWidget>
#include <QtTest>

#include "common/qt_matchers.h"

using namespace qtPilot;
using namespace qtPilot::test;

/// @brief Unit tests for Object ID refresh when objectName changes post-construction.
///
/// The AddQObject hook fires during QObject construction, before setObjectName()
/// is called. These tests verify that cached IDs are refreshed when objectName
/// is set after construction, and that stale IDs still resolve via the alias map.
class TestObjectIdRefresh : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase();
  void cleanupTestCase();
  void cleanup();

  void testIdRefreshOnObjectNameSet();
  void testIdRefreshCascadesToChildren();
  void testOldIdStillResolvable();
  void testMultipleNameChanges();
};

void TestObjectIdRefresh::initTestCase() {
  installObjectHooks();
  qDebug() << "Initial object count:" << ObjectRegistry::instance()->objectCount();
}

void TestObjectIdRefresh::cleanupTestCase() {
  uninstallObjectHooks();
}

void TestObjectIdRefresh::cleanup() {
  QCoreApplication::processEvents();
}

void TestObjectIdRefresh::testIdRefreshOnObjectNameSet() {
  auto* registry = ObjectRegistry::instance();

  // Create widget — hook fires, ID is cached with empty objectName
  auto* widget = new QWidget();
  QCoreApplication::processEvents();

  QString staleId = registry->objectId(widget);
  qDebug() << "Stale ID:" << staleId;
  // Stale ID should NOT contain "myWidget" since objectName wasn't set yet
  QEXPECT_THAT(staleId, Not(QStrContains("myWidget")));

  // Now set objectName — triggers objectNameChanged signal
  widget->setObjectName(QStringLiteral("myWidget"));
  QCoreApplication::processEvents();

  // After processEvents, the queued refresh should have fired
  QString refreshedId = registry->objectId(widget);
  qDebug() << "Refreshed ID:" << refreshedId;
  QEXPECT_THAT(refreshedId, AllOf(QStrContains("myWidget"), Ne(staleId)));

  delete widget;
  QCoreApplication::processEvents();
}

void TestObjectIdRefresh::testIdRefreshCascadesToChildren() {
  auto* registry = ObjectRegistry::instance();

  // Create parent and child without names
  auto* parent = new QWidget();
  auto* child = new QPushButton(parent);
  QCoreApplication::processEvents();

  QString childStaleId = registry->objectId(child);
  qDebug() << "Child stale ID:" << childStaleId;

  // Set parent's objectName — should cascade refresh to child
  parent->setObjectName(QStringLiteral("parentWidget"));
  QCoreApplication::processEvents();

  QString childRefreshedId = registry->objectId(child);
  qDebug() << "Child refreshed ID:" << childRefreshedId;
  // Child's path should now include parent's objectName
  QEXPECT_THAT(childRefreshedId, QStrContains("parentWidget"));

  // Also set child's name
  child->setObjectName(QStringLiteral("childButton"));
  QCoreApplication::processEvents();

  QString childFinalId = registry->objectId(child);
  qDebug() << "Child final ID:" << childFinalId;
  QEXPECT_THAT(childFinalId, AllOf(QStrContains("parentWidget"), QStrContains("childButton")));

  delete parent;
  QCoreApplication::processEvents();
}

void TestObjectIdRefresh::testOldIdStillResolvable() {
  auto* registry = ObjectRegistry::instance();

  auto* widget = new QWidget();
  QCoreApplication::processEvents();

  QString staleId = registry->objectId(widget);
  qDebug() << "Stale ID for alias test:" << staleId;

  // Set objectName to trigger refresh
  widget->setObjectName(QStringLiteral("aliasTestWidget"));
  QCoreApplication::processEvents();

  QString newId = registry->objectId(widget);
  QEXPECT_THAT(newId, Ne(staleId));
  qDebug() << "New ID:" << newId;

  // The stale ID should still resolve to the same object via alias map
  QObject* found = registry->findById(staleId);
  QEXPECT_THAT(found, Eq(widget));

  // The new ID should also resolve
  QObject* foundNew = registry->findById(newId);
  QEXPECT_THAT(foundNew, Eq(widget));

  delete widget;
  QCoreApplication::processEvents();
}

void TestObjectIdRefresh::testMultipleNameChanges() {
  auto* registry = ObjectRegistry::instance();

  auto* widget = new QWidget();
  QCoreApplication::processEvents();

  QString id0 = registry->objectId(widget);

  // First name change
  widget->setObjectName(QStringLiteral("firstName"));
  QCoreApplication::processEvents();
  QString id1 = registry->objectId(widget);
  QEXPECT_THAT(id1, QStrContains("firstName"));

  // Second name change
  widget->setObjectName(QStringLiteral("secondName"));
  QCoreApplication::processEvents();
  QString id2 = registry->objectId(widget);
  QEXPECT_THAT(id2, AllOf(QStrContains("secondName"), Not(QStrContains("firstName"))));

  // All previous IDs should still resolve
  QEXPECT_THAT(registry->findById(id0), Eq(widget));
  QEXPECT_THAT(registry->findById(id1), Eq(widget));
  QEXPECT_THAT(registry->findById(id2), Eq(widget));

  delete widget;
  QCoreApplication::processEvents();
}

QTEST_MAIN(TestObjectIdRefresh)
#include "test_object_id_refresh.moc"
