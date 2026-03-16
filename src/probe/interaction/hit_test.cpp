// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "hit_test.h"

#include "core/object_registry.h"

#include <stdexcept>

#include <QApplication>

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

}  // namespace qtPilot
