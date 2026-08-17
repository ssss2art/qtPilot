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
};

QTEST_MAIN(TestQmlDelegateIds)
#include "test_qml_delegate_ids.moc"
