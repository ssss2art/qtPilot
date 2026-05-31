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
  buffer.open(QIODevice::WriteOnly);
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

}  // namespace qtPilot
