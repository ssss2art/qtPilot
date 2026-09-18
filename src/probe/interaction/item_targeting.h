// Copyright (c) 2024-2026 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "core/probe.h"  // For QTPILOT_EXPORT

#include <QJsonObject>
#include <QPoint>
#include <QString>

class QGraphicsObject;
class QGraphicsView;

namespace qtPilot {

/// @brief Works out where to click so the event reaches the item addressed.
///
/// Dispatching at a coordinate derived from an item's bounding rect delivers
/// the event to whatever is topmost there, which is not always the item that
/// was asked for. Two things cause that, and they look the same from outside:
/// something is drawn over the item, or the item's own `shape()` excludes the
/// point (an outline whose interior is deliberately click-through has no
/// centre to press).
///
/// Either way the event lands on the wrong object while the call reports
/// success, so the caller's next assertion measures the wrong object and
/// passes. This resolves a point that actually reaches the item, or refuses.
class QTPILOT_EXPORT ItemTargeting {
 public:
  /// @brief Where a click aimed at an item should land.
  struct Target {
    QPoint viewportPoint;   ///< Point in the view's viewport coordinates.
    bool adjusted = false;  ///< True when the preferred point was unusable.
  };

  /// @brief Resolve where to click @p item in @p view.
  ///
  /// With no `position` in @p params the item's centre is preferred, falling
  /// back to a lattice over its bounding rect when the centre does not reach
  /// it. A `position` the caller supplied is never moved: they asked for that
  /// point, so an obstructed one is refused instead of quietly slid off.
  ///
  /// @param item The item being addressed.
  /// @param view The view rendering the item's scene.
  /// @param params The method's parameters; an optional `position` is read.
  /// @param methodName The JSON-RPC method, used in error payloads.
  /// @return The point to dispatch at, and whether it had to move.
  /// @throws JsonRpcException kItemOccluded when no tried point reaches the
  ///         item, naming what is in the way; kCoordinateOutOfBounds when the
  ///         item is scrolled out of the viewport entirely, which is a
  ///         different problem with a different fix; kInvalidParams when
  ///         `position` is malformed.
  static Target resolve(QGraphicsObject* item, QGraphicsView* view, const QJsonObject& params,
                        const QString& methodName);

  /// @brief How many points `resolve()` will try before giving up.
  ///
  /// The cost of a miss is this many scene hit-tests, so it is worth being a
  /// number rather than a detail buried in a loop.
  static int candidatePointCount();
};

}  // namespace qtPilot
