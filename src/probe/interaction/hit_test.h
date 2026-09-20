// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "core/probe.h"  // For QTPILOT_EXPORT

#include <expected>

#include <QJsonObject>
#include <QPoint>
#include <QWidget>

class QWindow;
class QGraphicsObject;
class QGraphicsView;
#ifdef QTPILOT_HAS_QML
class QQuickItem;
class QQuickWindow;
#endif

namespace qtPilot {

/// @brief Widget geometry and hit testing utilities (UI-04, UI-05).
///
/// Provides coordinate conversion and widget discovery functions:
/// - Get widget geometry in local and global coordinates
/// - Find widgets at screen coordinates
/// - Find child widgets at local coordinates
///
/// Usage:
/// @code
///   // Get geometry in both coordinate systems
///   QJsonObject geo = HitTest::widgetGeometry(button);
///   // Returns: { "local": {...}, "global": {...}, "devicePixelRatio": 1.0 }
///
///   // Find widget at screen position
///   QWidget* w = HitTest::widgetAt(QPoint(100, 100));
///
///   // Find deepest child at local position
///   QWidget* child = HitTest::childAt(parent, QPoint(50, 50));
/// @endcode
class QTPILOT_EXPORT HitTest {
 public:
  /// @brief Get widget geometry in local and global coordinates (UI-04).
  /// @param widget Widget to query
  /// @return JSON with local and global geometry plus devicePixelRatio
  ///
  /// JSON format:
  /// @code
  /// {
  ///   "local": { "x": 10, "y": 20, "width": 100, "height": 30 },
  ///   "global": { "x": 110, "y": 220, "width": 100, "height": 30 },
  ///   "devicePixelRatio": 1.0
  /// }
  /// @endcode
  static QJsonObject widgetGeometry(QWidget* widget);

  /// @brief Monadically get widget geometry in local and global coordinates.
  /// @param widget Widget to query
  /// @return JSON with local and global geometry, or error message on failure
  static std::expected<QJsonObject, QString> widgetGeometryExpected(QWidget* widget);

  /// @brief Find widget at global screen coordinates.
  /// @param globalPos Screen coordinates
  /// @return Widget at position, or nullptr if none
  static QWidget* widgetAt(const QPoint& globalPos);

  /// @brief Find deepest child widget at local coordinates (UI-05).
  /// @param parent Parent widget to search within
  /// @param localPos Position relative to parent
  /// @return Deepest visible child at position, or parent if none
  static QWidget* childAt(QWidget* parent, const QPoint& localPos);

  /// @brief Find the object at global coordinates and return its ID.
  ///
  /// Usually a QWidget, but NOT always: when the point falls on a
  /// QGraphicsView's viewport, the search continues into that view's scene and
  /// the ID returned is the scene item's -- a QGraphicsObject, never a QWidget.
  /// Callers that feed this ID straight into a widget-only API must be prepared
  /// for that. On empty canvas it falls back to the viewport, as before.
  /// @param globalPos Screen coordinates
  /// @return Object ID of the object at that position, or empty string if none
  static QString widgetIdAt(const QPoint& globalPos);

  // --- QGraphicsView scene items ---
  //
  // Everything in a QGraphicsView plan is a QGraphicsObject, not a QWidget, so
  // the widget entry points above report nothing for one. These map an item
  // through the view(s) rendering it, which is the step a caller otherwise has
  // to reverse-engineer by dragging and dividing.

  /// @brief Get a graphics item's geometry in item, scene and screen coordinates.
  ///
  /// Local is the item's own boundingRect(); scene is that rect mapped through
  /// the item's position and transform; global is it mapped on through a view.
  /// Every value is a **double** -- graphics coordinates are qreal and routinely
  /// fractional, so toInt() would yield 0. Same caveat as itemGeometry().
  ///
  /// Two deliberate choices, both of which callers depend on:
  /// - **boundingRect(), not childrenBoundingRect().** A scene item is commonly
  ///   a group (a symbol plus a label hanging below it) whose clickable body is
  ///   the item itself. Use childrenBoundingRect() at the call site if you want
  ///   the union.
  /// - **Every view is reported.** One scene is often rendered into several
  ///   views at different scales, so "the first view" would be a coin flip.
  ///   "views" holds one entry per view; the top-level "global", "viewport",
  ///   "visible" and "devicePixelRatio" mirror @a preferredView, or the first
  ///   view when none is given.
  ///
  /// An item scrolled outside the viewport is not an error: its rect is still
  /// reported and "visible" is false, so a caller can decide to scroll to it.
  /// An item in a scene with no view at all -- or a @a preferredView that does
  /// not render this item's scene -- reports a null "global".
  ///
  /// JSON format:
  /// @code
  /// {
  ///   "local":  { "x": 0, "y": 0, "width": 40, "height": 20 },
  ///   "scene":  { "x": 300, "y": 300, "width": 40, "height": 20 },
  ///   "viewport": { "x": 255, "y": 295, "width": 50, "height": 25 },
  ///   "global": { "x": 355, "y": 395, "width": 50, "height": 25 },
  ///   "visible": true,
  ///   "devicePixelRatio": 1.0,
  ///   "views": [ { "viewObjectId": "...", "viewport": {...}, "global": {...},
  ///                "visible": true, "devicePixelRatio": 1.0 } ]
  /// }
  /// @endcode
  /// @param item Item to query
  /// @param preferredView View to mirror at the top level; nullptr picks the first
  static QJsonObject graphicsItemGeometry(QGraphicsObject* item,
                                          QGraphicsView* preferredView = nullptr);

  /// @brief Find the topmost *addressable* scene item at a viewport position.
  ///
  /// Bare QGraphicsItems are not QObjects and have no ID of their own, and
  /// decorations drawn as bare items are extremely common. Two things follow,
  /// and both are deliberate:
  /// - a hit on a bare item walks up to the nearest QGraphicsObject ancestor;
  /// - a bare item that has no such ancestor does not mask what is underneath.
  ///   The whole stack at the point is walked in descending order, so a
  ///   parentless grid line or overlay cannot swallow the item below it.
  ///
  /// A position outside the viewport is a miss, not an answer: the view would
  /// otherwise project it into scene space and name an item for a point that is
  /// not on the canvas at all (a scroll bar, say).
  /// @param view View to hit test within
  /// @param viewportPos Position in the view's *viewport* coordinates
  /// @return The item at that position, or nullptr on a miss
  static QGraphicsObject* graphicsItemAt(QGraphicsView* view, const QPointF& viewportPos);

  /// @brief graphicsItemAt() as an object ID.
  ///
  /// Prefer graphicsItemAt() when the caller also needs the object -- looking
  /// the ID back up costs a registry search and can fail for an untracked item.
  /// @param view View to hit test within
  /// @param viewportPos Position in the view's *viewport* coordinates
  /// @return Object ID of the item at that position, or empty string on a miss
  static QString graphicsItemIdAt(QGraphicsView* view, const QPointF& viewportPos);

  // --- QWindow / Qt Quick equivalents ---
  //
  // A pure Qt Quick app has no QWidget anywhere, so the widget entry points
  // above return nothing for it. These mirror them for QWindow/QQuickItem
  // targets and emit the same JSON shape.

  /// @brief Get window geometry in local and global coordinates.
  ///
  /// Local is the window's own rect at the origin; global is its position on
  /// screen. Same JSON shape as widgetGeometry().
  /// @param window Window to query
  static QJsonObject windowGeometry(QWindow* window);

#ifdef QTPILOT_HAS_QML
  /// @brief Get a QML item's geometry in local, scene and global coordinates.
  ///
  /// Local is the item's rect within its parent; scene is window-local (the
  /// space Qt Quick input events use); global is screen coordinates.
  ///
  /// Two differences from widgetGeometry() that clients must handle:
  /// - there is an extra "scene" rect;
  /// - every value is a **double**, not an int, because QML positions are
  ///   routinely fractional. Read them with toDouble(); toInt() yields 0 for
  ///   any non-integral value.
  ///
  /// An item with no window is not an error: "local" and "scene" are still
  /// meaningful, but "global" is JSON null and "devicePixelRatio" is 1.0.
  /// @param item Item to query
  static QJsonObject itemGeometry(QQuickItem* item);

  /// @brief Find the deepest visible QML item at a scene position.
  ///
  /// Walks children in paint order (z, then document order) and descends
  /// through non-clipping items even when the point falls outside their own
  /// bounds, matching Qt Quick's own delivery.
  /// @param window Window to search within
  /// @param scenePos Position in scene (window-local) coordinates
  /// @return Deepest enabled+visible item at the position, or nullptr when the
  ///         position is outside the scene. Unlike childAt(), this does NOT
  ///         fall back to the root -- a miss is reported as a miss.
  static QQuickItem* itemAt(QQuickWindow* window, const QPointF& scenePos);

  /// @brief Find a QML item at global coordinates and return its ID.
  ///
  /// Scans top-level QQuickWindows; used when no parent is supplied and the
  /// widget hit test found nothing.
  /// @param globalPos Screen coordinates
  /// @return Object ID of the item at that position, or empty string
  static QString quickItemIdAt(const QPoint& globalPos);
#endif
};

}  // namespace qtPilot
