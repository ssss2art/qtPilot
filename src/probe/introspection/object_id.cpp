// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "introspection/object_id.h"

#include "core/object_registry.h"
#include "introspection/qml_inspector.h"

#include <ranges>

#include <QApplication>
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
/// Takes first 20 characters, replaces each run of non-alphanumerics with one
/// underscore ("Save & Close" -> "Save_Close", not "Save___Close").
QString sanitizeForId(const QString& input) {
  QString result;
  result.reserve(qMin(input.length(), 20));

  for (int i = 0; i < qMin(input.length(), 20); ++i) {
    QChar ch = input.at(i);
    if (ch.isLetterOrNumber()) {
      result.append(ch);
    } else if (!result.endsWith(QLatin1Char('_'))) {
      result.append(QLatin1Char('_'));
    }
  }

  // Trim trailing underscores
  while (result.endsWith(QLatin1Char('_')) && result.length() > 1) {
    result.chop(1);
  }

  return result;
}

/// Destructions on this thread, as reported by the registry's removal hook.
thread_local quint64 g_destroyedHere = 0;
/// Nesting depth of ID generation on this thread, and the destruction count
/// when the outermost generation began.
thread_local int g_generationDepth = 0;
thread_local quint64 g_generationStart = 0;

/// @brief Marks one ID generation (or resolution) as running on this thread.
///
/// Everything inside works on raw pointers it collected along the way. If any
/// object on this thread is destroyed meanwhile -- only application code can do
/// that, and the only application code ID generation runs is a `text` getter --
/// objectsDied() turns true and the walk must stop touching those pointers.
class GenerationWatch {
 public:
  GenerationWatch() {
    if (g_generationDepth++ == 0) {
      g_generationStart = g_destroyedHere;
    }
  }
  ~GenerationWatch() { --g_generationDepth; }
  GenerationWatch(const GenerationWatch&) = delete;
  GenerationWatch& operator=(const GenerationWatch&) = delete;
};

bool objectsDied() {
  return g_generationDepth > 0 && g_destroyedHere != g_generationStart;
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
  // The getter may have destroyed obj; nothing below may touch it then.
  if (objectsDied()) {
    return QString();
  }
  // '&' marks a mnemonic, and a tab a shortcut column, only where Qt draws them
  // so: actions and buttons. Elsewhere -- a QLabel reading "R&D", a line edit
  // holding a tab -- they are content, and dropping them makes siblings collide.
  if (obj->inherits("QAction") || obj->inherits("QAbstractButton")) {
    text = normalizeLabel(text);
  }
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

/// @brief Where an object's segment lands among its effective siblings.
struct SiblingSlot {
  /// 1-based `#N` suffix, or -1 when the base segment is already unique.
  int index = -1;
  /// The base came from a QML id that siblings share, and this object's
  /// objectName is unique among them, so the objectName is the segment instead.
  bool useObjectName = false;
};

/// @brief The objectName a shared QML id could yield to, or empty if none.
///
/// Only objectName sits below a QML id in baseIdSegment()'s priorities, so a
/// non-empty name that differs from the base means the base came from the id.
/// A name containing '#' is refused: it could spell another sibling's `id#N`.
QString promotableObjectName(QObject* obj, const QString& base) {
  QString name = obj->objectName();
  if (name.isEmpty() || name == base || name.contains(QLatin1Char('#'))) {
    return QString();
  }
  return name;
}

/// @brief Per-parent sibling-suffix memo, live only inside an IdGenerationScope.
///
/// Nesting is reference-counted so an inner scope shares the outer cache; only the
/// outermost release clears it. thread_local because ID generation belongs to the
/// thread that owns the objects, and a scope must never leak across threads.
struct SiblingIndexCache {
  int depth = 0;
  QHash<QObject*, QHash<QObject*, SiblingSlot>> byParent;
};

thread_local SiblingIndexCache* g_siblingCache = nullptr;

/// @brief Bucket a parent's effective children by segment and assign every one its
/// suffix, in a single pass.
///
/// This is where the quadratic goes away: the sibling question is answered once per
/// parent rather than once per child. Enumeration order and equality match the
/// direct scan exactly, so the suffixes are the same ones the unscoped path would
/// hand out.
QHash<QObject*, SiblingSlot> buildSiblingIndices(QObject* parent) {
  const QList<QObject*> children = effectiveChildren(parent);

  QHash<QString, QList<QObject*>> bySegment;
  bySegment.reserve(children.size());
  QHash<QString, int> nameCounts;
  for (QObject* child : children) {
    if (!child) {
      continue;
    }
    bySegment[baseIdSegment(child)].append(child);
    if (objectsDied()) {
      return {};  // the remaining children may be dead
    }
    const QString name = child->objectName();
    if (!name.isEmpty()) {
      ++nameCounts[name];
    }
  }

  QHash<QObject*, SiblingSlot> indices;
  indices.reserve(children.size());
  for (auto it = bySegment.constBegin(); it != bySegment.constEnd(); ++it) {
    const QList<QObject*>& group = it.value();
    if (group.size() <= 1) {
      // Unique segment: no suffix, so ids for unambiguous objects are unchanged.
      for (QObject* member : group) {
        indices.insert(member, SiblingSlot{});
      }
      continue;
    }
    int position = 0;
    for (QObject* member : group) {
      SiblingSlot slot;
      slot.index = ++position;  // 1-based, for human readability
      // The same test as the direct scan: no sibling shares the name, and none
      // emits it as its own base segment.
      const QString name = promotableObjectName(member, it.key());
      slot.useObjectName =
          !name.isEmpty() && nameCounts.value(name) == 1 && !bySegment.contains(name);
      indices.insert(member, slot);
    }
  }
  return indices;
}

/// @brief Position of this object among the effective siblings that would emit
/// the same base segment (index -1 when the base segment is already unique), and
/// whether a shared QML id yields to the object's unique objectName.
///
/// A QML id is per-declaration, so every delegate of a Repeater shares it and
/// the position is all that tells them apart: `row#2` says nothing about which
/// row it is. When the object's objectName is unique among its siblings and no
/// sibling emits it as a segment, the objectName is the better segment. Members
/// that cannot take their name keep the position they always had.
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
SiblingSlot placeAmongSiblings(QObject* obj, const QString& base) {
  if (!obj || base.isEmpty()) {
    return {};
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
        return {};
      }
      int sameSegmentCount = 0;
      int indexAmongSame = -1;
      const auto windows = guiApp->topLevelWindows();
      for (QWindow* w : windows) {
        const QString windowBase = baseIdSegment(w);
        if (objectsDied()) {
          return {};
        }
        if (windowBase == base) {
          if (w == obj) {
            indexAmongSame = sameSegmentCount;
          }
          sameSegmentCount++;
        }
      }
      if (sameSegmentCount <= 1) {
        return {};
      }
      return SiblingSlot{indexAmongSame + 1, false};
    }
    // Other parentless objects: no disambiguation context.
    return {};
  }

  // Inside a traversal scope, the whole group was (or can be) settled in one pass.
  // A miss falls through to the direct scan below rather than guessing -- an object
  // created after its parent's group was cached (QQmlContext::nameForObject can
  // construct one) is simply not in the map, and must still get a correct answer.
  if (g_siblingCache != nullptr) {
    auto parentIt = g_siblingCache->byParent.constFind(parent);
    if (parentIt == g_siblingCache->byParent.constEnd()) {
      QHash<QObject*, SiblingSlot> indices = buildSiblingIndices(parent);
      if (objectsDied()) {
        return {};  // incomplete: must not be cached, and obj may be gone
      }
      parentIt = g_siblingCache->byParent.insert(parent, indices);
    }
    const auto childIt = parentIt->constFind(obj);
    if (childIt != parentIt->constEnd()) {
      return childIt.value();
    }
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
  const QString name = promotableObjectName(obj, base);
  // Whether a sibling shares `name` or emits it as its segment. Tracked in the
  // same pass; it only matters if the base turns out to collide.
  bool nameTaken = name.isEmpty();

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
    const QString siblingName = sibling->objectName();
    if (!nameTaken && siblingName == name) {
      nameTaken = true;
    }
    // Could the sibling's base be `name`? Only through its objectName (just
    // checked), a QML id, a `text` property, or its class name.
    const bool mayEmitName =
        !nameTaken && (mayCarryQmlId || name.startsWith(QLatin1String("text_")) ||
                       name == QLatin1String(sibling->metaObject()->className()));
    if (!sameClass && !baseFromText && !mayCarryQmlId && siblingName != base && !mayEmitName) {
      continue;
    }

    const QString siblingBase = baseIdSegment(sibling);
    if (objectsDied()) {
      return {};
    }
    if (siblingBase == base) {
      ++sameSegmentCount;
    }
    if (siblingBase == name) {
      nameTaken = true;
    }
  }

  // Unique already: no suffix, so existing IDs for unambiguous objects are
  // unchanged.
  if (sameSegmentCount <= 1) {
    return {};
  }

  // 1-based for human readability.
  return SiblingSlot{indexAmongSame + 1, !nameTaken};
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
    for (QWindow* w : guiApp->topLevelWindows() | std::views::filter([](QWindow* w) {
                        // Skip hidden/offscreen windows and internal backing windows
                        return w && w->isVisible() && !w->inherits("QWidgetWindow");
                      })) {
      result.append(w);
    }
  }

  // Top-level WIDGETS need the same treatment, and for the same reason. A
  // parentless QWidget — the ordinary shape of a main window — is not a QObject
  // child of the application either, so the app-rooted walk never reaches it or
  // anything beneath it. The QWidgetWindow skip above assumes those widgets are
  // reachable "through the Widgets object graph", but nothing ever roots that
  // graph, so in practice a widgets application exposes almost nothing to
  // qt.objects.search: its window, menus and actions are all invisible while a
  // QML scene in the same process is fully visible.
  //
  // Unlike the window loop this does not filter on visibility: a hidden dialog
  // is still a legitimate search target, and there is no offscreen-duplicate
  // problem here of the kind QQuickWidget creates.
  if (qobject_cast<QApplication*>(app)) {
    for (QWidget* w : QApplication::topLevelWidgets() |
                          std::views::filter([&](QWidget* w) { return !result.contains(w); })) {
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
    const bool matched = matchesSegment(obj, segment);
    // The remaining candidates, and obj itself, may be dead.
    if (objectsDied()) {
      return nullptr;
    }
    if (matched) {
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

void noteObjectDestroyed() {
  ++g_destroyedHere;
}

QString generateIdSegment(QObject* obj) {
  if (!obj) {
    return QString();
  }
  GenerationWatch watch;

  const QString base = baseIdSegment(obj);
  if (objectsDied()) {
    return QString();
  }
  const SiblingSlot slot = placeAmongSiblings(obj, base);
  if (objectsDied()) {
    return QString();
  }

  if (slot.useObjectName) {
    return obj->objectName();
  }
  if (slot.index > 0) {
    return base + QLatin1Char('#') + QString::number(slot.index);
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

IdGenerationScope::IdGenerationScope() {
  static thread_local SiblingIndexCache cache;
  if (cache.depth++ == 0) {
    cache.byParent.clear();
    g_siblingCache = &cache;
  }
}

IdGenerationScope::~IdGenerationScope() {
  if (g_siblingCache != nullptr && --g_siblingCache->depth == 0) {
    // Drop the memo with the traversal. Holding it any longer would mean trusting
    // that the tree has not changed, which is only true within one walk.
    g_siblingCache->byParent.clear();
    g_siblingCache = nullptr;
  }
}

QString normalizeLabel(const QString& label) {
  const auto tab = label.indexOf(QLatin1Char('\t'));
  const QString shown = tab >= 0 ? label.left(tab) : label;

  QString result;
  result.reserve(shown.size());
  for (decltype(shown.size()) i = 0; i < shown.size(); ++i) {
    if (shown.at(i) != QLatin1Char('&')) {
      result.append(shown.at(i));
      continue;
    }
    // "&&" is an escaped, visible ampersand; a lone "&" only marks the mnemonic.
    if (i + 1 < shown.size() && shown.at(i + 1) == QLatin1Char('&')) {
      result.append(QLatin1Char('&'));
      ++i;
    }
  }
  return result;
}

QString generateObjectId(QObject* obj) {
  if (!obj) {
    return QString();
  }

  GenerationWatch watch;

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
    // A getter destroyed something: current, or an ancestor still to be walked.
    if (objectsDied()) {
      return QString();
    }
    current = effectiveParent(current);
  }

  return segments.join(QLatin1Char('/'));
}

std::expected<QObject*, QString> findByObjectIdExpected(const QString& id, QObject* root) {
  if (id.isEmpty()) {
    return std::unexpected(QStringLiteral("Object identifier is empty"));
  }

  // Resolution regenerates a segment for every candidate at every level, so it pays
  // the sibling scan just as often as generation does. One scope over the whole
  // lookup collapses that too.
  IdGenerationScope idScope;

  QStringList segments = id.split(QLatin1Char('/'), Qt::SkipEmptyParts);
  if (segments.isEmpty()) {
    return std::unexpected(QStringLiteral("Object identifier contains only slashes: %1").arg(id));
  }

  QList<QObject*> searchRoots;
  if (root) {
    searchRoots.append(root);
  } else {
    searchRoots = getTopLevelObjects();
  }

  GenerationWatch watch;
  QObject* found = findBySegments(segments, 0, searchRoots);
  if (objectsDied()) {
    return std::unexpected(
        QStringLiteral("Objects were destroyed while resolving %1; try again").arg(id));
  }
  if (!found) {
    return std::unexpected(QStringLiteral("Object not found by hierarchical path: %1").arg(id));
  }
  return found;
}

QObject* findByObjectId(const QString& id, QObject* root) {
  auto res = findByObjectIdExpected(id, root);
  return res.has_value() ? *res : nullptr;
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
  IdGenerationScope idScope;

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
