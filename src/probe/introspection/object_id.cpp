// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "introspection/object_id.h"

#include "core/object_registry.h"
#include "introspection/qml_inspector.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QJsonArray>
#include <QMetaProperty>
#include <QWidget>
#include <QWindow>

#ifdef QTPILOT_HAS_QML
#include <QQuickItem>
#endif

namespace qtPilot {

namespace {

/// @brief Sanitize a string for use in an ID segment.
/// Takes first 20 characters, replaces non-alphanumeric with underscores.
QString sanitizeForId(const QString& input) {
  QString result;
  result.reserve(qMin(input.length(), 20));

  for (int i = 0; i < qMin(input.length(), 20); ++i) {
    QChar ch = input.at(i);
    if (ch.isLetterOrNumber()) {
      result.append(ch);
    } else {
      result.append(QLatin1Char('_'));
    }
  }

  // Trim trailing underscores
  while (result.endsWith(QLatin1Char('_')) && result.length() > 1) {
    result.chop(1);
  }

  return result;
}

/// @brief Get the text property value if it exists.
/// Returns empty string if no text property or value is empty.
QString getTextProperty(QObject* obj) {
  if (!obj) {
    return QString();
  }

  // Check for "text" property via meta-object system
  const QMetaObject* meta = obj->metaObject();
  int textIndex = meta->indexOfProperty("text");
  if (textIndex >= 0) {
    QMetaProperty textProp = meta->property(textIndex);
    if (textProp.isReadable()) {
      QVariant value = textProp.read(obj);
      if (value.canConvert<QString>()) {
        return value.toString();
      }
    }
  }

  return QString();
}

/// @brief The ID segment for an object BEFORE sibling disambiguation.
///
/// Split out from generateIdSegment() so that the disambiguator can compare the
/// thing that actually has to be unique. Comparing class names instead was the
/// bug: sibling delegates share a class, but they also share a QML `id` and can
/// share a constant objectName, so a class-name comparison both missed real
/// collisions and could not see that two differently-classed objects had landed
/// on the same segment.
QString baseIdSegment(QObject* obj) {
  if (!obj) {
    return QString();
  }

#ifdef QTPILOT_HAS_QML
  // Priority 0 (QML only): QML id takes highest priority. The segment-only variant
  // skips resolving the context's base URL, which nothing here reads and which
  // sibling disambiguation would otherwise pay for once per sibling.
  QmlItemInfo qmlInfo = inspectQmlItemForSegment(obj);
  if (qmlInfo.isQmlItem && !qmlInfo.qmlId.isEmpty()) {
    return qmlInfo.qmlId;
  }
#endif

  // Priority 1: objectName (if set and non-empty)
  QString name = obj->objectName();
  if (!name.isEmpty()) {
    return name;
  }

  // Priority 2: text property (if exists and non-empty)
  QString text = getTextProperty(obj);
  if (!text.isEmpty()) {
    return QStringLiteral("text_") + sanitizeForId(text);
  }

  // Priority 3: short QML type name, else class name.
#ifdef QTPILOT_HAS_QML
  if (qmlInfo.isQmlItem) {
    return qmlInfo.shortTypeName;
  }
#endif
  return QString::fromLatin1(obj->metaObject()->className());
}

/// @brief Position of this object among the effective siblings that would emit
/// the same base segment. Returns -1 when the base segment is already unique.
///
/// Two things matter here, and both were previously wrong for QML delegates:
///
///   - The hierarchy. This walks effectiveParent()/effectiveChildren(), the same
///     axis generateObjectId() and the tree walkers use. Asking obj->parent()
///     meant every delegate -- whose QObject parent is null by definition --
///     fell into the "no disambiguation context" branch and got no suffix, so
///     all N siblings emitted one identical segment.
///   - The key. Comparing base segments rather than class names, because a QML
///     `id` and a constant objectName are per-DECLARATION, not per-instance:
///     every instance of `delegate: Rectangle { id: row }` yields "row".
///
/// The suffix must stay a form matchesSegment() can reproduce (`#N`), so an ID
/// resolves by path. The registry's `~N` collision suffix cannot be reproduced
/// that way, which is why it must remain a last resort rather than the mechanism
/// delegates rely on.
int getSiblingIndex(QObject* obj, const QString& base) {
  if (!obj || base.isEmpty()) {
    return -1;
  }

  QObject* parent = effectiveParent(obj);
  if (!parent) {
    // Top-level objects aren't children of anything. For top-level QWindows
    // (first-class tree roots) disambiguate among same-segment top-level windows
    // so multiple windows get unique ids (Foo#1, Foo#2) instead of colliding on
    // a single bare "Foo".
    if (qobject_cast<QWindow*>(obj)) {
      auto* guiApp = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
      if (!guiApp) {
        return -1;
      }
      int sameSegmentCount = 0;
      int indexAmongSame = -1;
      const auto windows = guiApp->topLevelWindows();
      for (QWindow* w : windows) {
        if (baseIdSegment(w) == base) {
          if (w == obj) {
            indexAmongSame = sameSegmentCount;
          }
          sameSegmentCount++;
        }
      }
      if (sameSegmentCount <= 1) {
        return -1;
      }
      return indexAmongSame + 1;
    }
    // Other parentless objects: no disambiguation context.
    return -1;
  }

  int sameSegmentCount = 0;
  int indexAmongSame = -1;

  // One loop, one population, one numbering.
  //
  // An earlier version split this into a cheap "fast path" for visual-axis
  // children and a segment-comparing slow path for the rest. That was wrong three
  // separate ways, each of which put two objects on one ID:
  //
  //   - The two paths counted DIFFERENT populations of the same sibling list (the
  //     fast path skipped every sibling with a QObject parent), so they issued
  //     overlapping 1-based suffixes. A static child and a delegate under one
  //     parent both got `Rectangle#1`.
  //   - The fast path keyed on objectName while the segment itself comes from the
  //     QML `id` when one is declared -- `id: row` plus `objectName: "row" + index`
  //     made every sibling look unique and none got a suffix.
  //   - It compared class identity before objectName, so two differently-classed
  //     siblings sharing an objectName or a `text` never got compared at all.
  //
  // So the population is now every effective child, and the key is the generated
  // SEGMENT, which is the thing that actually has to be unique.
  //
  // Cost: O(siblings) per object, and baseIdSegment() resolves a QML id, so a
  // parent with a very large eagerly-instantiated child list (a Repeater over
  // thousands of rows) makes a full tree walk quadratic. The pre-filter below
  // removes most of it, and a recycling ListView only ever instantiates its
  // visible delegates. Correctness first: the previous shortcut bought speed by
  // handing out duplicate IDs, which is the one thing an ID must never do.
  const QMetaObject* objMeta = obj->metaObject();
  const bool baseFromText = base.startsWith(QLatin1String("text_"));

  const QList<QObject*> siblings = effectiveChildren(parent);
  for (QObject* sibling : siblings) {
    if (!sibling) {
      continue;
    }
    if (sibling == obj) {
      indexAmongSame = sameSegmentCount;
      ++sameSegmentCount;
      continue;
    }

    // Skip only what provably cannot produce `base`. A sibling matches if it is
    // the same class (compared by NAME -- QML installs per-component metaobjects,
    // so pointer identity is not a class test), or its objectName is the segment,
    // or our segment came from a `text` property (a QLabel and a QPushButton both
    // reading "OK" collide across classes), or it is a QQuickItem and so may carry
    // a declared QML id we cannot see without asking.
    const bool sameClass = qstrcmp(sibling->metaObject()->className(), objMeta->className()) == 0;
    const bool mayCarryQmlId = isQmlItem(sibling);
    if (!sameClass && !baseFromText && !mayCarryQmlId && sibling->objectName() != base) {
      continue;
    }

    if (baseIdSegment(sibling) == base) {
      ++sameSegmentCount;
    }
  }

  // Unique already: no suffix, so existing IDs for unambiguous objects are
  // unchanged.
  if (sameSegmentCount <= 1) {
    return -1;
  }

  // 1-based for human readability.
  return indexAmongSame + 1;
}

/// @brief Get all top-level objects (those without parents).
/// Uses QCoreApplication's children and other known roots.
QList<QObject*> getTopLevelObjects() {
  QList<QObject*> result;

  QCoreApplication* app = QCoreApplication::instance();
  if (app) {
    // Include the application object itself as a search root.
    // generateObjectId() walks up to QCoreApplication, so IDs start
    // with the app's segment (e.g., "QApplication/..."). The search
    // must begin from the app to match that first segment.
    result.append(app);
  }

  // Top-level windows (e.g. a QQuickWindow for a pure Qt Quick app) have
  // parent()==nullptr and are NOT QObject children of the application, so the
  // parent-based tree walk rooted at the app never reaches them. Add them as
  // additional roots so qt.objects.tree surfaces QML scenes, and so
  // findByObjectId can resolve window-rooted IDs.
  if (auto* guiApp = qobject_cast<QGuiApplication*>(app)) {
    const auto topWindows = guiApp->topLevelWindows();
    for (QWindow* w : topWindows) {
      // Skip hidden/offscreen windows — notably a QQuickWidget's internal render
      // surface (a non-shown QQuickWindow that is reachable through the widget
      // hierarchy already) and never-shown transient/popup surfaces.
      if (!w->isVisible())
        continue;
      // Skip the internal backing window of a top-level QWidget: those belong
      // to the Widgets object graph, not a standalone window root.
      if (w->inherits("QWidgetWindow"))
        continue;
      result.append(w);
    }
  }

  return result;
}

/// @brief Match a single ID segment against an object.
///
/// A segment matches iff it equals the segment generateIdSegment() would emit
/// for this object. Delegating to the generator keeps the forward (id creation)
/// and reverse (id resolution) paths in lockstep — including the QML id priority
/// and the stripped short type name — so they cannot drift. The previous
/// hand-rolled matcher never checked qmlId and compared against the full
/// className (e.g. "QQuickRectangle"), so it could never match a QML segment
/// (a qmlId, or a stripped "Rectangle"/"Rectangle#2") that the generator emits.
bool matchesSegment(QObject* obj, const QString& segment) {
  if (!obj) {
    return false;
  }
  return generateIdSegment(obj) == segment;
}

/// @brief Find object by path segments starting from a list of candidates.
QObject* findBySegments(const QStringList& segments, int segmentIndex,
                        const QList<QObject*>& candidates) {
  // Bounded like every other walk over the merged parent/child axis. The recursion
  // depth here is set by the SEGMENT COUNT of a client-supplied id, so without this
  // an id of "a/a/a/..." repeated tens of thousands of times drives that many stack
  // frames -- each holding a QList returned by value -- on the host application's
  // main thread, whose stack is 1 MB on iOS rather than the 8 MB this code used to
  // be confined to. A path longer than the deepest walkable tree cannot name a
  // reachable object anyway.
  if (segmentIndex >= segments.size() || segmentIndex > kMaxEffectiveDepth) {
    return nullptr;
  }

  const QString& segment = segments.at(segmentIndex);
  bool isLastSegment = (segmentIndex == segments.size() - 1);

  for (QObject* obj : candidates) {
    if (matchesSegment(obj, segment)) {
      if (isLastSegment) {
        return obj;
      }
      // Continue searching in children
      QObject* found = findBySegments(segments, segmentIndex + 1, effectiveChildren(obj));
      if (found) {
        return found;
      }
    }
  }

  return nullptr;
}

/// @brief Serialize object tree recursively.
QJsonObject serializeTreeRecursive(QObject* obj, int maxDepth, int currentDepth) {
  QJsonObject result = serializeObjectInfo(obj);

  // Check depth limit. A negative maxDepth means "no client-imposed limit", NOT
  // "unbounded" -- kMaxEffectiveDepth still applies, because the effective
  // hierarchy is not guaranteed acyclic and this recursion runs on the host
  // application's stack.
  if (currentDepth >= kMaxEffectiveDepth) {
    return result;
  }
  if (maxDepth >= 0 && currentDepth >= maxDepth) {
    return result;
  }

  // Add children
  QList<QObject*> children = effectiveChildren(obj);
  if (!children.isEmpty()) {
    QJsonArray childArray;
    for (QObject* child : children) {
      childArray.append(serializeTreeRecursive(child, maxDepth, currentDepth + 1));
    }
    result[QLatin1String("children")] = childArray;
  }

  return result;
}

}  // namespace

QString generateIdSegment(QObject* obj) {
  if (!obj) {
    return QString();
  }

  const QString base = baseIdSegment(obj);
  const int siblingIndex = getSiblingIndex(obj, base);

  if (siblingIndex > 0) {
    return base + QLatin1Char('#') + QString::number(siblingIndex);
  }

  return base;
}

QObject* effectiveParent(QObject* obj) {
  if (!obj) {
    return nullptr;
  }
  if (QObject* parent = obj->parent()) {
    return parent;
  }
#ifdef QTPILOT_HAS_QML
  // A QML item created by a Repeater or ListView delegate has a VISUAL parent
  // but no QObject parent -- the engine owns it, not the item above it. Walking
  // QObject parents alone therefore stops dead at every delegate, so the whole
  // subtree under one is unreachable: no ID path, no tree entry, and nothing
  // for findByObjectId() to match. In a Qt Quick app that is usually the
  // navigation, the tab strips and the list rows -- the controls most worth
  // driving. Falling back to the visual parent puts them back on the path.
  if (auto* item = qobject_cast<QQuickItem*>(obj)) {
    return item->parentItem();
  }
#endif
  return nullptr;
}

QList<QObject*> effectiveChildren(QObject* obj) {
  if (!obj) {
    return {};
  }
  QList<QObject*> children = obj->children();
#ifdef QTPILOT_HAS_QML
  // The exact inverse of effectiveParent(), and it has to stay that way: a
  // child is listed here by whichever object effectiveParent() names as its
  // parent, so an ID generated by walking up always matches a traversal
  // walking down. Visual children that DO have a QObject parent are skipped --
  // they are already listed under that parent, and listing them twice would
  // put one object at two different paths.
  if (auto* item = qobject_cast<QQuickItem*>(obj)) {
    const QList<QQuickItem*> visualChildren = item->childItems();
    for (QQuickItem* child : visualChildren) {
      if (child && child->parent() == nullptr) {
        children.append(child);
      }
    }
  }
#endif
  return children;
}

QString generateObjectId(QObject* obj) {
  if (!obj) {
    return QString();
  }

  // Build path from root to object
  QStringList segments;
  QObject* current = obj;

  int depth = 0;
  while (current) {
    if (++depth > kMaxEffectiveDepth) {
      // Only reachable on a parent graph with a cycle across the two axes (see
      // kMaxEffectiveDepth). Warn once per call and return the truncated path
      // rather than looping until the host app is out of memory.
      qWarning(
          "[qtPilot] object id path exceeded %d levels for a %s; the parent "
          "hierarchy is cyclic. Returning a truncated id.",
          kMaxEffectiveDepth, obj->metaObject()->className());
      break;
    }
    segments.prepend(generateIdSegment(current));
    current = effectiveParent(current);
  }

  return segments.join(QLatin1Char('/'));
}

QObject* findByObjectId(const QString& id, QObject* root) {
  if (id.isEmpty()) {
    return nullptr;
  }

  QStringList segments = id.split(QLatin1Char('/'), Qt::SkipEmptyParts);
  if (segments.isEmpty()) {
    return nullptr;
  }

  QList<QObject*> searchRoots;
  if (root) {
    searchRoots.append(root);
  } else {
    searchRoots = getTopLevelObjects();
  }

  return findBySegments(segments, 0, searchRoots);
}

QJsonObject serializeObjectInfo(QObject* obj) {
  QJsonObject result;

  if (!obj) {
    return result;
  }

  result[QLatin1String("id")] = ObjectRegistry::instance()->objectId(obj);
  result[QLatin1String("className")] = QString::fromLatin1(obj->metaObject()->className());

  QString objectName = obj->objectName();
  if (!objectName.isEmpty()) {
    result[QLatin1String("objectName")] = objectName;
  }

  // Widget-specific properties
  QWidget* widget = qobject_cast<QWidget*>(obj);
  if (widget) {
    result[QLatin1String("visible")] = widget->isVisible();

    QJsonObject geometry;
    QRect geom = widget->geometry();
    geometry[QLatin1String("x")] = geom.x();
    geometry[QLatin1String("y")] = geom.y();
    geometry[QLatin1String("width")] = geom.width();
    geometry[QLatin1String("height")] = geom.height();
    result[QLatin1String("geometry")] = geometry;
  }

  // Include text property if present
  QString text = getTextProperty(obj);
  if (!text.isEmpty()) {
    result[QLatin1String("text")] = text;
  }

#ifdef QTPILOT_HAS_QML
  // QML-specific metadata
  QmlItemInfo qmlInfo = inspectQmlItem(obj);
  if (qmlInfo.isQmlItem) {
    result[QLatin1String("isQmlItem")] = true;
    if (!qmlInfo.qmlId.isEmpty()) {
      result[QLatin1String("qmlId")] = qmlInfo.qmlId;
    }
    if (!qmlInfo.qmlFile.isEmpty()) {
      result[QLatin1String("qmlFile")] = qmlInfo.qmlFile;
    }
    result[QLatin1String("qmlTypeName")] = qmlInfo.shortTypeName;
  }
#endif

  return result;
}

QJsonObject serializeObjectTree(QObject* root, int maxDepth) {
  if (!root) {
    // Serialize all top-level objects
    QJsonObject result;
    result[QLatin1String("id")] = QString();
    result[QLatin1String("className")] = QStringLiteral("Root");

    QList<QObject*> topLevel = getTopLevelObjects();
    if (!topLevel.isEmpty()) {
      QJsonArray childArray;
      for (QObject* obj : topLevel) {
        childArray.append(serializeTreeRecursive(obj, maxDepth, 0));
      }
      result[QLatin1String("children")] = childArray;
    }

    return result;
  }

  return serializeTreeRecursive(root, maxDepth, 0);
}

}  // namespace qtPilot
