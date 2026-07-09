// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "core/probe.h"  // For QTPILOT_EXPORT

#include <QByteArray>
#include <QRect>
#include <QWidget>

class QWindow;

namespace qtPilot {

/// @brief Screenshot capture utility (UI-03).
///
/// Provides methods to capture widgets, windows, and regions as PNG images
/// encoded in base64. This is useful for visual debugging, UI verification,
/// and communicating screen state to AI assistants.
///
/// Usage:
/// @code
///   // Capture a widget
///   QByteArray png = Screenshot::captureWidget(button);
///
///   // Capture entire window with frame
///   QByteArray png = Screenshot::captureWindow(mainWindow);
///
///   // Capture a region
///   QByteArray png = Screenshot::captureRegion(widget, QRect(0, 0, 100, 100));
/// @endcode
class QTPILOT_EXPORT Screenshot {
 public:
  /// @brief Capture a widget and its children.
  /// @param widget Widget to capture
  /// @return Base64-encoded PNG image
  /// @throws std::invalid_argument if widget is null
  static QByteArray captureWidget(QWidget* widget);

  /// @brief Capture an entire window including frame/decorations.
  /// @param window Window to capture (should be top-level)
  /// @return Base64-encoded PNG image
  /// @throws std::invalid_argument if window is null
  /// @throws std::runtime_error if screen cannot be determined
  static QByteArray captureWindow(QWidget* window);

  /// @brief Capture a rectangular region of a widget.
  /// @param widget Widget to capture from
  /// @param region Rectangle in widget coordinates
  /// @return Base64-encoded PNG image
  /// @throws std::invalid_argument if widget is null
  static QByteArray captureRegion(QWidget* widget, const QRect& region);

  // --- Extended capture methods for Computer Use Mode ---

  /// @brief Capture the entire screen containing the given widget.
  /// @param windowOnTargetScreen Any widget on the target screen
  /// @return Base64-encoded PNG image of the full screen
  /// @throws std::invalid_argument if widget is null
  /// @throws std::runtime_error if screen cannot be determined
  static QByteArray captureScreen(QWidget* windowOnTargetScreen);

  /// @brief Capture a window scaled to logical pixel dimensions.
  ///
  /// On HiDPI displays (devicePixelRatio > 1.0), the captured image is
  /// scaled down so that pixel coordinates in the image match logical
  /// widget coordinates 1:1. This ensures click coordinates derived from
  /// the screenshot are accurate without DPI conversion.
  ///
  /// @param window Window to capture (should be top-level)
  /// @return Base64-encoded PNG image at logical pixel resolution
  /// @throws std::invalid_argument if window is null
  /// @throws std::runtime_error if screen cannot be determined
  static QByteArray captureWindowLogical(QWidget* window);

  // --- QWindow overloads (pure Qt Quick apps: QQuickWindow has no QWidget) ---
  //
  // A QQuickWindow is captured with QQuickWindow::grabWindow() (an offscreen
  // render needing no screen-recording permission, like QWidget::grab());
  // other QWindows fall back to a composited screen grab of the native window.

  /// @brief Capture a window (QQuickWindow rendered offscreen when possible).
  static QByteArray captureWindow(QWindow* window);

  /// @brief Capture a rectangular region (window-local coordinates) of a window.
  static QByteArray captureRegion(QWindow* window, const QRect& region);

  /// @brief Capture the entire screen containing the given window.
  static QByteArray captureScreen(QWindow* windowOnTargetScreen);

  /// @brief Capture a window scaled to logical pixel dimensions.
  static QByteArray captureWindowLogical(QWindow* window);
};

}  // namespace qtPilot
