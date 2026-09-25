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

#include "common/qt_matchers.h"
#include "core/object_registry.h"
#include "introspection/object_id.h"

#include <memory>

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QtTest>

using namespace qtPilot;
using namespace qtPilot::test;

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

// A declared child and delegates of the SAME class under one parent. This shape --
// a background rectangle plus a Repeater of rectangles -- is ordinary Qt Quick, and
// it is what an earlier split-path implementation got wrong: the two paths counted
// different populations of one sibling list and both handed out "#1".
constexpr const char* kMixedParentageQml = R"QML(
import QtQuick 2.0
Item {
    objectName: "root"
    Item {
        objectName: "strip"
        Rectangle { width: 5; height: 5 }
        Repeater { model: 2; delegate: Rectangle { width: 5; height: 5 } }
    }
}
)QML";

// A declared QML `id` AND a per-instance objectName. Disambiguating on objectName
// once concluded every delegate was unique and emitted one shared id for all of
// them. The id is shared, so the unique objectName now names each delegate.
constexpr const char* kIdPlusIndexedNameQml = R"QML(
import QtQuick 2.0
Item {
    objectName: "root"
    Item {
        objectName: "strip"
        Repeater { model: 3; delegate: Rectangle { id: rowRoot; objectName: "row" + index } }
    }
}
)QML";

// Only some delegates carry an objectName unique among their siblings. Those take
// it; the rest keep the shared id with the position they always had, so their ids
// do not move when a neighbour gains a name.
constexpr const char* kPartlyNamedQml = R"QML(
import QtQuick 2.0
Item {
    objectName: "root"
    Item {
        objectName: "strip"
        Repeater { model: 4; delegate: Rectangle { id: rowRoot; objectName: ["alpha", "dup", "dup", ""][index] } }
    }
}
)QML";

// A static sibling whose own segment is `row1` (from its QML id). The delegate
// named "row1" must not take it, or the two would share one id.
constexpr const char* kNameTakenBySiblingSegmentQml = R"QML(
import QtQuick 2.0
Item {
    objectName: "root"
    Item {
        objectName: "strip"
        Item { id: row1 }
        Repeater { model: 3; delegate: Rectangle { id: rowRoot; objectName: "row" + index } }
    }
}
)QML";

// An objectName spelled like a positional segment. Promoting it would hand the
// third delegate's `rowRoot#3` to the first.
constexpr const char* kNameShapedLikeASuffixQml = R"QML(
import QtQuick 2.0
Item {
    objectName: "root"
    Item {
        objectName: "strip"
        Repeater { model: 3; delegate: Rectangle { id: rowRoot; objectName: index === 0 ? "rowRoot#3" : "" } }
    }
}
)QML";

// Distinct `text` per row: these are content-addressed (text_Alpha, text_Beta, ...)
// and must NOT pick up a positional suffix, which would make an id shift when a row
// is inserted above it.
constexpr const char* kTextKeyedQml = R"QML(
import QtQuick 2.0
Item {
    objectName: "root"
    Item {
        objectName: "strip"
        Repeater { model: ["Alpha", "Beta", "Gamma"]; delegate: Text { text: modelData } }
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

// Walks the effective hierarchy and records what generateObjectId() ACTUALLY emits,
// deliberately bypassing ObjectRegistry so no deduplication can mask a collision.
void collectGeneratedIds(QObject* obj, QStringList& out) {
  out.append(generateObjectId(obj));
  const QList<QObject*> children = effectiveChildren(obj);
  for (QObject* child : children) {
    collectGeneratedIds(child, out);
  }
}

/// Generated ids under `root`, both directly and inside an IdGenerationScope. The
/// scope answers from a per-parent cache, so it must agree with the direct path.
QStringList generatedIdsBothWays(QObject* root) {
  QStringList direct;
  collectGeneratedIds(root, direct);
  QStringList scoped;
  {
    IdGenerationScope scope;
    collectGeneratedIds(root, scoped);
  }
  // Compared joined: Qt 5.15's QList::operator== trips MSVC's STL4043
  // deprecation, which this build treats as an error. Ids never hold '\n'.
  if (scoped.join(QLatin1Char('\n')) != direct.join(QLatin1Char('\n'))) {
    qWarning() << "scoped ids" << scoped << "differ from direct ids" << direct;
    return {};
  }
  return direct;
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
    QCHECK_THAT(root.get(), NotNull());
    auto* rootItem = qobject_cast<QQuickItem*>(root.get());
    QCHECK_THAT(rootItem, NotNull());
    QQuickItem* strip = childItemNamed(rootItem, QStringLiteral("strip"));
    QCHECK_THAT(strip, NotNull());
    QQuickItem* tab0 = childItemNamed(strip, QStringLiteral("tab0"));
    QCHECK_THAT(tab0, NotNull());

    // Precondition: this is genuinely the parentless-but-visible case.
    QEXPECT_THAT(tab0->parent(), IsNull());

    const QString id = generateObjectId(tab0);
    QEXPECT_THAT(id, QStrEq("root/strip/tab0"));
  }

  // effectiveParent() and effectiveChildren() have to stay exact inverses, or
  // an ID built by walking up will not match a traversal walking down.
  void parentAndChildrenAgree() {
    QQmlEngine engine;
    auto root = build(&engine, kRepeaterQml);
    QCHECK_THAT(root.get(), NotNull());
    auto* rootItem = qobject_cast<QQuickItem*>(root.get());
    QCHECK_THAT(rootItem, NotNull());
    QQuickItem* strip = childItemNamed(rootItem, QStringLiteral("strip"));
    QCHECK_THAT(strip, NotNull());

    const QList<QObject*> children = effectiveChildren(strip);
    int delegatesSeen = 0;
    for (QObject* child : children) {
      QEXPECT_THAT(effectiveParent(child), Eq(strip));
      if (child->objectName().startsWith(QStringLiteral("tab"))) {
        ++delegatesSeen;
      }
    }
    QEXPECT_THAT(delegatesSeen, Eq(3));
  }

  // The ID a client is handed must resolve back to the same object -- the whole
  // point of handing one out.
  void delegateIdRoundTrips() {
    QQmlEngine engine;
    auto root = build(&engine, kRepeaterQml);
    QCHECK_THAT(root.get(), NotNull());
    auto* rootItem = qobject_cast<QQuickItem*>(root.get());
    QCHECK_THAT(rootItem, NotNull());
    QQuickItem* strip = childItemNamed(rootItem, QStringLiteral("strip"));
    QCHECK_THAT(strip, NotNull());
    QQuickItem* tab1 = childItemNamed(strip, QStringLiteral("tab1"));
    QCHECK_THAT(tab1, NotNull());

    const QString id = generateObjectId(tab1);
    QEXPECT_THAT(findByObjectId(id, root.get()), Eq(static_cast<QObject*>(tab1)));
  }

  // Every delegate appears in the serialized tree, and each appears ONCE. A
  // visual child that also has a QObject parent must not be listed twice, or
  // one object ends up at two different paths.
  void treeListsEveryDelegateExactlyOnce() {
    QQmlEngine engine;
    auto root = build(&engine, kRepeaterQml);
    QCHECK_THAT(root.get(), NotNull());

    const QJsonObject tree = serializeObjectTree(root.get());
    QStringList ids;
    collectIds(tree, ids);

    for (int i = 0; i < 3; ++i) {
      const QString expected = QStringLiteral("root/strip/tab%1").arg(i);
      QEXPECT_THAT(ids.count(expected), Eq(1));
    }

    QSet<QString> unique(ids.begin(), ids.end());
    QEXPECT_THAT(unique.size(), Eq(ids.size()));
  }

  // Objects that already exist when the probe starts are found only by the
  // scan, so the scan has to descend through delegates too.
  void scanReachesPreExistingDelegates() {
    QQmlEngine engine;
    auto root = build(&engine, kRepeaterQml);
    QCHECK_THAT(root.get(), NotNull());
    auto* rootItem = qobject_cast<QQuickItem*>(root.get());
    QCHECK_THAT(rootItem, NotNull());
    QQuickItem* strip = childItemNamed(rootItem, QStringLiteral("strip"));
    QCHECK_THAT(strip, NotNull());
    QQuickItem* tab2 = childItemNamed(strip, QStringLiteral("tab2"));
    QCHECK_THAT(tab2, NotNull());

    ObjectRegistry::instance()->scanExistingObjects(root.get());
    QEXPECT_THAT(ObjectRegistry::instance()->contains(tab2), IsTrue());

    // Tracked objects get a cached ID, which is what makes the ID resolvable
    // rather than transient.
    const QString id = ObjectRegistry::instance()->objectId(tab2);
    QEXPECT_THAT(ObjectRegistry::instance()->findById(id), Eq(static_cast<QObject*>(tab2)));
  }

  // The case the objectName-per-index fixture cannot reach: sibling delegates with
  // nothing per-instance to key on. Every one must still get a distinct id, and
  // that id must resolve back by PATH -- the registry's `~N` collision suffix is
  // not a substitute, because generateIdSegment() never emits `~` and so
  // findByObjectId() can never match it.
  void unnamedDelegatesGetDistinctResolvableIds() {
    QQmlEngine engine;
    auto root = build(&engine, kUnnamedDelegateQml);
    QCHECK_THAT(root.get(), NotNull());
    auto* rootItem = qobject_cast<QQuickItem*>(root.get());
    QCHECK_THAT(rootItem, NotNull());
    QQuickItem* strip = childItemNamed(rootItem, QStringLiteral("strip"));
    QCHECK_THAT(strip, NotNull());

    const QList<QObject*> children = effectiveChildren(strip);
    QList<QObject*> delegates;
    for (QObject* child : children) {
      // parent() == nullptr is what makes it a delegate; the Repeater itself is
      // also an unnamed QQuickItem here, but it is a QObject child of `strip`.
      if (child->parent() == nullptr && qobject_cast<QQuickItem*>(child)) {
        delegates.append(child);
      }
    }
    QEXPECT_THAT(delegates.size(), Eq(4));

    QSet<QString> ids;
    for (QObject* delegate : delegates) {
      const QString id = generateObjectId(delegate);
      QEXPECT_THAT(ids.contains(id), IsFalse());
      ids.insert(id);
      // Resolvable by path, which is the whole point of handing an id out.
      QEXPECT_THAT(findByObjectId(id, root.get()), Eq(delegate));
    }
  }

  // A shared QML `id` (or a constant objectName) must not be mistaken for a
  // unique segment. Disambiguation keys off the generated SEGMENT, not the class
  // name, precisely so this case is covered.
  void delegatesSharingADeclaredNameStillGetDistinctIds() {
    QQmlEngine engine;
    auto root = build(&engine, kSharedNameDelegateQml);
    QCHECK_THAT(root.get(), NotNull());
    auto* rootItem = qobject_cast<QQuickItem*>(root.get());
    QCHECK_THAT(rootItem, NotNull());
    QQuickItem* strip = childItemNamed(rootItem, QStringLiteral("strip"));
    QCHECK_THAT(strip, NotNull());

    QSet<QString> ids;
    int delegatesSeen = 0;
    const QList<QObject*> children = effectiveChildren(strip);
    for (QObject* child : children) {
      if (child->parent() != nullptr) {
        continue;  // not a delegate
      }
      ++delegatesSeen;
      const QString id = generateObjectId(child);
      QEXPECT_THAT(ids.contains(id), IsFalse());
      ids.insert(id);
      QEXPECT_THAT(findByObjectId(id, root.get()), Eq(child));
    }
    QEXPECT_THAT(delegatesSeen, Eq(3));
  }

  // Renaming a VISUAL ancestor must refresh the ids cached for delegates beneath
  // it. refreshDescendantIds() walked QObject children only, so a delegate kept a
  // cached id containing the old segment forever, with no alias to redirect it.
  void renamingAVisualAncestorRefreshesDelegateIds() {
    QQmlEngine engine;
    auto root = build(&engine, kRepeaterQml);
    QCHECK_THAT(root.get(), NotNull());
    auto* rootItem = qobject_cast<QQuickItem*>(root.get());
    QCHECK_THAT(rootItem, NotNull());
    QQuickItem* strip = childItemNamed(rootItem, QStringLiteral("strip"));
    QCHECK_THAT(strip, NotNull());
    QQuickItem* tab0 = childItemNamed(strip, QStringLiteral("tab0"));
    QCHECK_THAT(tab0, NotNull());

    auto* registry = ObjectRegistry::instance();
    registry->scanExistingObjects(root.get());
    QEXPECT_THAT(registry->objectId(tab0), QStrEq("root/strip/tab0"));

    strip->setObjectName(QStringLiteral("navStrip"));
    // The refresh is wired through a queued connection.
    QTRY_COMPARE(registry->objectId(tab0), QStringLiteral("root/navStrip/tab0"));
    QEXPECT_THAT(registry->findById(QStringLiteral("root/navStrip/tab0")),
                 Eq(static_cast<QObject*>(tab0)));
  }

  // A root-scoped search has to mean the same thing as "under this root in the
  // tree". Scoped lookups walked QObject links only, so they returned nothing for
  // delegates that qt.objects.tree listed under that very root.
  void rootScopedLookupsReachDelegates() {
    QQmlEngine engine;
    auto root = build(&engine, kRepeaterQml);
    QCHECK_THAT(root.get(), NotNull());
    auto* rootItem = qobject_cast<QQuickItem*>(root.get());
    QCHECK_THAT(rootItem, NotNull());
    QQuickItem* strip = childItemNamed(rootItem, QStringLiteral("strip"));
    QCHECK_THAT(strip, NotNull());
    QQuickItem* tab1 = childItemNamed(strip, QStringLiteral("tab1"));
    QCHECK_THAT(tab1, NotNull());

    auto* registry = ObjectRegistry::instance();
    QEXPECT_THAT(registry->findByObjectName(QStringLiteral("tab1"), root.get()),
                 Eq(static_cast<QObject*>(tab1)));

    const QList<QObject*> found = registry->findAllByClassName(QStringLiteral("QQuickItem"), strip);
    QEXPECT_THAT(found, Contains(static_cast<QObject*>(tab1)));
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
    QEXPECT_THAT(effectiveParent(outer.get()), Eq(static_cast<QObject*>(mid)));
    QEXPECT_THAT(effectiveParent(mid), Eq(static_cast<QObject*>(outer.get())));

    // Must terminate rather than hang. The id is truncated and useless, which is
    // the correct outcome for a malformed graph -- what matters is that the host
    // application survives.
    const QString id = generateObjectId(outer.get());
    QEXPECT_THAT(id, QIsNotEmpty());
  }

  // A QML id is per-declaration, so every delegate shares it and ends up `id#N`,
  // which says nothing about which row it is. A unique objectName names the row.
  void uniqueObjectNameWinsOverSharedQmlId() {
    QQmlEngine engine;
    auto root = build(&engine, kIdPlusIndexedNameQml);
    QCHECK_THAT(root.get(), NotNull());

    const QStringList ids = generatedIdsBothWays(root.get());
    QEXPECT_THAT(ids, AllOf(Contains(QStringLiteral("root/strip/row0")),
                            Contains(QStringLiteral("root/strip/row1")),
                            Contains(QStringLiteral("root/strip/row2"))));
    QEXPECT_THAT(ids, Not(Contains(QStringLiteral("root/strip/rowRoot#1"))));
    for (const char* name : {"row0", "row1", "row2"}) {
      const QString id = QStringLiteral("root/strip/%1").arg(QLatin1String(name));
      QObject* found = findByObjectId(id, root.get());
      QCHECK_THAT(found, NotNull());
      QEXPECT_THAT(found->objectName(), QStrEq(QLatin1String(name)));
    }
  }

  // Only a name no sibling shares is promoted, and the rest keep their positions.
  void sharedObjectNameIsNotPromoted() {
    QQmlEngine engine;
    auto root = build(&engine, kPartlyNamedQml);
    QCHECK_THAT(root.get(), NotNull());

    const QStringList ids = generatedIdsBothWays(root.get());
    QEXPECT_THAT(ids, AllOf(Contains(QStringLiteral("root/strip/alpha")),
                            Contains(QStringLiteral("root/strip/rowRoot#2")),
                            Contains(QStringLiteral("root/strip/rowRoot#3")),
                            Contains(QStringLiteral("root/strip/rowRoot#4"))));
    QEXPECT_THAT(ids, Not(Contains(QStringLiteral("root/strip/dup"))));
  }

  // A name a sibling already emits as its segment stays unpromoted.
  void objectNameTakenBySiblingSegmentIsNotPromoted() {
    QQmlEngine engine;
    auto root = build(&engine, kNameTakenBySiblingSegmentQml);
    QCHECK_THAT(root.get(), NotNull());

    const QStringList ids = generatedIdsBothWays(root.get());
    QEXPECT_THAT(ids, AllOf(Contains(QStringLiteral("root/strip/row0")),
                            Contains(QStringLiteral("root/strip/row1")),
                            Contains(QStringLiteral("root/strip/rowRoot#2")),
                            Contains(QStringLiteral("root/strip/row2"))));
  }

  // A name spelled `id#N` cannot be promoted without colliding with a position.
  void objectNameShapedLikeASuffixIsNotPromoted() {
    QQmlEngine engine;
    auto root = build(&engine, kNameShapedLikeASuffixQml);
    QCHECK_THAT(root.get(), NotNull());

    const QStringList ids = generatedIdsBothWays(root.get());
    QEXPECT_THAT(ids, AllOf(Contains(QStringLiteral("root/strip/rowRoot#1")),
                            Contains(QStringLiteral("root/strip/rowRoot#2")),
                            Contains(QStringLiteral("root/strip/rowRoot#3"))));
  }

  // Every id the GENERATOR produces must already be unique, before the registry's
  // `~N` collision suffix is applied. Asserting on registry ids instead would be
  // vacuous -- allocateUniqueIdLocked() uniquifies them by construction, so the
  // assertion would hold no matter what generateIdSegment() emitted.
  void generatedIdsAreUniqueBeforeRegistryDeduplication() {
    struct Fixture {
      const char* label;
      const char* qml;
    };
    const Fixture fixtures[] = {
        {"mixed parentage", kMixedParentageQml},
        {"declared id + per-index objectName", kIdPlusIndexedNameQml},
        {"unnamed delegates", kUnnamedDelegateQml},
        {"shared declared name", kSharedNameDelegateQml},
        {"text-keyed", kTextKeyedQml},
        {"partly named", kPartlyNamedQml},
        {"name taken by sibling segment", kNameTakenBySiblingSegmentQml},
        {"name shaped like a suffix", kNameShapedLikeASuffixQml},
    };

    for (const Fixture& fixture : fixtures) {
      QQmlEngine engine;
      auto root = build(&engine, fixture.qml);
      QCHECK_THAT(root.get(), NotNull());

      QStringList ids;
      collectGeneratedIds(root.get(), ids);
      QSet<QString> unique(ids.begin(), ids.end());
      if (unique.size() != ids.size()) {
        QStringList sorted = ids;
        sorted.sort();
        QFAIL(qPrintable(QStringLiteral("%1: duplicate generated ids: %2")
                             .arg(QLatin1String(fixture.label), sorted.join(QLatin1String(", ")))));
      }

      // And no id may depend on the registry suffix, which findByObjectId() cannot
      // reproduce -- that is the whole reason uniqueness has to hold up here.
      for (const QString& id : ids) {
        QEXPECT_THAT(id.contains(QLatin1Char('~')), IsFalse());
      }
    }
  }

  // Content-addressed segments must stay content-addressed: a positional suffix
  // would silently re-point a client's id when a row is inserted above it.
  void textKeyedDelegatesKeepContentAddressedIds() {
    QQmlEngine engine;
    auto root = build(&engine, kTextKeyedQml);
    QCHECK_THAT(root.get(), NotNull());

    QStringList ids;
    collectGeneratedIds(root.get(), ids);
    for (const char* label : {"Alpha", "Beta", "Gamma"}) {
      const QString expected = QStringLiteral("root/strip/text_%1").arg(QLatin1String(label));
      QEXPECT_THAT(ids, Contains(expected));
    }
  }
};

QTEST_MAIN(TestQmlDelegateIds)
#include "test_qml_delegate_ids.moc"
