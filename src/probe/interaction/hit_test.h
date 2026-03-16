// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "core/probe.h"  // For QTPILOT_EXPORT

#include <QJsonObject>
#include <QPoint>
#include <QWidget>

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
};

}  // namespace qtPilot
