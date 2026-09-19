// Copyright (c) 2024-2026 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "interaction/item_targeting.h"

#include "api/error_codes.h"
#include "core/object_registry.h"
#include "transport/jsonrpc_handler.h"

#include <typeinfo>

#include <QGraphicsItem>
#include <QGraphicsObject>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QJsonValue>
#include <QList>
#include <QPointF>
#include <QRect>
#include <QWidget>

namespace qtPilot {

namespace {

/// The number of samples per axis in the fallback lattice. Nine is fine enough
/// to land on a border band a tenth of the item wide, and bounds the cost of a
/// miss at 1 + 9*9 hit-tests.
constexpr int kLatticeSteps = 9;

/// @brief Where a click aimed at @p item should land, and whether it moved.
struct ItemTarget {
  QPoint viewportPoint;   ///< Point in the view's viewport coordinates.
  bool adjusted = false;  ///< True when the preferred point was unusable.
};

/// @brief The topmost item at @p viewportPoint, or nullptr.
QGraphicsItem* topItemAt(QGraphicsView* view, const QPoint& viewportPoint) {
  return view->itemAt(viewportPoint);
}

/// @brief Whether a press at that point would reach @p item.
///
/// A child counts: pressing a label that belongs to the item is still pressing
/// the item, and callers address the parent.
bool reaches(QGraphicsItem* top, QGraphicsObject* item) {
  for (QGraphicsItem* cursor = top; cursor; cursor = cursor->parentItem()) {
    if (cursor == item) {
      return true;
    }
  }
  return false;
}

/// @brief Name the topmost item at a point, for an error a caller can act on.
QString describeOccluder(QGraphicsView* view, const QPoint& viewportPoint) {
  QGraphicsItem* top = topItemAt(view, viewportPoint);
  if (!top) {
    return QStringLiteral("<nothing>");
  }
  if (auto* asObject = dynamic_cast<QGraphicsObject*>(top)) {
    const QString id = ObjectRegistry::instance()->objectId(asObject);
    if (!id.isEmpty()) {
      return id;
    }
  }
  return QStringLiteral("<a %1>").arg(QString::fromUtf8(typeid(*top).name()));
}

/// @brief Candidate points inside @p item, preferred first.
///
/// The centre of the bounding rect is what a caller means by "click it", so it
/// is tried first. It is not always usable: something can be drawn over it, and
/// an item's own shape() can exclude it -- an outline whose interior is
/// click-through has no centre to press. The rest of the rect is then sampled
/// so those items stay addressable instead of becoming unreachable.
QList<QPointF> candidateLocalPoints(QGraphicsObject* item) {
  const QRectF rect = item->boundingRect();
  QList<QPointF> points;
  points.append(rect.center());

  for (int row = 0; row < kLatticeSteps; ++row) {
    for (int col = 0; col < kLatticeSteps; ++col) {
      const qreal fx = (col + 0.5) / kLatticeSteps;
      const qreal fy = (row + 0.5) / kLatticeSteps;
      points.append(QPointF(rect.left() + rect.width() * fx, rect.top() + rect.height() * fy));
    }
  }
  return points;
}

/// @brief Resolve where to click @p item, or refuse.
///
/// @throws JsonRpcException kItemOccluded when no point on the item would
///         receive the press -- naming what is in the way. Refusing is the
///         point: dispatching at a coordinate that belongs to something else
///         presses the wrong object while reporting success, and the caller's
///         assertion then measures the wrong object and passes.

/// @brief Read a numeric coordinate out of a position object.
double requireCoordinate(const QJsonObject& params, const QString& key, const QString& methodName) {
  const QJsonValue raw = params.value(key);
  if (!raw.isDouble()) {
    throw JsonRpcException(JsonRpcError::kInvalidParams,
                           QStringLiteral("Parameter '%1' must be a number").arg(key),
                           QJsonObject{{QStringLiteral("method"), methodName}, {key, raw}});
  }
  return raw.toDouble();
}

}  // namespace

ItemTargeting::Target ItemTargeting::resolve(QGraphicsObject* item, QGraphicsView* view,
                                             const QJsonObject& params, const QString& methodName) {
  const QJsonValue rawPosition = params.value(QStringLiteral("position"));
  const bool callerChosePoint = !rawPosition.isUndefined() && !rawPosition.isNull();

  if (callerChosePoint) {
    if (!rawPosition.isObject()) {
      throw JsonRpcException(
          JsonRpcError::kInvalidParams,
          QStringLiteral("Parameter 'position' must be an object with numeric x/y fields"),
          QJsonObject{{QStringLiteral("method"), methodName},
                      {QStringLiteral("position"), rawPosition}});
    }
    const QJsonObject position = rawPosition.toObject();
    const QPointF localPoint(requireCoordinate(position, QStringLiteral("x"), methodName),
                             requireCoordinate(position, QStringLiteral("y"), methodName));
    const QPoint viewportPoint = view->mapFromScene(item->mapToScene(localPoint));

    // The caller named this exact point. Sliding off it quietly would answer a
    // question they did not ask, so an obstructed point is refused instead.
    if (!reaches(topItemAt(view, viewportPoint), item)) {
      throw JsonRpcException(
          ErrorCode::kItemOccluded,
          QStringLiteral("The requested position does not belong to this item"),
          QJsonObject{{QStringLiteral("method"), methodName},
                      {QStringLiteral("objectId"), ObjectRegistry::instance()->objectId(item)},
                      {QStringLiteral("occludedBy"), describeOccluder(view, viewportPoint)},
                      {QStringLiteral("x"), viewportPoint.x()},
                      {QStringLiteral("y"), viewportPoint.y()}});
    }
    return Target{viewportPoint, false};
  }

  const QRect viewportRect(QPoint(0, 0), view->viewport()->size());
  const QList<QPointF> candidates = candidateLocalPoints(item);
  const QPoint centre = view->mapFromScene(item->mapToScene(item->boundingRect().center()));
  bool first = true;
  bool anyPointOnScreen = false;
  for (const QPointF& localPoint : candidates) {
    const QPoint viewportPoint = view->mapFromScene(item->mapToScene(localPoint));
    const bool wasFirst = first;
    first = false;
    if (!viewportRect.contains(viewportPoint)) {
      continue;
    }
    anyPointOnScreen = true;
    if (reaches(topItemAt(view, viewportPoint), item)) {
      return Target{viewportPoint, !wasFirst};
    }
  }

  // Scrolled or panned out of sight is a different problem from covered up, and
  // the caller does something different about each: scroll to it, or deal with
  // what is on top of it.
  if (!anyPointOnScreen) {
    throw JsonRpcException(
        ErrorCode::kCoordinateOutOfBounds,
        QStringLiteral("Graphics item click point is outside the view viewport"),
        QJsonObject{{QStringLiteral("method"), methodName},
                    {QStringLiteral("objectId"), ObjectRegistry::instance()->objectId(item)},
                    {QStringLiteral("viewObjectId"), ObjectRegistry::instance()->objectId(view)},
                    {QStringLiteral("x"), centre.x()},
                    {QStringLiteral("y"), centre.y()}});
  }

  throw JsonRpcException(
      ErrorCode::kItemOccluded,
      QStringLiteral("No point on this item would receive the event; something is drawn over it, "
                     "or its shape() excludes the points tried"),
      QJsonObject{{QStringLiteral("method"), methodName},
                  {QStringLiteral("objectId"), ObjectRegistry::instance()->objectId(item)},
                  {QStringLiteral("occludedBy"), describeOccluder(view, centre)},
                  {QStringLiteral("viewObjectId"), ObjectRegistry::instance()->objectId(view)}});
}

int ItemTargeting::candidatePointCount() {
  return 1 + kLatticeSteps * kLatticeSteps;
}

}  // namespace qtPilot
