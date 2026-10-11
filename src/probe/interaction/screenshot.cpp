// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "screenshot.h"

#include <stdexcept>

#include <QBuffer>
#include <QGuiApplication>
#include <QImage>
#include <QOperatingSystemVersion>
#include <QPixmap>
#include <QScreen>
#include <QWindow>

#ifdef QTPILOT_HAS_QML
#include <QQuickItem>
#include <QQuickWindow>
#endif

#ifdef Q_OS_MACOS
#include <CoreGraphics/CGDirectDisplay.h>
#endif

namespace {

/// @brief Check screen capture permission on macOS.
///
/// On macOS 10.15+, screen->grabWindow() returns a pixmap with corrupt
/// backing data when Screen Recording permission is not granted. The pixmap
/// reports valid dimensions but the underlying IOSurface is inaccessible,
/// causing a SIGSEGV in the PNG encoder's memmove. We must check permission
/// BEFORE calling grabWindow().
///
/// On other platforms this always returns true.
bool checkScreenCapturePermission([[maybe_unused]] const char* context) {
#ifdef Q_OS_MACOS
  if (QOperatingSystemVersion::current() >= QOperatingSystemVersion::MacOSCatalina) {
    if (!CGPreflightScreenCaptureAccess()) {
      throw std::runtime_error(
          std::string(context) +
          ": screen capture permission denied on macOS. "
          "Grant permission in System Settings > Privacy & Security > Screen Recording, "
          "then restart the terminal/app.");
    }
  }
#endif
  return true;
}

/// @brief Encode a pixmap to base64 PNG with null-pixmap guard.
QByteArray encodePixmap(const QPixmap& pixmap, const char* context) {
  if (pixmap.isNull()) {
    throw std::runtime_error(std::string(context) +
                             ": grab returned a null pixmap "
                             "(window may be hidden or screen capture permission denied)");
  }

  QByteArray bytes;
  QBuffer buffer(&bytes);
  if (!buffer.open(QIODevice::WriteOnly)) {
    throw std::runtime_error(std::string(context) + ": failed to open buffer for PNG encoding");
  }
  if (!pixmap.save(&buffer, "PNG")) {
    throw std::runtime_error(std::string(context) + ": failed to encode screenshot as PNG");
  }

  return bytes.toBase64();
}

}  // namespace

namespace qtPilot {

QByteArray Screenshot::captureWidget(QWidget* widget) {
  if (!widget) {
    throw std::invalid_argument("captureWidget: widget cannot be null");
  }

  // QWidget::grab() renders the widget offscreen — no screen capture permission needed
  QPixmap pixmap = widget->grab();
  return encodePixmap(pixmap, "captureWidget");
}

QByteArray Screenshot::captureWindow(QWidget* window) {
  if (!window) {
    throw std::invalid_argument("captureWindow: window cannot be null");
  }

  QScreen* screen = window->screen();
  if (!screen) {
    screen = QGuiApplication::primaryScreen();
  }
  if (!screen) {
    throw std::runtime_error("captureWindow: cannot determine screen for screenshot");
  }

  checkScreenCapturePermission("captureWindow");
  QPixmap pixmap = screen->grabWindow(window->winId());
  return encodePixmap(pixmap, "captureWindow");
}

QByteArray Screenshot::captureRegion(QWidget* widget, const QRect& region) {
  if (!widget) {
    throw std::invalid_argument("captureRegion: widget cannot be null");
  }

  QPixmap pixmap = widget->grab(region);
  return encodePixmap(pixmap, "captureRegion");
}

// --- Extended capture methods for Computer Use Mode ---

QByteArray Screenshot::captureScreen(QWidget* windowOnTargetScreen) {
  if (!windowOnTargetScreen) {
    throw std::invalid_argument("captureScreen: widget cannot be null");
  }

  QScreen* screen = windowOnTargetScreen->screen();
  if (!screen) {
    screen = QGuiApplication::primaryScreen();
  }
  if (!screen) {
    throw std::runtime_error("captureScreen: cannot determine screen for screenshot");
  }

  checkScreenCapturePermission("captureScreen");
  // grabWindow(0) captures the entire screen/desktop
  QPixmap pixmap = screen->grabWindow(0);
  return encodePixmap(pixmap, "captureScreen");
}

QByteArray Screenshot::captureWindowLogical(QWidget* window) {
  if (!window) {
    throw std::invalid_argument("captureWindowLogical: window cannot be null");
  }

  QScreen* screen = window->screen();
  if (!screen) {
    screen = QGuiApplication::primaryScreen();
  }
  if (!screen) {
    throw std::runtime_error("captureWindowLogical: cannot determine screen for screenshot");
  }

  checkScreenCapturePermission("captureWindowLogical");
  QPixmap pixmap = screen->grabWindow(window->winId());

  if (pixmap.isNull()) {
    throw std::runtime_error(
        "captureWindowLogical: grab returned a null pixmap "
        "(window may be hidden or screen capture permission denied)");
  }

  // Scale down to logical pixels if on HiDPI display
  qreal dpr = window->devicePixelRatioF();
  if (dpr <= 1.0) {
    dpr = pixmap.devicePixelRatio();
  }
  if (dpr > 1.0) {
    int logicalWidth = qRound(pixmap.width() / dpr);
    int logicalHeight = qRound(pixmap.height() / dpr);
    pixmap =
        pixmap.scaled(logicalWidth, logicalHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    pixmap.setDevicePixelRatio(1.0);
  }

  return encodePixmap(pixmap, "captureWindowLogical");
}

// --- QWindow overloads (pure Qt Quick apps) ---

namespace {

#ifdef QTPILOT_HAS_QML
/// @brief Render a Qt Quick window offscreen.
/// @return A pixmap tagged with the window's device pixel ratio, or a null
///         pixmap if the scene graph could not render it.
QPixmap grabQuickWindow(QQuickWindow* window) {
  const QImage image = window->grabWindow();
  if (image.isNull()) {
    return {};
  }
  QPixmap pixmap = QPixmap::fromImage(image);
  // Ensure the device pixel ratio is set so the downstream logical/region
  // scaling is correct on HiDPI displays even if grabWindow() left it 1.0.
  if (pixmap.devicePixelRatio() <= 1.0) {
    pixmap.setDevicePixelRatio(window->devicePixelRatio());
  }
  return pixmap;
}
#endif

/// @brief Grab a QWindow as a QPixmap.
///
/// Prefers QQuickWindow::grabWindow() (offscreen render, no screen-recording
/// permission required); otherwise falls back to a composited screen grab of
/// the native window (subject to the macOS permission check).
QPixmap grabWindowPixmap(QWindow* window, const char* context) {
#ifdef QTPILOT_HAS_QML
  if (auto* quickWindow = qobject_cast<QQuickWindow*>(window)) {
    // grabWindow() re-renders the scene graph to an offscreen surface, so it
    // captures the actual UI even when the window is occluded or on a background
    // Space (unlike a screen grab, which would capture whatever is on top).
#ifdef QTPILOT_HAS_QUICK_CAPTURE
    // Qt 6 renders offscreen when the window is not exposed too, where a screen
    // grab could only capture whatever lies under it, so try it whether or not
    // the window is showing; only fall back to the screen grab if it yields
    // nothing.
    const bool canGrab = true;
#else
    // Qt 5 renders a window that is not showing through an OpenGL context it
    // creates there and then, and aborts the process if it cannot.
    const bool canGrab = quickWindow->isVisible();
#endif
    if (canGrab) {
      QPixmap pixmap = grabQuickWindow(quickWindow);
      if (!pixmap.isNull()) {
        return pixmap;
      }
    }
  }
#endif
  QScreen* screen = window->screen();
  if (!screen) {
    screen = QGuiApplication::primaryScreen();
  }
  if (!screen) {
    throw std::runtime_error(std::string(context) + ": cannot determine screen for screenshot");
  }
  checkScreenCapturePermission(context);
  QPixmap pixmap = screen->grabWindow(window->winId());
  // QScreen::grabWindow returns physical pixels tagged with dpr==1 on most
  // platforms; stamp the window's ratio so the downstream logical/region
  // scaling matches the QQuickWindow::grabWindow() branch on HiDPI displays.
  if (!pixmap.isNull() && pixmap.devicePixelRatio() <= 1.0) {
    pixmap.setDevicePixelRatio(window->devicePixelRatio());
  }
  return pixmap;
}

}  // namespace

QByteArray Screenshot::captureWindow(QWindow* window) {
  if (!window) {
    throw std::invalid_argument("captureWindow: window cannot be null");
  }
  return encodePixmap(grabWindowPixmap(window, "captureWindow"), "captureWindow");
}

QByteArray Screenshot::captureRegion(QWindow* window, const QRect& region) {
  if (!window) {
    throw std::invalid_argument("captureRegion: window cannot be null");
  }
  QPixmap full = grabWindowPixmap(window, "captureRegion");
  // grabWindow() honors the device pixel ratio; scale the (logical) region to
  // match the pixmap's physical pixels before cropping.
  qreal dpr = full.devicePixelRatio();
  QRect physRegion(qRound(region.x() * dpr), qRound(region.y() * dpr), qRound(region.width() * dpr),
                   qRound(region.height() * dpr));
  return encodePixmap(full.copy(physRegion), "captureRegion");
}

QByteArray Screenshot::captureScreen(QWindow* windowOnTargetScreen) {
  if (!windowOnTargetScreen) {
    throw std::invalid_argument("captureScreen: window cannot be null");
  }

  QScreen* screen = windowOnTargetScreen->screen();
  if (!screen) {
    screen = QGuiApplication::primaryScreen();
  }
  if (!screen) {
    throw std::runtime_error("captureScreen: cannot determine screen for screenshot");
  }

  checkScreenCapturePermission("captureScreen");
  QPixmap pixmap = screen->grabWindow(0);
  return encodePixmap(pixmap, "captureScreen");
}

QByteArray Screenshot::captureWindowLogical(QWindow* window) {
  if (!window) {
    throw std::invalid_argument("captureWindowLogical: window cannot be null");
  }

  QPixmap pixmap = grabWindowPixmap(window, "captureWindowLogical");
  if (pixmap.isNull()) {
    throw std::runtime_error(
        "captureWindowLogical: grab returned a null pixmap "
        "(window may be hidden or screen capture permission denied)");
  }

  // Scale down to logical pixels if on a HiDPI display
  qreal dpr = pixmap.devicePixelRatio();
  if (dpr > 1.0) {
    int logicalWidth = qRound(pixmap.width() / dpr);
    int logicalHeight = qRound(pixmap.height() / dpr);
    pixmap =
        pixmap.scaled(logicalWidth, logicalHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  }

  return encodePixmap(pixmap, "captureWindowLogical");
}

#ifdef QTPILOT_HAS_QUICK_CAPTURE

namespace {

/// @brief A logical scene rect as the device pixels it covers at @p dpr.
///
/// Each edge is rounded on its own, so an integral logical rect becomes
/// exactly size * dpr and adjacent rects share an edge rather than overlapping
/// or leaving a gap -- toAlignedRect() would grow a fractional rect outward by
/// a pixel of whatever surrounds it.
QRect toDevicePixels(const QRectF& logical, qreal dpr) {
  const int left = qRound(logical.left() * dpr);
  const int top = qRound(logical.top() * dpr);
  const int right = qRound(logical.right() * dpr);
  const int bottom = qRound(logical.bottom() * dpr);
  return QRect(QPoint(left, top), QSize(right - left, bottom - top));
}

/// @brief Render @p item's window and crop to @p sceneRect (logical pixels).
QByteArray captureSceneRect(QQuickItem* item, const QRectF& sceneRect, const char* context) {
  if (!item) {
    throw std::invalid_argument(std::string(context) + ": item cannot be null");
  }
  QQuickWindow* window = item->window();
  if (!window) {
    throw std::invalid_argument(std::string(context) + ": item is not on a window");
  }

  const QRectF visible = sceneRect.intersected(QRectF(QPointF(0, 0), QSizeF(window->size())));
  if (visible.isEmpty()) {
    throw std::runtime_error(std::string(context) + ": the area lies outside the item's window");
  }

  const QPixmap full = grabQuickWindow(window);
  if (full.isNull()) {
    throw std::runtime_error(std::string(context) +
                             ": the scene graph could not render the item's window");
  }
  QPixmap cropped = full.copy(toDevicePixels(visible, full.devicePixelRatio()));
  return encodePixmap(cropped, context);
}

}  // namespace

QByteArray Screenshot::captureItem(QQuickItem* item) {
  if (!item) {
    throw std::invalid_argument("captureItem: item cannot be null");
  }
  // The item's own rect rather than boundingRect(), which a subclass may grow
  // to include painting outside the item; this is the area the item occupies.
  return captureSceneRect(item, item->mapRectToScene(QRectF(QPointF(0, 0), item->size())),
                          "captureItem");
}

QByteArray Screenshot::captureItemRegion(QQuickItem* item, const QRectF& region) {
  if (!item) {
    throw std::invalid_argument("captureItemRegion: item cannot be null");
  }
  return captureSceneRect(item, item->mapRectToScene(region), "captureItemRegion");
}

#endif  // QTPILOT_HAS_QUICK_CAPTURE

}  // namespace qtPilot
