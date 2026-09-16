// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

// NOTE: This test requires QTPILOT_ENABLED=0 environment variable to be set
// to prevent full probe initialization. CTest sets this automatically.

#include "core/object_registry.h"
#include "introspection/object_id.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QPushButton>
#include <QWindow>
#include <QWidget>
#include <QtTest>

#include "common/qt_matchers.h"

using namespace qtPilot;
using namespace qtPilot::test;

/// @brief Unit tests for Object ID generation and tree serialization.
///
/// Tests the hierarchical ID system including:
/// - ID generation with objectName priority
/// - Text property fallback
/// - ClassName with sibling disambiguation
/// - findById lookup
/// - Tree serialization to JSON
class TestObjectId : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase();
  void cleanupTestCase();
  void cleanup();

  void testIdWithObjectName();
  void testIdWithTextProperty();
  void testIdWithClassName();
  void testIdSiblingDisambiguation();
  void testFindById();
  void testFindByIdGlobal();
  void testRegistryFindById();
  void testVisibleTopLevelWindowIncludedAndHiddenExcluded();
  void testTopLevelWindowIdsAreUniqueAndRoundTrip();
  void testSerializeObjectInfo();
  void testSerializeTree();
  void testSerializeTreeDepthLimit();

 private:
  QWidget* m_testWindow = nullptr;
};

void TestObjectId::initTestCase() {
  // Install hooks for testing
  installObjectHooks();

  qDebug() << "Initial object count:" << ObjectRegistry::instance()->objectCount();
}

void TestObjectId::cleanupTestCase() {
  // Clean up test window
  delete m_testWindow;
  m_testWindow = nullptr;

  // Uninstall hooks
  uninstallObjectHooks();
}

void TestObjectId::cleanup() {
  // Process any pending events between tests
  QCoreApplication::processEvents();
}

void TestObjectId::testIdWithObjectName() {
  // Create a widget with objectName
  QWidget parent;
  parent.setObjectName(QStringLiteral("mainWindow"));

  QWidget* child = new QWidget(&parent);
  child->setObjectName(QStringLiteral("centralWidget"));

  QPushButton* button = new QPushButton(QStringLiteral("Click me"), child);
  button->setObjectName(QStringLiteral("submitBtn"));

  QCoreApplication::processEvents();

  // ID should use objectNames throughout
  QString id = generateObjectId(button);
  QEXPECT_THAT(id, AllOf(QStrEndsWith("submitBtn"),
                         QStrContains("centralWidget"),
                         QStrContains("mainWindow")));

  // Verify format is "parent/child/grandchild"
  QStringList segments = id.split(QLatin1Char('/'));
  QEXPECT_THAT(segments, SizeIs(Ge(3)));
  QEXPECT_THAT(segments.last(), QStrEq("submitBtn"));
}

void TestObjectId::testIdWithTextProperty() {
  // Create a button without objectName but with text
  QWidget parent;
  parent.setObjectName(QStringLiteral("textTestParent"));

  QPushButton* button = new QPushButton(QStringLiteral("OK"), &parent);
  // Don't set objectName - should fall back to text property

  QCoreApplication::processEvents();

  QString id = generateObjectId(button);
  // Should contain "text_OK"
  QEXPECT_THAT(id, QStrContains("text_OK"));

  // Test text sanitization
  QPushButton* longTextButton = new QPushButton(
      QStringLiteral("This is a very long button label that exceeds twenty characters"), &parent);
  id = generateObjectId(longTextButton);
  // Should be truncated and sanitized
  QEXPECT_THAT(id, QStrContains("text_"));
  // Segment should be <= 25 chars ("text_" + 20 chars max)
  QStringList segments = id.split(QLatin1Char('/'));
  QString lastSegment = segments.last();
  QEXPECT_THAT(lastSegment, QStrStartsWith("text_"));
  QEXPECT_THAT(lastSegment.length(), Le(25));
}

void TestObjectId::testIdWithClassName() {
  // Create objects without objectName or text
  QWidget parent;
  parent.setObjectName(QStringLiteral("classTestParent"));

  QObject* child = new QObject(&parent);
  // No objectName, no text property - should use class name

  QCoreApplication::processEvents();

  QString id = generateObjectId(child);
  QEXPECT_THAT(id, QStrContains("QObject"));
}

void TestObjectId::testIdSiblingDisambiguation() {
  // Create multiple unnamed objects of the same class
  QWidget parent;
  parent.setObjectName(QStringLiteral("siblingTestParent"));

  QPushButton* btn1 = new QPushButton(&parent);
  // No objectName, no text - should get disambiguation suffix

  QPushButton* btn2 = new QPushButton(&parent);
  // Same class, also unnamed

  QPushButton* btn3 = new QPushButton(&parent);
  // Third unnamed button

  QCoreApplication::processEvents();

  QString id1 = generateObjectId(btn1);
  QString id2 = generateObjectId(btn2);
  QString id3 = generateObjectId(btn3);

  // All IDs should be different
  QEXPECT_THAT(id1, Ne(id2));
  QEXPECT_THAT(id2, Ne(id3));
  QEXPECT_THAT(id1, Ne(id3));

  // Should contain disambiguation suffixes
  QStringList segments1 = id1.split(QLatin1Char('/'));
  QStringList segments2 = id2.split(QLatin1Char('/'));
  QStringList segments3 = id3.split(QLatin1Char('/'));

  // Each should end with QPushButton#N
  QEXPECT_THAT(segments1.last(), QStrStartsWith("QPushButton"));
  QEXPECT_THAT(segments2.last(), QStrStartsWith("QPushButton"));
  QEXPECT_THAT(segments3.last(), QStrStartsWith("QPushButton"));

  // Check for #N suffixes (order may vary)
  QStringList endings;
  endings << segments1.last() << segments2.last() << segments3.last();
  QEXPECT_THAT(endings, UnorderedElementsAre(
      QStrEq("QPushButton#1"),
      QStrEq("QPushButton#2"),
      QStrEq("QPushButton#3")));
}

void TestObjectId::testFindById() {
  // Create a hierarchy
  QWidget parent;
  parent.setObjectName(QStringLiteral("findByIdParent"));

  QWidget* child = new QWidget(&parent);
  child->setObjectName(QStringLiteral("findByIdChild"));

  QPushButton* button = new QPushButton(QStringLiteral("Find me"), child);
  button->setObjectName(QStringLiteral("targetButton"));

  QCoreApplication::processEvents();

  // Get the ID
  QString id = generateObjectId(button);

  // Find by ID starting from parent
  QObject* found = findByObjectId(id, &parent);
  QEXPECT_THAT(found, Eq(button));

  // Search for non-existent ID
  QObject* notFound = findByObjectId(QStringLiteral("nonexistent/path/here"), &parent);
  QEXPECT_THAT(notFound, IsNull());
}

void TestObjectId::testFindByIdGlobal() {
  // Create an object parented to the application (makes it a top-level object)
  QObject* topLevel = new QObject(QCoreApplication::instance());
  topLevel->setObjectName(QStringLiteral("globalTestRoot"));

  QObject* child = new QObject(topLevel);
  child->setObjectName(QStringLiteral("globalTestChild"));

  QCoreApplication::processEvents();

  // Generate the full hierarchical ID
  QString childId = generateObjectId(child);
  qDebug() << "Global child ID:" << childId;

  // The ID should contain the application class name as the first segment
  QStringList segments = childId.split(QLatin1Char('/'));
  QEXPECT_THAT(segments, SizeIs(Ge(3)));  // App/globalTestRoot/globalTestChild

  // Critical test: findByObjectId with NO root must resolve the full path
  QObject* found = findByObjectId(childId);
  QEXPECT_THAT(found, NotNull());
  QEXPECT_THAT(found, Eq(child));

  // Also verify finding the top-level object itself
  QString topLevelId = generateObjectId(topLevel);
  QObject* foundTopLevel = findByObjectId(topLevelId);
  QEXPECT_THAT(foundTopLevel, NotNull());
  QEXPECT_THAT(foundTopLevel, Eq(topLevel));

  // Clean up
  delete topLevel;
}

void TestObjectId::testRegistryFindById() {
  ObjectRegistry* registry = ObjectRegistry::instance();

  // NOTE: The AddQObject hook fires at the START of QObject construction,
  // before parent-child relationships are set and before the derived class
  // constructor runs. This means IDs computed at hook time reflect the
  // incomplete state of the object (no parent, no text, just QObject).
  //
  // This test verifies the registry's ID caching and lookup work correctly,
  // even if the cached IDs are minimal.

  // Create objects - IDs will be generated at construction time
  QWidget* parent = new QWidget();
  QWidget* child = new QWidget(parent);
  QPushButton* button = new QPushButton(QStringLiteral("Registry"), child);

  // Set names after creation (won't affect cached IDs)
  parent->setObjectName(QStringLiteral("registryTestParent"));
  child->setObjectName(QStringLiteral("registryTestChild"));
  button->setObjectName(QStringLiteral("registryButton"));

  QCoreApplication::processEvents();

  // Get cached ID via registry
  QString id = registry->objectId(button);
  QEXPECT_THAT(id, QIsNotEmpty());
  qDebug() << "Button ID from registry:" << id;

  // The cached ID was generated at construction time, so it won't
  // include the objectName. Just verify it's non-empty and lookup works.

  // Find via registry using the cached ID
  QObject* found = registry->findById(id);
  QEXPECT_THAT(found, Eq(button));

  // Verify registry contains the object
  QEXPECT_THAT(registry->contains(button), IsTrue());

  // Clean up
  delete parent;
  QCoreApplication::processEvents();

  // After deletion, findById should return nullptr (QPointer detected deletion)
  QObject* deleted = registry->findById(id);
  QEXPECT_THAT(deleted, IsNull());
}

void TestObjectId::testVisibleTopLevelWindowIncludedAndHiddenExcluded() {
  QWindow visibleWindow;
  visibleWindow.setObjectName(QStringLiteral("visibleTopLevelWindow"));
  visibleWindow.setGeometry(10, 10, 100, 80);
  visibleWindow.show();

  QWindow hiddenWindow;
  hiddenWindow.setObjectName(QStringLiteral("hiddenTopLevelWindow"));
  QCoreApplication::processEvents();

  const QJsonArray roots = serializeObjectTree(nullptr, 0)[QStringLiteral("children")].toArray();
  QEXPECT_THAT(roots, JsonArrayContains(HasJsonField("objectName", "visibleTopLevelWindow")));
  QEXPECT_THAT(roots, Not(JsonArrayContains(HasJsonField("objectName", "hiddenTopLevelWindow"))));
}

void TestObjectId::testTopLevelWindowIdsAreUniqueAndRoundTrip() {
  QWindow first;
  QWindow second;
  first.setGeometry(10, 10, 100, 80);
  second.setGeometry(120, 10, 100, 80);
  first.show();
  second.show();
  QCoreApplication::processEvents();

  const QString firstId = generateObjectId(&first);
  const QString secondId = generateObjectId(&second);
  QEXPECT_THAT(firstId, QIsNotEmpty());
  QEXPECT_THAT(secondId, QIsNotEmpty());
  QEXPECT_THAT(firstId, Ne(secondId));
  QEXPECT_THAT(findByObjectId(firstId), Eq(&first));
  QEXPECT_THAT(findByObjectId(secondId), Eq(&second));
}

void TestObjectId::testSerializeObjectInfo() {
  QWidget widget;
  widget.setObjectName(QStringLiteral("serializeInfoWidget"));
  widget.setGeometry(10, 20, 300, 200);
  widget.show();

  QCoreApplication::processEvents();

  QJsonObject info = serializeObjectInfo(&widget);

  QEXPECT_THAT(info, AllOf(
      HasJsonField("id"),
      HasJsonField("className", "QWidget"),
      HasJsonField("objectName", "serializeInfoWidget"),
      HasJsonField("visible", true),
      HasJsonField("geometry", AllOf(
          HasJsonField("x", 10),
          HasJsonField("y", 20),
          HasJsonField("width", 300),
          HasJsonField("height", 200)))));
}

void TestObjectId::testSerializeTree() {
  // Create a hierarchy
  QWidget parent;
  parent.setObjectName(QStringLiteral("treeRoot"));

  QWidget* child1 = new QWidget(&parent);
  child1->setObjectName(QStringLiteral("child1"));

  QWidget* child2 = new QWidget(&parent);
  child2->setObjectName(QStringLiteral("child2"));

  QPushButton* grandchild = new QPushButton(QStringLiteral("Leaf"), child1);
  grandchild->setObjectName(QStringLiteral("leafButton"));

  QCoreApplication::processEvents();

  // Serialize the tree
  QJsonObject tree = serializeObjectTree(&parent);

  QEXPECT_THAT(tree, AllOf(
      HasJsonField("id"),
      HasJsonField("className", "QWidget"),
      HasJsonField("objectName", "treeRoot"),
      HasJsonField("children", AllOf(
          JsonArraySize(2),
          JsonArrayContains(AllOf(
              HasJsonField("objectName", "child1"),
              HasJsonField("children", AllOf(
                  JsonArraySize(1),
                  JsonArrayContains(AllOf(
                      HasJsonField("objectName", "leafButton"),
                      HasJsonField("text", "Leaf")))))))))));

  // Debug output
  qDebug() << "Serialized tree:" << QJsonDocument(tree).toJson(QJsonDocument::Indented);
}

void TestObjectId::testSerializeTreeDepthLimit() {
  // Create a deeper hierarchy
  QWidget parent;
  parent.setObjectName(QStringLiteral("depthRoot"));

  QWidget* level1 = new QWidget(&parent);
  level1->setObjectName(QStringLiteral("level1"));

  QWidget* level2 = new QWidget(level1);
  level2->setObjectName(QStringLiteral("level2"));

  QWidget* level3 = new QWidget(level2);
  level3->setObjectName(QStringLiteral("level3"));

  QCoreApplication::processEvents();

  // Serialize with depth limit of 1 (root + one level of children)
  QJsonObject tree = serializeObjectTree(&parent, 1);

  QEXPECT_THAT(tree, HasJsonField("children", AllOf(
      JsonArraySize(1),
      JsonArrayContains(AllOf(
          HasJsonField("objectName", "level1"),
          DoesNotHaveJsonField("children"))))));

  // Serialize with no limit
  QJsonObject fullTree = serializeObjectTree(&parent, -1);
  QEXPECT_THAT(fullTree, HasJsonField("children",
      JsonArrayContains(AllOf(
          HasJsonField("objectName", "level1"),
          HasJsonField("children",
              JsonArrayContains(AllOf(
                  HasJsonField("objectName", "level2"),
                  HasJsonField("children"))))))));
}

QTEST_MAIN(TestObjectId)
#include "test_object_id.moc"
