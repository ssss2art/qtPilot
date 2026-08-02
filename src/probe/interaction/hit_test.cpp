// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "hit_test.h"

#include "core/object_registry.h"

#include <functional>
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

QJsonObject HitTest::windowGeometry(QWindow* window) {
  if (!window) {
    throw std::invalid_argument("windowGeometry: window cannot be null");
  }

  QJsonObject result;

  // A window's "local" rect is its own content area at the origin -- unlike a
  // widget, there is no parent to be relative to.
  result["local"] =
      QJsonObject{{"x", 0}, {"y", 0}, {"width", window->width()}, {"height", window->height()}};

  const QRect frame = window->geometry();
  result["global"] = QJsonObject{
      {"x", frame.x()}, {"y", frame.y()}, {"width", frame.width()}, {"height", frame.height()}};

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
    const QPoint globalTopLeft = window->mapToGlobal(sceneRect.topLeft().toPoint());
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

  // Depth-first, last-child-first: later siblings paint on top, so the last
  // match in child order is the one visually under the point.
  std::function<QQuickItem*(QQuickItem*, const QPointF&)> deepest =
      [&](QQuickItem* parent, const QPointF& parentPos) -> QQuickItem* {
    const QList<QQuickItem*> children = parent->childItems();
    for (auto it = children.crbegin(); it != children.crend(); ++it) {
      QQuickItem* child = *it;
      if (!child->isVisible() || !child->isEnabled()) {
        continue;
      }
      const QPointF childPos = parent->mapToItem(child, parentPos);
      if (!child->contains(childPos)) {
        continue;
      }
      if (QQuickItem* hit = deepest(child, childPos)) {
        return hit;
      }
      return child;
    }
    return nullptr;
  };

  const QPointF rootPos = root->mapFromScene(scenePos);
  QQuickItem* hit = deepest(root, rootPos);
  return hit ? hit : root;
}

QString HitTest::quickItemIdAt(const QPoint& globalPos) {
  const QList<QWindow*> windows = QGuiApplication::topLevelWindows();
  // Reverse order: the most recently raised window is checked first.
  for (auto it = windows.crbegin(); it != windows.crend(); ++it) {
    auto* quickWindow = qobject_cast<QQuickWindow*>(*it);
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
