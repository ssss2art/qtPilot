// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT
//
// Covers ID paths and tree traversal for QML items created by a delegate.
//
// A Repeater or ListView delegate is owned by the QML engine, not by the item
// above it, so its QObject parent is null while its VISUAL parent is the item
// it appears inside. Every walk here used to follow QObject parents only, which
// meant a delegate terminated the path in both directions:
//
//   - generateObjectId() stopped at the delegate, producing a rootless ID;
//   - serializeObjectTree() never listed it, so the tree omitted whole subtrees;
//   - scanExistingObjects() never reached it, so anything built before the probe
//     started stayed untracked -- and an untracked object gets a TRANSIENT ID
//     that is deliberately not cached, so an ID handed to a client by hitTest
//     could never be resolved again.
//
// In a real Qt Quick app that covers the navigation bars, tab strips and list
// rows: the controls most worth driving.

#include "core/object_registry.h"
#include "introspection/object_id.h"

#include <memory>

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QtTest>

using namespace qtPilot;

namespace {

// Three delegates inside a container, which is the shape that broke: the
// delegates are visual children of `strip` but QObject children of nobody.
constexpr const char* kRepeaterQml = R"QML(
import QtQuick 2.0
Item {
    objectName: "root"
    Item {
        objectName: "strip"
        Repeater {
            model: 3
            delegate: Item { objectName: "tab" + index }
        }
    }
}
)QML";

// The same shape with NOTHING per-instance to key on. This is the common real
// case -- delegates usually carry no name at all -- and it is what the objectName
// variant above cannot exercise, because a per-index objectName short-circuits
// disambiguation before it is ever consulted.
constexpr const char* kUnnamedDelegateQml = R"QML(
import QtQuick 2.0
Item {
    objectName: "root"
    Item {
        objectName: "strip"
        Repeater { model: 4; delegate: Rectangle { width: 10; height: 10 } }
    }
}
)QML";

// A QML `id` and a constant objectName are per-DECLARATION, not per-instance:
// every instance resolves to the same name, so neither disambiguates on its own.
constexpr const char* kSharedNameDelegateQml = R"QML(
import QtQuick 2.0
Item {
    objectName: "root"
    Item {
        objectName: "strip"
        Repeater { model: 3; delegate: Rectangle { id: rowRoot; objectName: "row" } }
    }
}
)QML";

std::unique_ptr<QObject> build(QQmlEngine* engine, const char* qml) {
  QQmlComponent component(engine);
  component.setData(QByteArray(qml), QUrl());
  std::unique_ptr<QObject> root(component.create());
  if (!root) {
    qWarning() << "QML failed to compile:" << component.errorString();
  }
  return root;
}

QQuickItem* childItemNamed(QQuickItem* parent, const QString& name) {
  const QList<QQuickItem*> children = parent->childItems();
  for (QQuickItem* child : children) {
    if (child->objectName() == name) {
      return child;
    }
  }
  return nullptr;
}

void collectIds(const QJsonObject& node, QStringList& out) {
  out.append(node.value(QStringLiteral("id")).toString());
  const QJsonArray children = node.value(QStringLiteral("children")).toArray();
  for (const QJsonValue& child : children) {
    collectIds(child.toObject(), out);
  }
}

}  // namespace

class TestQmlDelegateIds : public QObject {
  Q_OBJECT

 private slots:
  // Track object destruction, as a live probe does. Without the hooks a QML tree
  // freed at the end of one test function stays in the registry as a dangling
  // pointer, and the allocator readily hands the same address to the next test's
  // objects -- which then look already-tracked, so they never get their
  // objectNameChanged connection. That is a test-isolation artifact, not probe
  // behaviour, but it makes rename assertions fail depending on test order.
  void initTestCase() { installObjectHooks(); }
  void cleanupTestCase() { uninstallObjectHooks(); }
  void cleanup() { QCoreApplication::processEvents(); }

  // A delegate's ID must be a full path under its VISUAL parent, not a rootless
  // segment. The rootless form is what made these unresolvable.
  void delegateIdIsRootedAtVisualParent() {
    QQmlEngine engine;
    auto root = build(&engine, kRepeaterQml);
    QVERIFY(root);
    auto* rootItem = qobject_cast<QQuickItem*>(root.get());
    QVERIFY(rootItem);
    QQuickItem* strip = childItemNamed(rootItem, QStringLiteral("strip"));
    QVERIFY(strip);
    QQuickItem* tab0 = childItemNamed(strip, QStringLiteral("tab0"));
    QVERIFY2(tab0, "Repeater delegate missing from its visual parent");

    // Precondition: this is genuinely the parentless-but-visible case.
    QCOMPARE(tab0->parent(), nullptr);

    const QString id = generateObjectId(tab0);
    QCOMPARE(id, QStringLiteral("root/strip/tab0"));
  }

  // effectiveParent() and effectiveChildren() have to stay exact inverses, or
  // an ID built by walking up will not match a traversal walking down.
  void parentAndChildrenAgree() {
    QQmlEngine engine;
    auto root = build(&engine, kRepeaterQml);
    QVERIFY(root);
    auto* rootItem = qobject_cast<QQuickItem*>(root.get());
    QQuickItem* strip = childItemNamed(rootItem, QStringLiteral("strip"));
    QVERIFY(strip);

    const QList<QObject*> children = effectiveChildren(strip);
    int delegatesSeen = 0;
    for (QObject* child : children) {
      QCOMPARE(effectiveParent(child), strip);
      if (child->objectName().startsWith(QStringLiteral("tab"))) {
        ++delegatesSeen;
      }
    }
    QCOMPARE(delegatesSeen, 3);
  }

  // The ID a client is handed must resolve back to the same object -- the whole
  // point of handing one out.
  void delegateIdRoundTrips() {
    QQmlEngine engine;
    auto root = build(&engine, kRepeaterQml);
    QVERIFY(root);
    auto* rootItem = qobject_cast<QQuickItem*>(root.get());
    QQuickItem* strip = childItemNamed(rootItem, QStringLiteral("strip"));
    QQuickItem* tab1 = childItemNamed(strip, QStringLiteral("tab1"));
    QVERIFY(tab1);

    const QString id = generateObjectId(tab1);
    QCOMPARE(findByObjectId(id, root.get()), static_cast<QObject*>(tab1));
  }

  // Every delegate appears in the serialized tree, and each appears ONCE. A
  // visual child that also has a QObject parent must not be listed twice, or
  // one object ends up at two different paths.
  void treeListsEveryDelegateExactlyOnce() {
    QQmlEngine engine;
    auto root = build(&engine, kRepeaterQml);
    QVERIFY(root);

    const QJsonObject tree = serializeObjectTree(root.get());
    QStringList ids;
    collectIds(tree, ids);

    for (int i = 0; i < 3; ++i) {
      const QString expected = QStringLiteral("root/strip/tab%1").arg(i);
      QCOMPARE(ids.count(expected), 1);
    }

    QSet<QString> unique(ids.begin(), ids.end());
    QCOMPARE(unique.size(), ids.size());
  }

  // Objects that already exist when the probe starts are found only by the
  // scan, so the scan has to descend through delegates too.
  void scanReachesPreExistingDelegates() {
    QQmlEngine engine;
    auto root = build(&engine, kRepeaterQml);
    QVERIFY(root);
    auto* rootItem = qobject_cast<QQuickItem*>(root.get());
    QQuickItem* strip = childItemNamed(rootItem, QStringLiteral("strip"));
    QQuickItem* tab2 = childItemNamed(strip, QStringLiteral("tab2"));
    QVERIFY(tab2);

    ObjectRegistry::instance()->scanExistingObjects(root.get());
    QVERIFY2(ObjectRegistry::instance()->contains(tab2),
             "delegate created before the probe started was never tracked");

    // Tracked objects get a cached ID, which is what makes the ID resolvable
    // rather than transient.
    const QString id = ObjectRegistry::instance()->objectId(tab2);
    QCOMPARE(ObjectRegistry::instance()->findById(id), static_cast<QObject*>(tab2));
  }

  // The case the objectName-per-index fixture cannot reach: sibling delegates with
  // nothing per-instance to key on. Every one must still get a distinct id, and
  // that id must resolve back by PATH -- the registry's `~N` collision suffix is
  // not a substitute, because generateIdSegment() never emits `~` and so
  // findByObjectId() can never match it.
  void unnamedDelegatesGetDistinctResolvableIds() {
    QQmlEngine engine;
    auto root = build(&engine, kUnnamedDelegateQml);
    QVERIFY(root);
    auto* rootItem = qobject_cast<QQuickItem*>(root.get());
    QVERIFY(rootItem);
    QQuickItem* strip = childItemNamed(rootItem, QStringLiteral("strip"));
    QVERIFY(strip);

    const QList<QObject*> children = effectiveChildren(strip);
    QList<QObject*> delegates;
    for (QObject* child : children) {
      // parent() == nullptr is what makes it a delegate; the Repeater itself is
      // also an unnamed QQuickItem here, but it is a QObject child of `strip`.
      if (child->parent() == nullptr && qobject_cast<QQuickItem*>(child)) {
        delegates.append(child);
      }
    }
    QCOMPARE(delegates.size(), 4);

    QSet<QString> ids;
    for (QObject* delegate : delegates) {
      const QString id = generateObjectId(delegate);
      QVERIFY2(!ids.contains(id),
               qPrintable(QStringLiteral("duplicate id for sibling delegate: %1").arg(id)));
      ids.insert(id);
      // Resolvable by path, which is the whole point of handing an id out.
      QCOMPARE(findByObjectId(id, root.get()), delegate);
    }
  }

  // A shared QML `id` (or a constant objectName) must not be mistaken for a
  // unique segment. Disambiguation keys off the generated SEGMENT, not the class
  // name, precisely so this case is covered.
  void delegatesSharingADeclaredNameStillGetDistinctIds() {
    QQmlEngine engine;
    auto root = build(&engine, kSharedNameDelegateQml);
    QVERIFY(root);
    auto* rootItem = qobject_cast<QQuickItem*>(root.get());
    QQuickItem* strip = childItemNamed(rootItem, QStringLiteral("strip"));
    QVERIFY(strip);

    QSet<QString> ids;
    int delegatesSeen = 0;
    const QList<QObject*> children = effectiveChildren(strip);
    for (QObject* child : children) {
      if (child->parent() != nullptr) {
        continue;  // not a delegate
      }
      ++delegatesSeen;
      const QString id = generateObjectId(child);
      QVERIFY2(!ids.contains(id),
               qPrintable(QStringLiteral("delegates collided on id: %1").arg(id)));
      ids.insert(id);
      QCOMPARE(findByObjectId(id, root.get()), child);
    }
    QCOMPARE(delegatesSeen, 3);
  }

  // Renaming a VISUAL ancestor must refresh the ids cached for delegates beneath
  // it. refreshDescendantIds() walked QObject children only, so a delegate kept a
  // cached id containing the old segment forever, with no alias to redirect it.
  void renamingAVisualAncestorRefreshesDelegateIds() {
    QQmlEngine engine;
    auto root = build(&engine, kRepeaterQml);
    QVERIFY(root);
    auto* rootItem = qobject_cast<QQuickItem*>(root.get());
    QQuickItem* strip = childItemNamed(rootItem, QStringLiteral("strip"));
    QVERIFY(strip);
    QQuickItem* tab0 = childItemNamed(strip, QStringLiteral("tab0"));
    QVERIFY(tab0);

    auto* registry = ObjectRegistry::instance();
    registry->scanExistingObjects(root.get());
    QCOMPARE(registry->objectId(tab0), QStringLiteral("root/strip/tab0"));

    strip->setObjectName(QStringLiteral("navStrip"));
    // The refresh is wired through a queued connection.
    QTRY_COMPARE(registry->objectId(tab0), QStringLiteral("root/navStrip/tab0"));
    QCOMPARE(registry->findById(QStringLiteral("root/navStrip/tab0")), static_cast<QObject*>(tab0));
  }

  // A root-scoped search has to mean the same thing as "under this root in the
  // tree". Scoped lookups walked QObject links only, so they returned nothing for
  // delegates that qt.objects.tree listed under that very root.
  void rootScopedLookupsReachDelegates() {
    QQmlEngine engine;
    auto root = build(&engine, kRepeaterQml);
    QVERIFY(root);
    auto* rootItem = qobject_cast<QQuickItem*>(root.get());
    QQuickItem* strip = childItemNamed(rootItem, QStringLiteral("strip"));
    QVERIFY(strip);
    QQuickItem* tab1 = childItemNamed(strip, QStringLiteral("tab1"));
    QVERIFY(tab1);

    auto* registry = ObjectRegistry::instance();
    QCOMPARE(registry->findByObjectName(QStringLiteral("tab1"), root.get()),
             static_cast<QObject*>(tab1));

    const QList<QObject*> found = registry->findAllByClassName(QStringLiteral("QQuickItem"), strip);
    QVERIFY2(found.contains(tab1), "class-name search scoped to a root missed a delegate");
  }

  // The effective hierarchy merges two axes that Qt cycle-checks only
  // independently, so a walk over it must be bounded. Qt accepts this shape
  // silently; before the bound, generateObjectId() looped until the host process
  // was out of memory.
  void interlockedParentAxesDoNotHang() {
    auto outer = std::make_unique<QQuickItem>();
    auto* mid = new QQuickItem(outer.get());  // QObject child of outer
    mid->setParentItem(nullptr);
    outer->setParentItem(mid);  // ...and outer's VISUAL parent

    // Precondition: this really is the interlocked shape, accepted by Qt.
    QCOMPARE(effectiveParent(outer.get()), static_cast<QObject*>(mid));
    QCOMPARE(effectiveParent(mid), static_cast<QObject*>(outer.get()));

    // Must terminate rather than hang. The id is truncated and useless, which is
    // the correct outcome for a malformed graph -- what matters is that the host
    // application survives.
    const QString id = generateObjectId(outer.get());
    QVERIFY(!id.isEmpty());
  }
};

QTEST_MAIN(TestQmlDelegateIds)
#include "test_qml_delegate_ids.moc"
