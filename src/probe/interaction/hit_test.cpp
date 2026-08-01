// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "hit_test.h"

#include "core/object_registry.h"

#include <algorithm>
#include <stdexcept>

#include <QApplication>
#include <QGuiApplication>
#include <QWindow>

#ifdef QTPILOT_HAS_QML
#include <QQuickItem>
#include <QQuickWindow>
#endif

namespace qtPilot {

QJsonObject HitTest::widgetGeometry(QWidget* widget) {
  if (!widget) {
    throw std::invalid_argument("widgetGeometry: widget cannot be null");
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

  // Use ObjectRegistry to get hierarchical ID
  return ObjectRegistry::instance()->objectId(widget);
}

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
  std::stable_sort(children.begin(), children.end(),
                   [](const QQuickItem* a, const QQuickItem* b) { return a->z() < b->z(); });

  for (auto it = children.crbegin(); it != children.crend(); ++it) {
    QQuickItem* child = *it;
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
    // Map the QPointF overload, not the rounded QPoint one: rounding the origin
    // while leaving width/height fractional would put the four fields in two
    // different precision domains, so `global.x + global.width/2` would drift
    // from the scene rect reported above.
    const QPointF globalTopLeft = window->mapToGlobal(sceneRect.topLeft());
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
  if (QWindow* focus = QGuiApplication::focusWindow()) {
    candidates.append(focus);
  }
  const QList<QWindow*> all = QGuiApplication::topLevelWindows();
  for (auto it = all.crbegin(); it != all.crend(); ++it) {
    if (!candidates.contains(*it)) {
      candidates.append(*it);
    }
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
