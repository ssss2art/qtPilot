// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "hit_test.h"

#include "core/object_registry.h"

#include <algorithm>
#include <ranges>
#include <stdexcept>

#include <QApplication>
#include <QGraphicsObject>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGuiApplication>
#include <QJsonArray>
#include <QWindow>

#ifdef QTPILOT_HAS_QML
#include <QQuickItem>
#include <QQuickWindow>
#endif

namespace qtPilot {

std::expected<QJsonObject, QString> HitTest::widgetGeometryExpected(QWidget* widget) {
  if (!widget) {
    return std::unexpected(QStringLiteral("widgetGeometryExpected: widget cannot be null"));
  }

  QJsonObject result;

  // Local geometry (relative to parent)
  QRect local = widget->geometry();
  result["local"] = QJsonObject{
      {"x", local.x()}, {"y", local.y()}, {"width", local.width()}, {"height", local.height()}};

  // Global geometry (screen coordinates)
  QPoint globalTopLeft = widget->mapToGlobal(QPoint(0, 0));
  result["global"] = QJsonObject{{"x", globalTopLeft.x()},
                                 {"y", globalTopLeft.y()},
                                 {"width", widget->width()},
                                 {"height", widget->height()}};

  // Device pixel ratio for high-DPI awareness
  result["devicePixelRatio"] = widget->devicePixelRatioF();

  return result;
}

QJsonObject HitTest::widgetGeometry(QWidget* widget) {
  auto res = widgetGeometryExpected(widget);
  if (!res) {
    throw std::invalid_argument(res.error().toStdString());
  }
  return *res;
}

QWidget* HitTest::widgetAt(const QPoint& globalPos) {
  return QApplication::widgetAt(globalPos);
}

QWidget* HitTest::childAt(QWidget* parent, const QPoint& localPos) {
  if (!parent) {
    throw std::invalid_argument("childAt: parent cannot be null");
  }

  QWidget* child = parent->childAt(localPos);
  return child ? child : parent;
}

QString HitTest::widgetIdAt(const QPoint& globalPos) {
  QWidget* widget = widgetAt(globalPos);
  if (!widget) {
    return QString();
  }

  // A QGraphicsView is one opaque widget to the widget tree, so stopping here
  // would report the viewport for every point in a plan and never the product
  // under the cursor. Carry on into the scene when the point is on the canvas.
  //
  // The canvas is the *viewport*, and the test is pointer identity, not a cast
  // of the parent: QAbstractScrollArea parents its scroll bars and corner
  // widget to the view itself, so a parent-cast would treat a click on the
  // scroll bar as a click on the canvas, map it to a point outside the viewport
  // and answer with whatever item that projects onto.
  if (QWidget* parent = widget->parentWidget()) {
    if (auto* view = qobject_cast<QGraphicsView*>(parent); view && view->viewport() == widget) {
      const QString itemId =
          graphicsItemIdAt(view, QPointF(view->viewport()->mapFromGlobal(globalPos)));
      if (!itemId.isEmpty()) {
        return itemId;
      }
      // Empty canvas: fall through and report the viewport, as before.
    }
  }

  // Use ObjectRegistry to get hierarchical ID
  return ObjectRegistry::instance()->objectId(widget);
}

namespace {

/// @brief JSON rect from a QRectF, as doubles.
///
/// Graphics coordinates are qreal and a scaled view makes fractional values the
/// norm, so these stay double throughout -- toInt() on them yields 0.
QJsonObject rectToJson(const QRectF& rect) {
  return QJsonObject{
      {"x", rect.x()}, {"y", rect.y()}, {"width", rect.width()}, {"height", rect.height()}};
}

/// @brief Nearest QGraphicsObject at or above @a item in the parent chain.
///
/// A QGraphicsItem is not a QObject, so it has no objectId. Decorations drawn
/// as bare items are common, and reporting a miss for one would be useless --
/// the addressable thing is the QGraphicsObject that owns it.
QGraphicsObject* nearestGraphicsObject(QGraphicsItem* item) {
  for (QGraphicsItem* cursor = item; cursor; cursor = cursor->parentItem()) {
    if (QGraphicsObject* object = cursor->toGraphicsObject()) {
      return object;
    }
  }
  return nullptr;
}

/// @brief Map @a sceneRect through @a view into viewport and global rects.
/// @param itemVisible The item's own visibility, folded into the entry's "visible"
QJsonObject viewEntry(QGraphicsView* view, const QRectF& sceneRect, bool itemVisible) {
  // viewportTransform() carries the view transform (scale, rotation, shear) and
  // the scroll offset in one matrix. That composite is precisely what a caller
  // used to have to recover by dragging a known distance and dividing.
  //
  // Not mapFromScene(QRectF): that returns a QPolygon, i.e. integer corners, so
  // a 40-unit item at 1.25x came back 51px wide instead of 50. Rounding twice
  // over is exactly the kind of drift the caller is being spared here.
  const QRectF viewportRect = view->viewportTransform().mapRect(sceneRect);

  // Take the QPointF overload where it exists. Rounding the origin while
  // width/height stay fractional would put the four fields of "global" in two
  // different precision domains, so global.x + global.width/2 would drift from
  // the viewport rect -- the very calibration error this API exists to remove.
  // itemGeometry() makes the same choice for the same reason.
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  const QPointF origin = view->viewport()->mapToGlobal(QPointF(0, 0));
#else
  const QPointF origin = QPointF(view->viewport()->mapToGlobal(QPoint(0, 0)));
#endif

  QJsonObject entry;
  entry["viewObjectId"] = ObjectRegistry::instance()->objectId(view);
  entry["viewport"] = rectToJson(viewportRect);
  entry["global"] = rectToJson(viewportRect.translated(origin));
  // Being scrolled out is not an error -- a caller may want to scroll to it --
  // but clicking the reported point would hit whatever is on screen there
  // instead, so say so. An item can also be hidden outright.
  //
  // Overlap is tested in QRectF space, not via QRect::intersects(): that is
  // false whenever *either* rect is empty, and a zero-width or zero-height item
  // is ordinary in a scene (a horizontal line with a cosmetic pen, a grouping
  // item that only holds children). Those would be called invisible wherever
  // they sat, and a caller would scroll to reveal something already on screen.
  const QRectF viewportBounds = QRectF(view->viewport()->rect());
  const bool overlaps = viewportRect.left() <= viewportBounds.right() &&
                        viewportRect.right() >= viewportBounds.left() &&
                        viewportRect.top() <= viewportBounds.bottom() &&
                        viewportRect.bottom() >= viewportBounds.top();
  entry["visible"] = itemVisible && view->isVisible() && overlaps;
  entry["devicePixelRatio"] = view->viewport()->devicePixelRatioF();
  return entry;
}

}  // namespace

QJsonObject HitTest::graphicsItemGeometry(QGraphicsObject* item, QGraphicsView* preferredView) {
  if (!item) {
    throw std::invalid_argument("graphicsItemGeometry: item cannot be null");
  }

  QJsonObject result;

  // boundingRect(), not childrenBoundingRect(): a scene item is often a group
  // whose clickable body is the item itself, with a label hanging outside it.
  const QRectF localRect = item->boundingRect();
  result["local"] = rectToJson(localRect);

  const QRectF sceneRect = item->mapToScene(localRect).boundingRect();
  result["scene"] = rectToJson(sceneRect);

  QJsonArray views;
  QJsonObject mirrored;
  bool haveMirror = false;

  QGraphicsScene* scene = item->scene();
  const QList<QGraphicsView*> sceneViews = scene ? scene->views() : QList<QGraphicsView*>();
  for (QGraphicsView* view : sceneViews) {
    if (!view) {
      continue;
    }
    const QJsonObject entry = viewEntry(view, sceneRect, item->isVisible());
    views.append(entry);

    // The top level mirrors the requested view, or the first one when the
    // caller did not name one. With several views at different scales there is
    // no defensible "the" answer, so the array is the real result.
    const bool isPreferred = preferredView ? (view == preferredView) : !haveMirror;
    if (isPreferred) {
      mirrored = entry;
      haveMirror = true;
    }
  }

  result["views"] = views;
  if (haveMirror) {
    // value(), not operator[]: the non-const overload detaches this shared copy
    // and would insert a null for any key that went missing.
    result["viewport"] = mirrored.value(QStringLiteral("viewport"));
    result["global"] = mirrored.value(QStringLiteral("global"));
    result["visible"] = mirrored.value(QStringLiteral("visible"));
    result["devicePixelRatio"] = mirrored.value(QStringLiteral("devicePixelRatio"));
  } else {
    // In a scene nobody renders (or a preferredView that does not render this
    // scene): local and scene still mean something, a screen position does not.
    result["viewport"] = QJsonValue::Null;
    result["global"] = QJsonValue::Null;
    result["visible"] = false;
    result["devicePixelRatio"] = 1.0;
  }

  return result;
}

QGraphicsObject* HitTest::graphicsItemAt(QGraphicsView* view, const QPointF& viewportPos) {
  if (!view) {
    throw std::invalid_argument("graphicsItemAt: view cannot be null");
  }

  // Outside the canvas is not a hit on the canvas. The view would gladly
  // project such a point into scene space and name an item for it, which is how
  // a click on a scroll bar or a frame could come back as a scene item.
  if (!QRectF(view->viewport()->rect()).contains(viewportPos)) {
    return nullptr;
  }

  // The whole stack at the point, not just itemAt()'s topmost one.
  //
  // Scene decorations -- grid lines, overlays, rubber bands -- are routinely
  // parentless bare QGraphicsItems drawn above the content. They are not
  // QObjects, so they have no id of their own, and walking their parent chain
  // rescues nothing because they are siblings of the content rather than its
  // children. Consulting only the topmost item would let any such decoration
  // swallow every addressable item beneath it. items() is documented to return
  // descending stacking order, so the first entry that resolves is the topmost
  // thing the caller can actually address.
  const QList<QGraphicsItem*> stack = view->items(viewportPos.toPoint());
  auto it = std::ranges::find_if(
      stack, [](QGraphicsItem* candidate) { return nearestGraphicsObject(candidate) != nullptr; });
  return it != stack.end() ? nearestGraphicsObject(*it) : nullptr;
}

QString HitTest::graphicsItemIdAt(QGraphicsView* view, const QPointF& viewportPos) {
  QGraphicsObject* object = graphicsItemAt(view, viewportPos);
  if (!object) {
    return QString();
  }
  return ObjectRegistry::instance()->objectId(object);
}

QJsonObject HitTest::windowGeometry(QWindow* window) {
  if (!window) {
    throw std::invalid_argument("windowGeometry: window cannot be null");
  }

  QJsonObject result;

  // A window's "local" rect is its own content area at the origin -- unlike a
  // widget, there is no parent to be relative to.
  result["local"] =
      QJsonObject{{"x", 0}, {"y", 0}, {"width", window->width()}, {"height", window->height()}};

  // geometry() is the *content* rect; frameGeometry() would include decoration.
  // Content matches widgetGeometry's mapToGlobal(QPoint(0,0)) origin.
  const QRect content = window->geometry();
  result["global"] = QJsonObject{{"x", content.x()},
                                 {"y", content.y()},
                                 {"width", content.width()},
                                 {"height", content.height()}};

  result["devicePixelRatio"] = window->devicePixelRatio();

  return result;
}

#ifdef QTPILOT_HAS_QML

namespace {

/// @brief Deepest visible+enabled descendant of @a parent containing @a parentPos.
///
/// Returns @a parent itself when no child matches. Positions are in @a parent's
/// coordinate space.
QQuickItem* deepestItemAt(QQuickItem* parent, const QPointF& parentPos) {
  // Paint order, not child order: Qt Quick stacks by z, then by document order.
  // childItems() is document order only, so an item with a raised z would be
  // missed. Sort a copy (stable, so equal z keeps document order) and walk it
  // backwards -- last painted is topmost.
  QList<QQuickItem*> children = parent->childItems();
  std::ranges::stable_sort(
      children, [](const QQuickItem* a, const QQuickItem* b) { return a->z() < b->z(); });

  for (QQuickItem* child : std::views::reverse(children)) {
    if (!child->isVisible() || !child->isEnabled()) {
      continue;
    }
    const QPointF childPos = parent->mapToItem(child, parentPos);
    const bool inside = child->contains(childPos);

    // A non-clipping item may render and receive input outside its own bounds,
    // so a containment miss must not prune the subtree -- only a clipping item
    // can do that. Zero-sized grouping Items are the common case: they fail
    // contains() for every point while their children are perfectly visible.
    if (!inside && child->clip()) {
      continue;
    }

    if (QQuickItem* hit = deepestItemAt(child, childPos)) {
      if (hit != child || inside) {
        return hit;
      }
    }
  }
  return parent->contains(parentPos) ? parent : nullptr;
}

}  // namespace

QJsonObject HitTest::itemGeometry(QQuickItem* item) {
  if (!item) {
    throw std::invalid_argument("itemGeometry: item cannot be null");
  }

  QJsonObject result;

  // Local: position within the parent item, as QML authors it.
  result["local"] = QJsonObject{
      {"x", item->x()}, {"y", item->y()}, {"width", item->width()}, {"height", item->height()}};

  // Scene: window-local coordinates. This is the space Qt Quick input events
  // use, so it is what a caller needs to synthesise a click on this item.
  const QRectF sceneRect = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
  result["scene"] = QJsonObject{{"x", sceneRect.x()},
                                {"y", sceneRect.y()},
                                {"width", sceneRect.width()},
                                {"height", sceneRect.height()}};

  QQuickWindow* window = item->window();
  if (window) {
    // Prefer the QPointF overload: rounding the origin while leaving
    // width/height fractional would put the four fields in two different
    // precision domains, so `global.x + global.width/2` would drift from the
    // scene rect reported above. Qt 5 has no such overload, so it rounds --
    // the sub-pixel skew is accepted there rather than dropping the whole
    // fractional report.
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QPointF globalTopLeft = window->mapToGlobal(sceneRect.topLeft());
#else
    const QPointF globalTopLeft = QPointF(window->mapToGlobal(sceneRect.topLeft().toPoint()));
#endif
    result["global"] = QJsonObject{{"x", globalTopLeft.x()},
                                   {"y", globalTopLeft.y()},
                                   {"width", sceneRect.width()},
                                   {"height", sceneRect.height()}};
    result["devicePixelRatio"] = window->devicePixelRatio();
  } else {
    // Not rendered yet: scene coords exist but map to no screen position.
    // Report null rather than inventing one at the origin.
    result["global"] = QJsonValue::Null;
    result["devicePixelRatio"] = 1.0;
  }

  return result;
}

QQuickItem* HitTest::itemAt(QQuickWindow* window, const QPointF& scenePos) {
  if (!window) {
    throw std::invalid_argument("itemAt: window cannot be null");
  }

  QQuickItem* root = window->contentItem();
  if (!root) {
    return nullptr;
  }

  const QPointF rootPos = root->mapFromScene(scenePos);
  if (!root->contains(rootPos)) {
    // Outside the scene entirely. Report a miss rather than the content item,
    // so callers can distinguish "nothing here" from "the root is here".
    return nullptr;
  }
  return deepestItemAt(root, rootPos);
}

QString HitTest::quickItemIdAt(const QPoint& globalPos) {
  // QGuiApplication::topLevelWindows() is registration order, NOT stacking
  // order -- raising a window does not reorder it. So check the focused window
  // first (the only stacking signal Qt gives us portably here), then fall back
  // to a reverse scan, which approximates "most recently created first".
  QList<QWindow*> candidates;
  QWindow* const focus = QGuiApplication::focusWindow();
  const QList<QWindow*> all = QGuiApplication::topLevelWindows();
  candidates.reserve(all.size());
  if (focus) {
    candidates.append(focus);
  }
  for (QWindow* w :
       all | std::views::reverse | std::views::filter([&](QWindow* w) { return w != focus; })) {
    candidates.append(w);
  }

  for (QWindow* candidate : candidates) {
    auto* quickWindow = qobject_cast<QQuickWindow*>(candidate);
    if (!quickWindow || !quickWindow->isVisible()) {
      continue;
    }
    if (!quickWindow->geometry().contains(globalPos)) {
      continue;
    }
    const QPointF scenePos = quickWindow->mapFromGlobal(globalPos);
    if (QQuickItem* item = itemAt(quickWindow, scenePos)) {
      return ObjectRegistry::instance()->objectId(item);
    }
    return ObjectRegistry::instance()->objectId(quickWindow);
  }
  return QString();
}

#endif  // QTPILOT_HAS_QML

}  // namespace qtPilot
