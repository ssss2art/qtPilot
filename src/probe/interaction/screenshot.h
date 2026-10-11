// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "core/probe.h"  // For QTPILOT_EXPORT

#include <QByteArray>
#include <QRect>
#include <QRectF>
#include <QWidget>

class QWindow;
class QQuickItem;

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

#ifdef QTPILOT_HAS_QML
  // --- QQuickItem capture ---
  //
  // An item has no surface of its own, so it is captured by rendering its
  // window with QQuickWindow::grabWindow() and cropping to where the item is in
  // the scene. grabWindow() renders the scene graph synchronously, and renders
  // offscreen when the window is not exposed -- a hidden window, a window under
  // the offscreen platform, or the internal window of a QQuickWidget -- so no
  // screen-recording permission is involved and no frame needs to be awaited.

  /// @brief Capture a Qt Quick item, cropped to its bounds in the scene.
  ///
  /// The crop is the item's own rect mapped to the scene, so a transformed
  /// item is captured by its on-screen bounding box. Like QWidget::grab(), the
  /// image is in device pixels: an item of logical size w x h on a window with
  /// device pixel ratio r yields a round(w*r) x round(h*r) image.
  ///
  /// @param item Item to capture; must be on a window, visible, and non-empty.
  /// @return Base64-encoded PNG image
  /// @throws std::invalid_argument if @p item is null or not on a window
  /// @throws std::runtime_error if the item lies wholly outside its window, or
  ///         the window cannot be rendered
  static QByteArray captureItem(QQuickItem* item);

  /// @brief Capture a rectangle of a Qt Quick item.
  ///
  /// Like captureRegion(QWidget*, QRect), @p region is relative to the target:
  /// here it is in the item's own coordinates, and is mapped to the scene.
  ///
  /// @param item Item to capture from; must be on a window.
  /// @param region Rectangle in item coordinates (logical pixels)
  /// @return Base64-encoded PNG image
  /// @throws std::invalid_argument if @p item is null or not on a window
  /// @throws std::runtime_error if the region lies wholly outside the window,
  ///         or the window cannot be rendered
  static QByteArray captureItemRegion(QQuickItem* item, const QRectF& region);
#endif
};

}  // namespace qtPilot
