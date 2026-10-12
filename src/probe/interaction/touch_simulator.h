// Copyright (c) 2026 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "core/probe.h"  // For QTPILOT_EXPORT

#include <QList>
#include <QPointF>
#include <QString>

class QWindow;

// Touch goes through QPointingDevice and QEventPoint, which Qt 5 does not have.
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#define QTPILOT_HAS_TOUCH
#endif

namespace qtPilot {

/// @brief One finger in a frame: its id and where it is, in window coordinates.
struct TouchFinger {
  int id = 0;
  QPointF windowPosition;
};

/// @brief What a frame did to one finger.
struct TouchReport {
  int id = 0;
  QString state;  ///< "pressed", "moved", "stationary" or "released".
  QPointF windowPosition;
};

/// @brief Touchscreen input, delivered a frame at a time.
///
/// Each frame names the fingers that are down after it. A finger new to the
/// frame is pressed, one that moved is updated, one that stayed is stationary,
/// and one that was down but is missing is released -- all in one touch event,
/// as a touchscreen reports them. The fingers stay down between frames, so the
/// caller's own pacing is the gesture's timing: a long press is a frame, a wait,
/// and an empty frame.
///
/// Delivery goes through the window system interface, as the platform's own
/// touch input does, from a touchscreen device registered for the purpose. Qt
/// Quick's pointer handlers, touch-to-mouse synthesis and popup overlays then
/// behave as they do for a finger.
class QTPILOT_EXPORT TouchSimulator {
 public:
  /// @brief Whether any finger is down.
  static bool isHeld();

  /// @brief The window the fingers that are down belong to, or null.
  static QWindow* heldWindow();

  /// @brief Apply one frame to @p window and queue its touch event.
  /// @param window Where the fingers are; must be heldWindow() while any is down.
  /// @param fingers The fingers down after this frame. Ids must be distinct.
  /// @return Every finger the frame touched, by ascending id.
  /// @note Delivery is queued, as qt.ui.click is, so the caller's request
  ///       completes before the application reacts to the touch.
  static QList<TouchReport> frame(QWindow* window, const QList<TouchFinger>& fingers);
};

}  // namespace qtPilot
