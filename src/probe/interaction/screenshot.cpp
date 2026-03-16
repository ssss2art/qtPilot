// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "screenshot.h"

#include <stdexcept>

#include <QBuffer>
#include <QGuiApplication>
#include <QPixmap>
#include <QScreen>

namespace qtPilot {

QByteArray Screenshot::captureWidget(QWidget* widget) {
  if (!widget) {
    throw std::invalid_argument("captureWidget: widget cannot be null");
  }

  // QWidget::grab() captures the widget and all children
  QPixmap pixmap = widget->grab();

  QByteArray bytes;
  QBuffer buffer(&bytes);
  buffer.open(QIODevice::WriteOnly);
  pixmap.save(&buffer, "PNG");

  return bytes.toBase64();
}

QByteArray Screenshot::captureWindow(QWidget* window) {
  if (!window) {
    throw std::invalid_argument("captureWindow: window cannot be null");
  }

  // Use screen grab for window including frame
  QScreen* screen = window->screen();
  if (!screen) {
    screen = QGuiApplication::primaryScreen();
  }

  if (!screen) {
    throw std::runtime_error("captureWindow: cannot determine screen for screenshot");
  }

  QPixmap pixmap = screen->grabWindow(window->winId());

  QByteArray bytes;
  QBuffer buffer(&bytes);
  buffer.open(QIODevice::WriteOnly);
  pixmap.save(&buffer, "PNG");

  return bytes.toBase64();
}

QByteArray Screenshot::captureRegion(QWidget* widget, const QRect& region) {
  if (!widget) {
    throw std::invalid_argument("captureRegion: widget cannot be null");
  }

  QPixmap pixmap = widget->grab(region);

  QByteArray bytes;
  QBuffer buffer(&bytes);
  buffer.open(QIODevice::WriteOnly);
  pixmap.save(&buffer, "PNG");

  return bytes.toBase64();
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

  // grabWindow(0) captures the entire screen/desktop
  QPixmap pixmap = screen->grabWindow(0);

  QByteArray bytes;
  QBuffer buffer(&bytes);
  buffer.open(QIODevice::WriteOnly);
  pixmap.save(&buffer, "PNG");

  return bytes.toBase64();
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

  QPixmap pixmap = screen->grabWindow(window->winId());

  // Scale down to logical pixels if on HiDPI display
  qreal dpr = pixmap.devicePixelRatio();
  if (dpr > 1.0) {
    int logicalWidth = qRound(pixmap.width() / dpr);
    int logicalHeight = qRound(pixmap.height() / dpr);
    pixmap =
        pixmap.scaled(logicalWidth, logicalHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  }

  QByteArray bytes;
  QBuffer buffer(&bytes);
  buffer.open(QIODevice::WriteOnly);
  pixmap.save(&buffer, "PNG");

  return bytes.toBase64();
}

}  // namespace qtPilot
