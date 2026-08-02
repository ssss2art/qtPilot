// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "core/probe.h"  // For QTPILOT_EXPORT

#include <QJsonObject>
#include <QPoint>
#include <QWidget>

class QWindow;
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

  /// @brief Find widget at global screen coordinates.
  /// @param globalPos Screen coordinates
  /// @return Widget at position, or nullptr if none
  static QWidget* widgetAt(const QPoint& globalPos);

  /// @brief Find deepest child widget at local coordinates (UI-05).
  /// @param parent Parent widget to search within
  /// @param localPos Position relative to parent
  /// @return Deepest visible child at position, or parent if none
  static QWidget* childAt(QWidget* parent, const QPoint& localPos);

  /// @brief Find widget at global coordinates and return its ID.
  /// @param globalPos Screen coordinates
  /// @return Object ID of widget at position, or empty string if none
  static QString widgetIdAt(const QPoint& globalPos);

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
