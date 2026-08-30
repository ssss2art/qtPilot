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
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QThread>
#include <QWidget>
#include <QWindow>

namespace qtPilot {

// ============================================================================
// Internal helpers (file-scope, not in header)
// ============================================================================

namespace {

/// @brief Parse JSON params string into QJsonObject.
QJsonObject parseParams(const QString& params) {
  if (params.isEmpty()) {
    return QJsonObject();
  }
  const QJsonDocument doc = QJsonDocument::fromJson(params.toUtf8());
  return doc.isObject() ? doc.object() : QJsonObject();
}

/// @brief Serialize a response envelope to compact JSON string.
QString envelopeToString(const QJsonObject& envelope) {
  return QString::fromUtf8(QJsonDocument(envelope).toJson(QJsonDocument::Compact));
}

/// @brief A resolved computer-use target: either a QWidget (Widgets app) or a
///        top-level QWindow / QQuickWindow (pure Qt Quick app).
struct CuTarget {
  QWidget* widget = nullptr;
  // QPointer so a window destroyed mid-action (e.g. a click that closes it,
  // observed across a processEvents turn) is seen as null rather than dangling.
  QPointer<QWindow> window;
  bool isWindow() const { return !window.isNull(); }
};

/// @brief Resolve the active target: a top-level QWidget or QWindow/QQuickWindow.
///
/// A focused real window (e.g. a QQuickWindow) is preferred first — this handles
/// hybrid apps where a QApplication hosts a focused QQuickWindow — but the
/// internal QWidgetWindow backing store of a Widgets app is skipped so those
/// apps still resolve to their QWidget (which has childAt hit resolution). The
/// QApplication cast guards the Widgets-only statics under a pure QGuiApplication.
/// @throws JsonRpcException if no active window found.
CuTarget getActiveTarget() {
  auto* coreApp = QCoreApplication::instance();
  auto* guiApp = qobject_cast<QGuiApplication*>(coreApp);

  if (guiApp) {
    if (QWindow* focus = guiApp->focusWindow()) {
      if (focus->isVisible() && !focus->inherits("QWidgetWindow"))
        return {nullptr, focus};
    }
  }

  if (qobject_cast<QApplication*>(coreApp)) {
    if (QWidget* window = QApplication::activeWindow())
      return {window, nullptr};
    const auto topLevels = QApplication::topLevelWidgets();
    for (QWidget* w : topLevels) {
      if (w->isVisible())
        return {w, nullptr};
    }
  }

  if (guiApp) {
    const auto windows = guiApp->topLevelWindows();
    for (QWindow* w : windows) {
      if (w->isVisible() && !w->inherits("QWidgetWindow"))
        return {nullptr, w};
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
    // If no widget was found at unscaled coordinates on a High-DPI display,
    // retry scaling physical screen coordinates to logical screen coordinates.
    const qreal dpr = window ? window->devicePixelRatio() : 1.0;
    if (!target && dpr > 1.0) {
      globalPos = QPoint(qRound(x / dpr), qRound(y / dpr));
      target = QApplication::widgetAt(globalPos);
    }
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

/// @brief Resolve a coordinate to a window-local point for a QWindow target.
/// @throws JsonRpcException if window-relative coordinates are out of bounds.
QPoint resolveWindowLocal(QWindow* window, int x, int y, bool screenAbsolute) {
  // Bounds-check the window-local point on BOTH paths.
  QPoint local = screenAbsolute ? window->mapFromGlobal(QPoint(x, y)) : QPoint(x, y);
  const qreal dpr = window->devicePixelRatio();
  const QSize winSize = window->size();

  // If screen-absolute coordinates fall outside bounds on a High-DPI display,
  // retry by scaling physical screen coordinates to logical screen coordinates.
  if (screenAbsolute && dpr > 1.0 &&
      (local.x() < 0 || local.y() < 0 || local.x() >= winSize.width() ||
       local.y() >= winSize.height())) {
    local = window->mapFromGlobal(QPoint(qRound(x / dpr), qRound(y / dpr)));
  }

  if (local.x() < 0 || local.y() < 0 || local.x() >= winSize.width() ||
      local.y() >= winSize.height()) {
    throw JsonRpcException(
        ErrorCode::kCoordinateOutOfBounds,
        QStringLiteral("Coordinates (%1, %2) out of bounds for window size (%3 x %4)")
            .arg(local.x())
            .arg(local.y())
            .arg(winSize.width())
            .arg(winSize.height()),
        QJsonObject{{QStringLiteral("x"), local.x()},
                    {QStringLiteral("y"), local.y()},
                    {QStringLiteral("windowWidth"), winSize.width()},
                    {QStringLiteral("windowHeight"), winSize.height()}});
  }
  return local;
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
void maybeAddScreenshot(QJsonObject& result, const QJsonObject& params, const CuTarget& t) {
  if (!params[QStringLiteral("include_screenshot")].toBool(false))
    return;
  // The target may have gone away during the action (e.g. a click that closed
  // the window); skip rather than dereference a null target.
  if (!t.isWindow() && !t.widget)
    return;
  QByteArray base64 = t.isWindow() ? Screenshot::captureWindowLogical(t.window)
                                   : Screenshot::captureWindowLogical(t.widget);
  result[QStringLiteral("screenshot")] = QString::fromLatin1(base64);
}

/// @brief Dispatch a click (press+release) to a widget or window target.
void dispatchClick(const CuTarget& t, InputSimulator::MouseButton button, int x, int y, bool sa) {
  if (t.isWindow()) {
    InputSimulator::mouseClick(t.window, button, resolveWindowLocal(t.window, x, y, sa));
  } else {
    auto r = resolveWindowCoordinate(t.widget, x, y, sa);
    InputSimulator::mouseClick(r.widget, button, r.localPos);
  }
}

/// @brief Dispatch a double-click to a widget or window target.
void dispatchDoubleClick(const CuTarget& t, int x, int y, bool sa) {
  if (t.isWindow()) {
    InputSimulator::mouseDoubleClick(t.window, InputSimulator::MouseButton::Left,
                                     resolveWindowLocal(t.window, x, y, sa));
  } else {
    auto r = resolveWindowCoordinate(t.widget, x, y, sa);
    InputSimulator::mouseDoubleClick(r.widget, InputSimulator::MouseButton::Left, r.localPos);
  }
}

/// @brief Dispatch a mouse-button press to a widget or window target.
void dispatchPress(const CuTarget& t, InputSimulator::MouseButton button, int x, int y, bool sa) {
  if (t.isWindow()) {
    InputSimulator::mousePress(t.window, button, resolveWindowLocal(t.window, x, y, sa));
  } else {
    auto r = resolveWindowCoordinate(t.widget, x, y, sa);
    InputSimulator::mousePress(r.widget, button, r.localPos);
  }
}

/// @brief Dispatch a mouse-button release to a widget or window target.
void dispatchRelease(const CuTarget& t, InputSimulator::MouseButton button, int x, int y, bool sa) {
  if (t.isWindow()) {
    InputSimulator::mouseRelease(t.window, button, resolveWindowLocal(t.window, x, y, sa));
  } else {
    auto r = resolveWindowCoordinate(t.widget, x, y, sa);
    InputSimulator::mouseRelease(r.widget, button, r.localPos);
  }
}

/// @brief Dispatch a scroll to a widget or window target.
void dispatchScroll(const CuTarget& t, int x, int y, bool sa, int dx, int dy) {
  if (t.isWindow()) {
    InputSimulator::scroll(t.window, resolveWindowLocal(t.window, x, y, sa), dx, dy);
  } else {
    auto r = resolveWindowCoordinate(t.widget, x, y, sa);
    InputSimulator::scroll(r.widget, r.localPos, dx, dy);
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
/// @param t The active target (for mapToGlobal when using window-relative coords).
/// @param x X coordinate as provided by the caller.
/// @param y Y coordinate as provided by the caller.
/// @param screenAbsolute If true, x/y are already screen-absolute.
void trackPosition(const CuTarget& t, int x, int y, bool screenAbsolute) {
  if (screenAbsolute) {
    s_lastSimulatedPosition = QPoint(x, y);
  } else if (t.isWindow()) {
    s_lastSimulatedPosition = t.window->mapToGlobal(QPoint(x, y));
  } else if (t.widget) {
    s_lastSimulatedPosition = t.widget->mapToGlobal(QPoint(x, y));
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
    CuTarget t = getActiveTarget();

    bool fullScreen = p[QStringLiteral("fullScreen")].toBool(false);
    bool physicalPixels = p[QStringLiteral("physicalPixels")].toBool(false);
    QJsonObject region = p[QStringLiteral("region")].toObject();

    QByteArray base64;
    if (fullScreen) {
      base64 =
          t.isWindow() ? Screenshot::captureScreen(t.window) : Screenshot::captureScreen(t.widget);
    } else if (!region.isEmpty()) {
      QRect rect(region[QStringLiteral("x")].toInt(), region[QStringLiteral("y")].toInt(),
                 region[QStringLiteral("width")].toInt(), region[QStringLiteral("height")].toInt());
      base64 = t.isWindow() ? Screenshot::captureRegion(t.window, rect)
                            : Screenshot::captureRegion(t.widget, rect);
    } else if (physicalPixels) {
      base64 =
          t.isWindow() ? Screenshot::captureWindow(t.window) : Screenshot::captureWindow(t.widget);
    } else {
      // Default: logical pixel capture (1:1 coordinate matching)
      base64 = t.isWindow() ? Screenshot::captureWindowLogical(t.window)
                            : Screenshot::captureWindowLogical(t.widget);
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
    CuTarget t = getActiveTarget();

    int x = p[QStringLiteral("x")].toInt();
    int y = p[QStringLiteral("y")].toInt();
    bool screenAbsolute = p[QStringLiteral("screenAbsolute")].toBool(false);
    int delayMs = p[QStringLiteral("delay_ms")].toInt(0);
    QString buttonStr = p[QStringLiteral("button")].toString(QStringLiteral("left"));

    if (delayMs > 0) {
      QThread::msleep(static_cast<unsigned long>(delayMs));
    }

    dispatchClick(t, parseMouseButton(buttonStr), x, y, screenAbsolute);

    trackPosition(t, x, y, screenAbsolute);

    QJsonObject result;
    result[QStringLiteral("success")] = true;
    maybeAddScreenshot(result, p, t);
    return envelopeToString(ResponseEnvelope::wrap(result));
  });

  // cu.rightClick - right click at coordinates
  m_handler->RegisterMethod(QStringLiteral("cu.rightClick"), [](const QString& params) -> QString {
    auto p = parseParams(params);
    CuTarget t = getActiveTarget();

    int x = p[QStringLiteral("x")].toInt();
    int y = p[QStringLiteral("y")].toInt();
    bool screenAbsolute = p[QStringLiteral("screenAbsolute")].toBool(false);
    int delayMs = p[QStringLiteral("delay_ms")].toInt(0);

    if (delayMs > 0) {
      QThread::msleep(static_cast<unsigned long>(delayMs));
    }

    dispatchClick(t, InputSimulator::MouseButton::Right, x, y, screenAbsolute);

    trackPosition(t, x, y, screenAbsolute);

    QJsonObject result;
    result[QStringLiteral("success")] = true;
    maybeAddScreenshot(result, p, t);
    return envelopeToString(ResponseEnvelope::wrap(result));
  });

  // cu.middleClick - middle click at coordinates
  m_handler->RegisterMethod(QStringLiteral("cu.middleClick"), [](const QString& params) -> QString {
    auto p = parseParams(params);
    CuTarget t = getActiveTarget();

    int x = p[QStringLiteral("x")].toInt();
    int y = p[QStringLiteral("y")].toInt();
    bool screenAbsolute = p[QStringLiteral("screenAbsolute")].toBool(false);
    int delayMs = p[QStringLiteral("delay_ms")].toInt(0);

    if (delayMs > 0) {
      QThread::msleep(static_cast<unsigned long>(delayMs));
    }

    dispatchClick(t, InputSimulator::MouseButton::Middle, x, y, screenAbsolute);

    trackPosition(t, x, y, screenAbsolute);

    QJsonObject result;
    result[QStringLiteral("success")] = true;
    maybeAddScreenshot(result, p, t);
    return envelopeToString(ResponseEnvelope::wrap(result));
  });

  // cu.doubleClick - double click at coordinates
  m_handler->RegisterMethod(QStringLiteral("cu.doubleClick"), [](const QString& params) -> QString {
    auto p = parseParams(params);
    CuTarget t = getActiveTarget();

    int x = p[QStringLiteral("x")].toInt();
    int y = p[QStringLiteral("y")].toInt();
    bool screenAbsolute = p[QStringLiteral("screenAbsolute")].toBool(false);
    int delayMs = p[QStringLiteral("delay_ms")].toInt(0);

    if (delayMs > 0) {
      QThread::msleep(static_cast<unsigned long>(delayMs));
    }

    dispatchDoubleClick(t, x, y, screenAbsolute);

    trackPosition(t, x, y, screenAbsolute);

    QJsonObject result;
    result[QStringLiteral("success")] = true;
    maybeAddScreenshot(result, p, t);
    return envelopeToString(ResponseEnvelope::wrap(result));
  });

  // cu.mouseMove - move cursor to coordinates
  m_handler->RegisterMethod(QStringLiteral("cu.mouseMove"), [](const QString& params) -> QString {
    auto p = parseParams(params);
    CuTarget t = getActiveTarget();

    int x = p[QStringLiteral("x")].toInt();
    int y = p[QStringLiteral("y")].toInt();
    bool screenAbsolute = p[QStringLiteral("screenAbsolute")].toBool(false);

    if (screenAbsolute) {
      QCursor::setPos(QPoint(x, y));
    } else if (t.isWindow()) {
      InputSimulator::mouseMove(t.window, resolveWindowLocal(t.window, x, y, false));
    } else {
      auto target = resolveWindowCoordinate(t.widget, x, y, false);
      InputSimulator::mouseMove(target.widget, target.localPos);
    }

    trackPosition(t, x, y, screenAbsolute);

    QJsonObject result;
    result[QStringLiteral("success")] = true;
    maybeAddScreenshot(result, p, t);
    return envelopeToString(ResponseEnvelope::wrap(result));
  });

  // cu.drag - drag from start to end coordinates
  m_handler->RegisterMethod(QStringLiteral("cu.drag"), [](const QString& params) -> QString {
    auto p = parseParams(params);
    CuTarget t = getActiveTarget();

    int startX = p[QStringLiteral("startX")].toInt();
    int startY = p[QStringLiteral("startY")].toInt();
    int endX = p[QStringLiteral("endX")].toInt();
    int endY = p[QStringLiteral("endY")].toInt();
    bool screenAbsolute = p[QStringLiteral("screenAbsolute")].toBool(false);

    if (t.isWindow()) {
      // resolveWindowLocal bounds-checks and maps screen-absolute coords.
      QPoint startPos = resolveWindowLocal(t.window, startX, startY, screenAbsolute);
      QPoint endPos = resolveWindowLocal(t.window, endX, endY, screenAbsolute);
      InputSimulator::mouseDrag(t.window, startPos, endPos, InputSimulator::MouseButton::Left);
    } else {
      QWidget* window = t.widget;
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
    }

    // Track the END position (where the cursor ends up after drag)
    trackPosition(t, endX, endY, screenAbsolute);

    QJsonObject result;
    result[QStringLiteral("success")] = true;
    maybeAddScreenshot(result, p, t);
    return envelopeToString(ResponseEnvelope::wrap(result));
  });

  // cu.mouseDown - press mouse button at coordinates
  m_handler->RegisterMethod(QStringLiteral("cu.mouseDown"), [](const QString& params) -> QString {
    auto p = parseParams(params);
    CuTarget t = getActiveTarget();

    int x = p[QStringLiteral("x")].toInt();
    int y = p[QStringLiteral("y")].toInt();
    bool screenAbsolute = p[QStringLiteral("screenAbsolute")].toBool(false);
    QString buttonStr = p[QStringLiteral("button")].toString(QStringLiteral("left"));

    dispatchPress(t, parseMouseButton(buttonStr), x, y, screenAbsolute);

    trackPosition(t, x, y, screenAbsolute);

    QJsonObject result;
    result[QStringLiteral("success")] = true;
    maybeAddScreenshot(result, p, t);
    return envelopeToString(ResponseEnvelope::wrap(result));
  });

  // cu.mouseUp - release mouse button at coordinates
  m_handler->RegisterMethod(QStringLiteral("cu.mouseUp"), [](const QString& params) -> QString {
    auto p = parseParams(params);
    CuTarget t = getActiveTarget();

    int x = p[QStringLiteral("x")].toInt();
    int y = p[QStringLiteral("y")].toInt();
    bool screenAbsolute = p[QStringLiteral("screenAbsolute")].toBool(false);
    QString buttonStr = p[QStringLiteral("button")].toString(QStringLiteral("left"));

    dispatchRelease(t, parseMouseButton(buttonStr), x, y, screenAbsolute);

    trackPosition(t, x, y, screenAbsolute);

    QJsonObject result;
    result[QStringLiteral("success")] = true;
    maybeAddScreenshot(result, p, t);
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

    // Widget apps route to the focused QWidget; pure Qt Quick apps have no
    // focus QWidget, so fall back to the active window (QQuickWindow forwards
    // key events to its focused item).
    QWidget* focusWidget = QApplication::focusWidget();
    CuTarget t;
    if (focusWidget) {
      InputSimulator::sendText(focusWidget, text);
      t.widget = focusWidget->window();
    } else {
      t = getActiveTarget();
      // getActiveTarget() resolves EITHER a QWidget (Widgets app) or a QWindow.
      // Testing only isWindow() discarded a perfectly good widget target and
      // failed the call — the state every app is in before anything is clicked.
      if (t.widget) {
        InputSimulator::sendText(t.widget, text);
      } else if (t.isWindow()) {
        InputSimulator::sendText(t.window, text);
      } else {
        throw JsonRpcException(
            ErrorCode::kNoFocusedWidget,
            QStringLiteral("No focusable target: the application has no active window and no "
                           "visible top-level widget"),
            QJsonObject{{QStringLiteral("hint"),
                         QStringLiteral("Show a window first, or give a specific widget focus "
                                        "with qt.methods.invoke {method: \"setFocus\"}")}});
      }
    }

    QJsonObject result;
    result[QStringLiteral("success")] = true;
    maybeAddScreenshot(result, p, t);
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

    KeyCombo combo = KeyNameMapper::parseKeyCombo(keyStr);
    if (combo.key == Qt::Key_unknown) {
      throw JsonRpcException(
          ErrorCode::kKeyParseError,
          QStringLiteral("Failed to parse key combination: %1").arg(keyStr),
          QJsonObject{{QStringLiteral("key"), keyStr},
                      {QStringLiteral("hint"),
                       QStringLiteral("Use named keys and '+' separators: ctrl+shift+s, meta+Plus, "
                                      "QuestionMark, Enter, ArrowUp, etc.")}});
    }

    // Widget apps route to the focused QWidget; pure Qt Quick apps fall back to
    // the active window (QQuickWindow forwards key events to its focused item).
    QWidget* focusWidget = QApplication::focusWidget();
    CuTarget t;
    if (focusWidget) {
      InputSimulator::sendKey(focusWidget, combo.key, combo.modifiers);
      t.widget = focusWidget->window();
    } else {
      t = getActiveTarget();
      // Same as cu.type above: a resolved QWidget target is valid, and rejecting
      // it made every key-driven assertion fail until the caller happened to
      // click something first.
      if (t.widget) {
        InputSimulator::sendKey(t.widget, combo.key, combo.modifiers);
      } else if (t.isWindow()) {
        InputSimulator::sendKey(t.window, combo.key, combo.modifiers);
      } else {
        throw JsonRpcException(
            ErrorCode::kNoFocusedWidget,
            QStringLiteral("No focusable target: the application has no active window and no "
                           "visible top-level widget"),
            QJsonObject{{QStringLiteral("hint"),
                         QStringLiteral("Show a window first, or give a specific widget focus "
                                        "with qt.methods.invoke {method: \"setFocus\"}")}});
      }
    }

    QJsonObject result;
    result[QStringLiteral("success")] = true;
    maybeAddScreenshot(result, p, t);
    return envelopeToString(ResponseEnvelope::wrap(result));
  });
}

// ============================================================================
// Scroll: cu.scroll
// ============================================================================

void ComputerUseModeApi::registerScrollMethod() {
  m_handler->RegisterMethod(QStringLiteral("cu.scroll"), [](const QString& params) -> QString {
    auto p = parseParams(params);
    CuTarget t = getActiveTarget();

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

    dispatchScroll(t, x, y, screenAbsolute, dx, dy);

    trackPosition(t, x, y, screenAbsolute);

    QJsonObject result;
    result[QStringLiteral("success")] = true;
    maybeAddScreenshot(result, p, t);
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

        CuTarget t = getActiveTarget();
        QPoint windowPos;
        QString widgetId;
        QString className;
        if (t.isWindow()) {
          windowPos = t.window->mapFromGlobal(globalPos);
          className = QString::fromUtf8(t.window->metaObject()->className());
        } else {
          windowPos = t.widget->mapFromGlobal(globalPos);
          widgetId = HitTest::widgetIdAt(globalPos);
          QWidget* widgetAtPos = QApplication::widgetAt(globalPos);
          if (widgetAtPos) {
            className = QString::fromUtf8(widgetAtPos->metaObject()->className());
          }
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
