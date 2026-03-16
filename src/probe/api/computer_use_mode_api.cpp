// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "api/computer_use_mode_api.h"

#include "api/error_codes.h"
#include "api/response_envelope.h"
#include "interaction/hit_test.h"
#include "interaction/input_simulator.h"
#include "interaction/key_name_mapper.h"
#include "interaction/screenshot.h"

#include <QApplication>
#include <QCursor>
#include <QDateTime>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QWidget>

namespace qtPilot {

// ============================================================================
// Internal helpers (file-scope, not in header)
// ============================================================================

namespace {

/// @brief Parse JSON params string into QJsonObject.
QJsonObject parseParams(const QString& params) {
  return QJsonDocument::fromJson(params.toUtf8()).object();
}

/// @brief Serialize a response envelope to compact JSON string.
QString envelopeToString(const QJsonObject& envelope) {
  return QString::fromUtf8(QJsonDocument(envelope).toJson(QJsonDocument::Compact));
}

/// @brief Get the active window, falling back to first visible top-level widget.
/// @throws JsonRpcException if no active window found.
QWidget* getActiveWindow() {
  QWidget* window = QApplication::activeWindow();
  if (window)
    return window;

  // Fall back to first visible top-level widget
  const auto topLevels = QApplication::topLevelWidgets();
  for (QWidget* w : topLevels) {
    if (w->isVisible()) {
      return w;
    }
  }

  throw JsonRpcException(
      ErrorCode::kNoActiveWindow, QStringLiteral("No active Qt window found"),
      QJsonObject{
          {QStringLiteral("hint"), QStringLiteral("Ensure the application has a visible window")}});
}

/// @brief Result of resolving a coordinate to a target widget.
struct ResolvedTarget {
  QWidget* widget;
  QPoint localPos;
};

/// @brief Resolve a coordinate to a target widget and local position.
/// @param window The active window for window-relative coordinates.
/// @param x X coordinate
/// @param y Y coordinate
/// @param screenAbsolute If true, treat x,y as screen-absolute coordinates.
/// @throws JsonRpcException if coordinates are out of bounds.
ResolvedTarget resolveWindowCoordinate(QWidget* window, int x, int y, bool screenAbsolute) {
  if (screenAbsolute) {
    QPoint globalPos(x, y);
    QWidget* target = QApplication::widgetAt(globalPos);
    if (!target) {
      throw JsonRpcException(
          ErrorCode::kCoordinateOutOfBounds,
          QStringLiteral("No widget found at screen coordinates (%1, %2)").arg(x).arg(y),
          QJsonObject{{QStringLiteral("x"), x},
                      {QStringLiteral("y"), y},
                      {QStringLiteral("screenAbsolute"), true}});
    }
    QPoint localPos = target->mapFromGlobal(globalPos);
    return {target, localPos};
  }

  // Window-relative: bounds-check against window size
  QSize winSize = window->size();
  if (x < 0 || y < 0 || x >= winSize.width() || y >= winSize.height()) {
    throw JsonRpcException(
        ErrorCode::kCoordinateOutOfBounds,
        QStringLiteral("Coordinates (%1, %2) out of bounds for window size (%3 x %4)")
            .arg(x)
            .arg(y)
            .arg(winSize.width())
            .arg(winSize.height()),
        QJsonObject{{QStringLiteral("x"), x},
                    {QStringLiteral("y"), y},
                    {QStringLiteral("windowWidth"), winSize.width()},
                    {QStringLiteral("windowHeight"), winSize.height()}});
  }

  // Find deepest child widget at this position
  QWidget* child = window->childAt(QPoint(x, y));
  if (child) {
    QPoint localPos = child->mapFrom(window, QPoint(x, y));
    return {child, localPos};
  }

  // No child found - target is the window itself
  return {window, QPoint(x, y)};
}

/// @brief Convert button string to InputSimulator::MouseButton enum.
InputSimulator::MouseButton parseMouseButton(const QString& buttonStr) {
  if (buttonStr == QStringLiteral("right"))
    return InputSimulator::MouseButton::Right;
  if (buttonStr == QStringLiteral("middle"))
    return InputSimulator::MouseButton::Middle;
  return InputSimulator::MouseButton::Left;
}

/// @brief Optionally capture a screenshot and add to result.
void maybeAddScreenshot(QJsonObject& result, const QJsonObject& params, QWidget* window) {
  if (params[QStringLiteral("include_screenshot")].toBool(false)) {
    QByteArray base64 = Screenshot::captureWindowLogical(window);
    result[QStringLiteral("screenshot")] = QString::fromLatin1(base64);
  }
}

/// @brief Get image dimensions from base64 PNG data.
QJsonObject imageWithDimensions(const QByteArray& base64) {
  QJsonObject result;
  result[QStringLiteral("image")] = QString::fromLatin1(base64);

  // Decode to get dimensions
  QByteArray raw = QByteArray::fromBase64(base64);
  QImage img;
  img.loadFromData(raw, "PNG");
  result[QStringLiteral("width")] = img.width();
  result[QStringLiteral("height")] = img.height();
  return result;
}

// Virtual cursor position tracking for CU mode.
// Set by coordinate-based actions (click, move, drag, etc.), read by cu.cursorPosition.
// Stored as screen-absolute (global) coordinates for consistency with QCursor::pos().
static QPoint s_lastSimulatedPosition(-1, -1);
static bool s_hasSimulatedPosition = false;

/// @brief Update the tracked virtual cursor position after a coordinate-based action.
/// @param window The active window (for mapToGlobal when using window-relative coords).
/// @param x X coordinate as provided by the caller.
/// @param y Y coordinate as provided by the caller.
/// @param screenAbsolute If true, x/y are already screen-absolute.
void trackPosition(QWidget* window, int x, int y, bool screenAbsolute) {
  if (screenAbsolute) {
    s_lastSimulatedPosition = QPoint(x, y);
  } else if (window) {
    s_lastSimulatedPosition = window->mapToGlobal(QPoint(x, y));
  } else {
    s_lastSimulatedPosition = QPoint(x, y);
  }
  s_hasSimulatedPosition = true;
}

}  // anonymous namespace

// ============================================================================
// Constructor - register all method groups
// ============================================================================

ComputerUseModeApi::ComputerUseModeApi(JsonRpcHandler* handler, QObject* parent)
    : QObject(parent), m_handler(handler) {
  registerScreenshotMethods();
  registerMouseMethods();
  registerKeyboardMethods();
  registerScrollMethod();
  registerQueryMethods();
}

// ============================================================================
// Screenshot: cu.screenshot
// ============================================================================

void ComputerUseModeApi::registerScreenshotMethods() {
  m_handler->RegisterMethod(QStringLiteral("cu.screenshot"), [](const QString& params) -> QString {
    auto p = parseParams(params);
    QWidget* window = getActiveWindow();

    bool fullScreen = p[QStringLiteral("fullScreen")].toBool(false);
    bool physicalPixels = p[QStringLiteral("physicalPixels")].toBool(false);
    QJsonObject region = p[QStringLiteral("region")].toObject();

    QByteArray base64;
    if (fullScreen) {
      base64 = Screenshot::captureScreen(window);
    } else if (!region.isEmpty()) {
      QRect rect(region[QStringLiteral("x")].toInt(), region[QStringLiteral("y")].toInt(),
                 region[QStringLiteral("width")].toInt(), region[QStringLiteral("height")].toInt());
      base64 = Screenshot::captureRegion(window, rect);
    } else if (physicalPixels) {
      base64 = Screenshot::captureWindow(window);
    } else {
      // Default: logical pixel capture (1:1 coordinate matching)
      base64 = Screenshot::captureWindowLogical(window);
    }

    QJsonObject result = imageWithDimensions(base64);
    return envelopeToString(ResponseEnvelope::wrap(result));
  });
}

// ============================================================================
// Mouse methods: cu.click, cu.rightClick, cu.middleClick, cu.doubleClick,
//                cu.mouseMove, cu.drag, cu.mouseDown, cu.mouseUp
// ============================================================================

void ComputerUseModeApi::registerMouseMethods() {
  // cu.click - click at coordinates with optional button
  m_handler->RegisterMethod(QStringLiteral("cu.click"), [](const QString& params) -> QString {
    auto p = parseParams(params);
    QWidget* window = getActiveWindow();

    int x = p[QStringLiteral("x")].toInt();
    int y = p[QStringLiteral("y")].toInt();
    bool screenAbsolute = p[QStringLiteral("screenAbsolute")].toBool(false);
    int delayMs = p[QStringLiteral("delay_ms")].toInt(0);
    QString buttonStr = p[QStringLiteral("button")].toString(QStringLiteral("left"));

    if (delayMs > 0) {
      QThread::msleep(static_cast<unsigned long>(delayMs));
    }

    auto target = resolveWindowCoordinate(window, x, y, screenAbsolute);
    InputSimulator::mouseClick(target.widget, parseMouseButton(buttonStr), target.localPos);

    trackPosition(window, x, y, screenAbsolute);

    QJsonObject result;
    result[QStringLiteral("success")] = true;
    maybeAddScreenshot(result, p, window);
    return envelopeToString(ResponseEnvelope::wrap(result));
  });

  // cu.rightClick - right click at coordinates
  m_handler->RegisterMethod(QStringLiteral("cu.rightClick"), [](const QString& params) -> QString {
    auto p = parseParams(params);
    QWidget* window = getActiveWindow();

    int x = p[QStringLiteral("x")].toInt();
    int y = p[QStringLiteral("y")].toInt();
    bool screenAbsolute = p[QStringLiteral("screenAbsolute")].toBool(false);
    int delayMs = p[QStringLiteral("delay_ms")].toInt(0);

    if (delayMs > 0) {
      QThread::msleep(static_cast<unsigned long>(delayMs));
    }

    auto target = resolveWindowCoordinate(window, x, y, screenAbsolute);
    InputSimulator::mouseClick(target.widget, InputSimulator::MouseButton::Right, target.localPos);

    trackPosition(window, x, y, screenAbsolute);

    QJsonObject result;
    result[QStringLiteral("success")] = true;
    maybeAddScreenshot(result, p, window);
    return envelopeToString(ResponseEnvelope::wrap(result));
  });

  // cu.middleClick - middle click at coordinates
  m_handler->RegisterMethod(QStringLiteral("cu.middleClick"), [](const QString& params) -> QString {
    auto p = parseParams(params);
    QWidget* window = getActiveWindow();

    int x = p[QStringLiteral("x")].toInt();
    int y = p[QStringLiteral("y")].toInt();
    bool screenAbsolute = p[QStringLiteral("screenAbsolute")].toBool(false);
    int delayMs = p[QStringLiteral("delay_ms")].toInt(0);

    if (delayMs > 0) {
      QThread::msleep(static_cast<unsigned long>(delayMs));
    }

    auto target = resolveWindowCoordinate(window, x, y, screenAbsolute);
    InputSimulator::mouseClick(target.widget, InputSimulator::MouseButton::Middle, target.localPos);

    trackPosition(window, x, y, screenAbsolute);

    QJsonObject result;
    result[QStringLiteral("success")] = true;
    maybeAddScreenshot(result, p, window);
    return envelopeToString(ResponseEnvelope::wrap(result));
  });

  // cu.doubleClick - double click at coordinates
  m_handler->RegisterMethod(QStringLiteral("cu.doubleClick"), [](const QString& params) -> QString {
    auto p = parseParams(params);
    QWidget* window = getActiveWindow();

    int x = p[QStringLiteral("x")].toInt();
    int y = p[QStringLiteral("y")].toInt();
    bool screenAbsolute = p[QStringLiteral("screenAbsolute")].toBool(false);
    int delayMs = p[QStringLiteral("delay_ms")].toInt(0);

    if (delayMs > 0) {
      QThread::msleep(static_cast<unsigned long>(delayMs));
    }

    auto target = resolveWindowCoordinate(window, x, y, screenAbsolute);
    InputSimulator::mouseDoubleClick(target.widget, InputSimulator::MouseButton::Left,
                                     target.localPos);

    trackPosition(window, x, y, screenAbsolute);

    QJsonObject result;
    result[QStringLiteral("success")] = true;
    maybeAddScreenshot(result, p, window);
    return envelopeToString(ResponseEnvelope::wrap(result));
  });

  // cu.mouseMove - move cursor to coordinates
  m_handler->RegisterMethod(QStringLiteral("cu.mouseMove"), [](const QString& params) -> QString {
    auto p = parseParams(params);
    QWidget* window = getActiveWindow();

    int x = p[QStringLiteral("x")].toInt();
    int y = p[QStringLiteral("y")].toInt();
    bool screenAbsolute = p[QStringLiteral("screenAbsolute")].toBool(false);

    if (screenAbsolute) {
      QCursor::setPos(QPoint(x, y));
    } else {
      auto target = resolveWindowCoordinate(window, x, y, false);
      InputSimulator::mouseMove(target.widget, target.localPos);
    }

    trackPosition(window, x, y, screenAbsolute);

    QJsonObject result;
    result[QStringLiteral("success")] = true;
    maybeAddScreenshot(result, p, window);
    return envelopeToString(ResponseEnvelope::wrap(result));
  });

  // cu.drag - drag from start to end coordinates
  m_handler->RegisterMethod(QStringLiteral("cu.drag"), [](const QString& params) -> QString {
    auto p = parseParams(params);
    QWidget* window = getActiveWindow();

    int startX = p[QStringLiteral("startX")].toInt();
    int startY = p[QStringLiteral("startY")].toInt();
    int endX = p[QStringLiteral("endX")].toInt();
    int endY = p[QStringLiteral("endY")].toInt();
    bool screenAbsolute = p[QStringLiteral("screenAbsolute")].toBool(false);

    QPoint startPos, endPos;
    if (screenAbsolute) {
      // Convert screen coords to window-relative for mouseDrag
      startPos = window->mapFromGlobal(QPoint(startX, startY));
      endPos = window->mapFromGlobal(QPoint(endX, endY));
    } else {
      startPos = QPoint(startX, startY);
      endPos = QPoint(endX, endY);
    }

    // Bounds-check both coordinates
    QSize winSize = window->size();
    auto checkBounds = [&](const QPoint& pt, const QString& label) {
      if (pt.x() < 0 || pt.y() < 0 || pt.x() >= winSize.width() || pt.y() >= winSize.height()) {
        throw JsonRpcException(
            ErrorCode::kCoordinateOutOfBounds,
            QStringLiteral("%1 coordinates (%2, %3) out of bounds for window size (%4 x %5)")
                .arg(label)
                .arg(pt.x())
                .arg(pt.y())
                .arg(winSize.width())
                .arg(winSize.height()),
            QJsonObject{{QStringLiteral("x"), pt.x()},
                        {QStringLiteral("y"), pt.y()},
                        {QStringLiteral("which"), label}});
      }
    };
    checkBounds(startPos, QStringLiteral("start"));
    checkBounds(endPos, QStringLiteral("end"));

    InputSimulator::mouseDrag(window, startPos, endPos, InputSimulator::MouseButton::Left);

    // Track the END position (where the cursor ends up after drag)
    trackPosition(window, endX, endY, screenAbsolute);

    QJsonObject result;
    result[QStringLiteral("success")] = true;
    maybeAddScreenshot(result, p, window);
    return envelopeToString(ResponseEnvelope::wrap(result));
  });

  // cu.mouseDown - press mouse button at coordinates
  m_handler->RegisterMethod(QStringLiteral("cu.mouseDown"), [](const QString& params) -> QString {
    auto p = parseParams(params);
    QWidget* window = getActiveWindow();

    int x = p[QStringLiteral("x")].toInt();
    int y = p[QStringLiteral("y")].toInt();
    bool screenAbsolute = p[QStringLiteral("screenAbsolute")].toBool(false);
    QString buttonStr = p[QStringLiteral("button")].toString(QStringLiteral("left"));

    auto target = resolveWindowCoordinate(window, x, y, screenAbsolute);
    InputSimulator::mousePress(target.widget, parseMouseButton(buttonStr), target.localPos);

    trackPosition(window, x, y, screenAbsolute);

    QJsonObject result;
    result[QStringLiteral("success")] = true;
    maybeAddScreenshot(result, p, window);
    return envelopeToString(ResponseEnvelope::wrap(result));
  });

  // cu.mouseUp - release mouse button at coordinates
  m_handler->RegisterMethod(QStringLiteral("cu.mouseUp"), [](const QString& params) -> QString {
    auto p = parseParams(params);
    QWidget* window = getActiveWindow();

    int x = p[QStringLiteral("x")].toInt();
    int y = p[QStringLiteral("y")].toInt();
    bool screenAbsolute = p[QStringLiteral("screenAbsolute")].toBool(false);
    QString buttonStr = p[QStringLiteral("button")].toString(QStringLiteral("left"));

    auto target = resolveWindowCoordinate(window, x, y, screenAbsolute);
    InputSimulator::mouseRelease(target.widget, parseMouseButton(buttonStr), target.localPos);

    trackPosition(window, x, y, screenAbsolute);

    QJsonObject result;
    result[QStringLiteral("success")] = true;
    maybeAddScreenshot(result, p, window);
    return envelopeToString(ResponseEnvelope::wrap(result));
  });
}

// ============================================================================
// Keyboard methods: cu.type, cu.key
// ============================================================================

void ComputerUseModeApi::registerKeyboardMethods() {
  // cu.type - type text at focused widget
  m_handler->RegisterMethod(QStringLiteral("cu.type"), [](const QString& params) -> QString {
    auto p = parseParams(params);
    QString text = p[QStringLiteral("text")].toString();

    if (text.isEmpty()) {
      throw JsonRpcException(JsonRpcError::kInvalidParams,
                             QStringLiteral("Missing required parameter: text"),
                             QJsonObject{{QStringLiteral("method"), QStringLiteral("cu.type")}});
    }

    QWidget* focusWidget = QApplication::focusWidget();
    if (!focusWidget) {
      throw JsonRpcException(
          ErrorCode::kNoFocusedWidget, QStringLiteral("No widget has keyboard focus"),
          QJsonObject{{QStringLiteral("hint"),
                       QStringLiteral("Click on a widget first to give it focus")}});
    }

    InputSimulator::sendText(focusWidget, text);

    QJsonObject result;
    result[QStringLiteral("success")] = true;

    // For include_screenshot, need a window
    if (p[QStringLiteral("include_screenshot")].toBool(false)) {
      QWidget* window = focusWidget->window();
      if (window) {
        QByteArray base64 = Screenshot::captureWindowLogical(window);
        result[QStringLiteral("screenshot")] = QString::fromLatin1(base64);
      }
    }

    return envelopeToString(ResponseEnvelope::wrap(result));
  });

  // cu.key - send key combination at focused widget
  m_handler->RegisterMethod(QStringLiteral("cu.key"), [](const QString& params) -> QString {
    auto p = parseParams(params);
    QString keyStr = p[QStringLiteral("key")].toString();

    if (keyStr.isEmpty()) {
      throw JsonRpcException(JsonRpcError::kInvalidParams,
                             QStringLiteral("Missing required parameter: key"),
                             QJsonObject{{QStringLiteral("method"), QStringLiteral("cu.key")}});
    }

    QWidget* focusWidget = QApplication::focusWidget();
    if (!focusWidget) {
      throw JsonRpcException(
          ErrorCode::kNoFocusedWidget, QStringLiteral("No widget has keyboard focus"),
          QJsonObject{{QStringLiteral("hint"),
                       QStringLiteral("Click on a widget first to give it focus")}});
    }

    KeyCombo combo = KeyNameMapper::parseKeyCombo(keyStr);
    if (combo.key == Qt::Key_unknown) {
      throw JsonRpcException(
          ErrorCode::kKeyParseError,
          QStringLiteral("Failed to parse key combination: %1").arg(keyStr),
          QJsonObject{
              {QStringLiteral("key"), keyStr},
              {QStringLiteral("hint"),
               QStringLiteral("Use Chrome-style key names: ctrl+shift+s, Enter, ArrowUp, etc.")}});
    }

    InputSimulator::sendKey(focusWidget, combo.key, combo.modifiers);

    QJsonObject result;
    result[QStringLiteral("success")] = true;

    if (p[QStringLiteral("include_screenshot")].toBool(false)) {
      QWidget* window = focusWidget->window();
      if (window) {
        QByteArray base64 = Screenshot::captureWindowLogical(window);
        result[QStringLiteral("screenshot")] = QString::fromLatin1(base64);
      }
    }

    return envelopeToString(ResponseEnvelope::wrap(result));
  });
}

// ============================================================================
// Scroll: cu.scroll
// ============================================================================

void ComputerUseModeApi::registerScrollMethod() {
  m_handler->RegisterMethod(QStringLiteral("cu.scroll"), [](const QString& params) -> QString {
    auto p = parseParams(params);
    QWidget* window = getActiveWindow();

    int x = p[QStringLiteral("x")].toInt();
    int y = p[QStringLiteral("y")].toInt();
    bool screenAbsolute = p[QStringLiteral("screenAbsolute")].toBool(false);
    QString direction = p[QStringLiteral("direction")].toString();
    int amount = p[QStringLiteral("amount")].toInt(3);

    if (direction.isEmpty()) {
      throw JsonRpcException(JsonRpcError::kInvalidParams,
                             QStringLiteral("Missing required parameter: direction"),
                             QJsonObject{{QStringLiteral("method"), QStringLiteral("cu.scroll")}});
    }

    auto target = resolveWindowCoordinate(window, x, y, screenAbsolute);

    // Map direction to dx/dy
    // Positive dy = scroll up (content moves down) in Qt's angleDelta convention
    int dx = 0, dy = 0;
    if (direction == QStringLiteral("up")) {
      dy = amount;
    } else if (direction == QStringLiteral("down")) {
      dy = -amount;
    } else if (direction == QStringLiteral("left")) {
      dx = -amount;
    } else if (direction == QStringLiteral("right")) {
      dx = amount;
    } else {
      throw JsonRpcException(
          JsonRpcError::kInvalidParams,
          QStringLiteral("Invalid direction: %1 (expected: up, down, left, right)").arg(direction),
          QJsonObject{{QStringLiteral("direction"), direction},
                      {QStringLiteral("method"), QStringLiteral("cu.scroll")}});
    }

    InputSimulator::scroll(target.widget, target.localPos, dx, dy);

    trackPosition(window, x, y, screenAbsolute);

    QJsonObject result;
    result[QStringLiteral("success")] = true;
    maybeAddScreenshot(result, p, window);
    return envelopeToString(ResponseEnvelope::wrap(result));
  });
}

// ============================================================================
// Query: cu.cursorPosition
// ============================================================================

void ComputerUseModeApi::registerQueryMethods() {
  m_handler->RegisterMethod(
      QStringLiteral("cu.cursorPosition"), [](const QString& /*params*/) -> QString {
        QPoint globalPos;
        bool isVirtual = false;

        if (s_hasSimulatedPosition) {
          globalPos = s_lastSimulatedPosition;
          isVirtual = true;
        } else {
          globalPos = QCursor::pos();
        }

        QWidget* window = getActiveWindow();
        QPoint windowPos = window->mapFromGlobal(globalPos);

        QString widgetId = HitTest::widgetIdAt(globalPos);

        QWidget* widgetAtPos = QApplication::widgetAt(globalPos);
        QString className;
        if (widgetAtPos) {
          className = QString::fromUtf8(widgetAtPos->metaObject()->className());
        }

        QJsonObject result;
        result[QStringLiteral("x")] = windowPos.x();
        result[QStringLiteral("y")] = windowPos.y();
        result[QStringLiteral("screenX")] = globalPos.x();
        result[QStringLiteral("screenY")] = globalPos.y();
        result[QStringLiteral("widgetId")] = widgetId;
        result[QStringLiteral("className")] = className;
        result[QStringLiteral("virtual")] = isVirtual;

        return envelopeToString(ResponseEnvelope::wrap(result));
      });
}

}  // namespace qtPilot
