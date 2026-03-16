// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

// NOTE: This test requires QTPILOT_ENABLED=0 environment variable to be set
// to prevent full probe initialization. CTest sets this automatically.

#include "core/object_registry.h"
#include "introspection/object_id.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QPushButton>
#include <QWidget>
#include <QtTest>

using namespace qtPilot;

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
  QVERIFY(id.endsWith(QStringLiteral("submitBtn")));
  QVERIFY(id.contains(QStringLiteral("centralWidget")));
  QVERIFY(id.contains(QStringLiteral("mainWindow")));

  // Verify format is "parent/child/grandchild"
  QStringList segments = id.split(QLatin1Char('/'));
  QVERIFY(segments.size() >= 3);
  QCOMPARE(segments.last(), QStringLiteral("submitBtn"));
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
  QVERIFY(id.contains(QStringLiteral("text_OK")));

  // Test text sanitization
  QPushButton* longTextButton = new QPushButton(
      QStringLiteral("This is a very long button label that exceeds twenty characters"), &parent);
  id = generateObjectId(longTextButton);
  // Should be truncated and sanitized
  QVERIFY(id.contains(QStringLiteral("text_")));
  // Segment should be <= 25 chars ("text_" + 20 chars max)
  QStringList segments = id.split(QLatin1Char('/'));
  QString lastSegment = segments.last();
  QVERIFY(lastSegment.startsWith(QStringLiteral("text_")));
  QVERIFY(lastSegment.length() <= 25);
}

void TestObjectId::testIdWithClassName() {
  // Create objects without objectName or text
  QWidget parent;
  parent.setObjectName(QStringLiteral("classTestParent"));

  QObject* child = new QObject(&parent);
  // No objectName, no text property - should use class name

  QCoreApplication::processEvents();

  QString id = generateObjectId(child);
  QVERIFY(id.contains(QStringLiteral("QObject")));
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
  QVERIFY(id1 != id2);
  QVERIFY(id2 != id3);
  QVERIFY(id1 != id3);

  // Should contain disambiguation suffixes
  QStringList segments1 = id1.split(QLatin1Char('/'));
  QStringList segments2 = id2.split(QLatin1Char('/'));
  QStringList segments3 = id3.split(QLatin1Char('/'));

  // Each should end with QPushButton#N
  QVERIFY(segments1.last().startsWith(QStringLiteral("QPushButton")));
  QVERIFY(segments2.last().startsWith(QStringLiteral("QPushButton")));
  QVERIFY(segments3.last().startsWith(QStringLiteral("QPushButton")));

  // Check for #N suffixes (order may vary)
  QStringList endings;
  endings << segments1.last() << segments2.last() << segments3.last();
  QVERIFY(endings.contains(QStringLiteral("QPushButton#1")));
  QVERIFY(endings.contains(QStringLiteral("QPushButton#2")));
  QVERIFY(endings.contains(QStringLiteral("QPushButton#3")));
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
  QCOMPARE(found, button);

  // Search for non-existent ID
  QObject* notFound = findByObjectId(QStringLiteral("nonexistent/path/here"), &parent);
  QVERIFY(notFound == nullptr);
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
  QVERIFY(segments.size() >= 3);  // App/globalTestRoot/globalTestChild

  // Critical test: findByObjectId with NO root must resolve the full path
  QObject* found = findByObjectId(childId);
  QVERIFY2(found != nullptr,
           qPrintable(QStringLiteral("findByObjectId failed for global ID: ") + childId));
  QCOMPARE(found, child);

  // Also verify finding the top-level object itself
  QString topLevelId = generateObjectId(topLevel);
  QObject* foundTopLevel = findByObjectId(topLevelId);
  QVERIFY2(foundTopLevel != nullptr,
           qPrintable(QStringLiteral("findByObjectId failed for top-level ID: ") + topLevelId));
  QCOMPARE(foundTopLevel, topLevel);

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
  QVERIFY(!id.isEmpty());
  qDebug() << "Button ID from registry:" << id;

  // The cached ID was generated at construction time, so it won't
  // include the objectName. Just verify it's non-empty and lookup works.

  // Find via registry using the cached ID
  QObject* found = registry->findById(id);
  QCOMPARE(found, button);

  // Verify registry contains the object
  QVERIFY(registry->contains(button));

  // Clean up
  delete parent;
  QCoreApplication::processEvents();

  // After deletion, findById should return nullptr (QPointer detected deletion)
  QObject* deleted = registry->findById(id);
  QVERIFY(deleted == nullptr);
}

void TestObjectId::testSerializeObjectInfo() {
  QWidget widget;
  widget.setObjectName(QStringLiteral("serializeInfoWidget"));
  widget.setGeometry(10, 20, 300, 200);
  widget.show();

  QCoreApplication::processEvents();

  QJsonObject info = serializeObjectInfo(&widget);

  // Check required fields
  QVERIFY(info.contains(QStringLiteral("id")));
  QVERIFY(info.contains(QStringLiteral("className")));
  QCOMPARE(info[QStringLiteral("className")].toString(), QStringLiteral("QWidget"));
  QCOMPARE(info[QStringLiteral("objectName")].toString(), QStringLiteral("serializeInfoWidget"));

  // Widget-specific fields
  QVERIFY(info.contains(QStringLiteral("visible")));
  QVERIFY(info.contains(QStringLiteral("geometry")));

  QJsonObject geom = info[QStringLiteral("geometry")].toObject();
  QCOMPARE(geom[QStringLiteral("x")].toInt(), 10);
  QCOMPARE(geom[QStringLiteral("y")].toInt(), 20);
  QCOMPARE(geom[QStringLiteral("width")].toInt(), 300);
  QCOMPARE(geom[QStringLiteral("height")].toInt(), 200);
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

  // Verify root
  QVERIFY(tree.contains(QStringLiteral("id")));
  QCOMPARE(tree[QStringLiteral("className")].toString(), QStringLiteral("QWidget"));
  QCOMPARE(tree[QStringLiteral("objectName")].toString(), QStringLiteral("treeRoot"));

  // Verify children exist
  QVERIFY(tree.contains(QStringLiteral("children")));
  QJsonArray children = tree[QStringLiteral("children")].toArray();
  QCOMPARE(children.size(), 2);

  // Find child1 and verify its grandchild
  bool foundChild1WithGrandchild = false;
  for (int i = 0; i < children.size(); ++i) {
    QJsonObject child = children[i].toObject();
    if (child[QStringLiteral("objectName")].toString() == QStringLiteral("child1")) {
      QJsonArray grandchildren = child[QStringLiteral("children")].toArray();
      QCOMPARE(grandchildren.size(), 1);
      QJsonObject gc = grandchildren[0].toObject();
      QCOMPARE(gc[QStringLiteral("objectName")].toString(), QStringLiteral("leafButton"));
      QCOMPARE(gc[QStringLiteral("text")].toString(), QStringLiteral("Leaf"));
      foundChild1WithGrandchild = true;
    }
  }
  QVERIFY(foundChild1WithGrandchild);

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

  // Root should have children
  QVERIFY(tree.contains(QStringLiteral("children")));
  QJsonArray children = tree[QStringLiteral("children")].toArray();
  QCOMPARE(children.size(), 1);

  // Level1 should NOT have children due to depth limit
  QJsonObject level1Obj = children[0].toObject();
  QCOMPARE(level1Obj[QStringLiteral("objectName")].toString(), QStringLiteral("level1"));
  QVERIFY(!level1Obj.contains(QStringLiteral("children")));

  // Serialize with no limit
  QJsonObject fullTree = serializeObjectTree(&parent, -1);
  QJsonArray fullChildren = fullTree[QStringLiteral("children")].toArray();
  QJsonObject fullLevel1 = fullChildren[0].toObject();
  QVERIFY(fullLevel1.contains(QStringLiteral("children")));  // Should have children

  QJsonArray level2Children = fullLevel1[QStringLiteral("children")].toArray();
  QJsonObject fullLevel2 = level2Children[0].toObject();
  QVERIFY(fullLevel2.contains(QStringLiteral("children")));  // Should have grandchildren
}

QTEST_MAIN(TestObjectId)
#include "test_object_id.moc"
