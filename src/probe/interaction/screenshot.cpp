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
  qreal dpr = pixmap.devicePixelRatio();
  if (dpr > 1.0) {
    int logicalWidth = qRound(pixmap.width() / dpr);
    int logicalHeight = qRound(pixmap.height() / dpr);
    pixmap =
        pixmap.scaled(logicalWidth, logicalHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  }

  return encodePixmap(pixmap, "captureWindowLogical");
}

// --- QWindow overloads (pure Qt Quick apps) ---

namespace {

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
    // Space (unlike a screen grab, which would capture whatever is on top). Try
    // it whenever the window has been shown; only fall back to the screen grab
    // if it yields nothing.
    if (quickWindow->isVisible()) {
      QImage image = quickWindow->grabWindow();
      if (!image.isNull()) {
        QPixmap pixmap = QPixmap::fromImage(image);
        // Ensure the device pixel ratio is set so the downstream logical/region
        // scaling is correct on HiDPI displays even if grabWindow() left it 1.0.
        if (pixmap.devicePixelRatio() <= 1.0) {
          pixmap.setDevicePixelRatio(quickWindow->devicePixelRatio());
        }
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

}  // namespace qtPilot
