// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "api/native_mode_api.h"

#include "api/error_codes.h"
#include "api/response_envelope.h"
#include "api/symbolic_name_map.h"
#include "core/object_registry.h"
#include "core/object_resolver.h"
#include "core/version.h"
#include "interaction/hit_test.h"
#include "interaction/input_simulator.h"
#include "interaction/item_targeting.h"
#include "interaction/modifier_parser.h"
#include "interaction/screenshot.h"
#include "introspection/event_capture.h"
#include "introspection/meta_inspector.h"
#include "introspection/model_navigator.h"
#include "introspection/object_id.h"
#include "introspection/qml_inspector.h"
#include "introspection/signal_monitor.h"

#include <expected>

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGraphicsObject>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMenu>
#include <QPointer>
#include <QThread>
#include <QTreeView>
#include <QWidget>
#include <QWindow>

#ifdef QTPILOT_HAS_QML
#include <QQuickItem>
#include <QQuickWindow>
#endif

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

/// @brief Monadic resolution of objectId parameter to QObject* using std::expected.
std::expected<QObject*, JsonRpcException> tryResolveObjectParam(const QJsonObject& params,
                                                                const QString& methodName) {
  QString objectId = params[QStringLiteral("objectId")].toString();
  if (objectId.isEmpty()) {
    return std::unexpected(JsonRpcException(JsonRpcError::kInvalidParams,
                                            QStringLiteral("Missing required parameter: objectId"),
                                            QJsonObject{{QStringLiteral("method"), methodName}}));
  }

  return ObjectResolver::resolveExpected(objectId).transform_error(
      [&](const ObjectResolver::ResolveError& err) {
        return JsonRpcException(
            ErrorCode::kObjectNotFound, QStringLiteral("Object not found: %1").arg(err.id),
            QJsonObject{{QStringLiteral("objectId"), err.id},
                        {QStringLiteral("hint"),
                         QStringLiteral(
                             "Use qt.objects.search or qt.objects.tree to discover valid IDs")}});
      });
}

/// @brief Monadic resolution of objectId parameter to QWidget* via .and_then().
std::expected<QWidget*, JsonRpcException> tryResolveWidgetParam(const QJsonObject& params,
                                                                const QString& methodName) {
  return tryResolveObjectParam(params, methodName)
      .and_then([&](QObject* obj) -> std::expected<QWidget*, JsonRpcException> {
        if (auto* widget = qobject_cast<QWidget*>(obj)) {
          return widget;
        }
        QString objectId = params[QStringLiteral("objectId")].toString();
        return std::unexpected(JsonRpcException(
            ErrorCode::kObjectNotWidget, QStringLiteral("Object is not a widget: %1").arg(objectId),
            QJsonObject{
                {QStringLiteral("objectId"), objectId},
                {QStringLiteral("className"), QString::fromUtf8(obj->metaObject()->className())}}));
      });
}

/// @brief Monadic resolution of objectId parameter to QAbstractItemModel* via .and_then().
std::expected<QAbstractItemModel*, JsonRpcException> tryResolveModelParam(
    const QJsonObject& params, const QString& methodName) {
  return tryResolveObjectParam(params, methodName)
      .and_then([&](QObject* obj) -> std::expected<QAbstractItemModel*, JsonRpcException> {
        return ModelNavigator::resolveModelExpected(obj).transform_error([&](const QString&) {
          QString objectId = params[QStringLiteral("objectId")].toString();
          return JsonRpcException(
              ErrorCode::kNotAModel,
              QStringLiteral("Object is not a model and does not have an associated model"),
              QJsonObject{{QStringLiteral("objectId"), objectId},
                          {QStringLiteral("hint"),
                           QStringLiteral("Use qt.models.list to discover available models")}});
        });
      });
}

/// @brief Monadic extraction of a required string parameter.
std::expected<QString, JsonRpcException> requireStringParam(const QJsonObject& params,
                                                            const QString& key,
                                                            const QString& methodName) {
  const QJsonValue val = params.value(key);
  if (!val.isString() || val.toString().isEmpty()) {
    return std::unexpected(JsonRpcException(
        JsonRpcError::kInvalidParams, QStringLiteral("Missing required parameter: %1").arg(key),
        QJsonObject{{QStringLiteral("method"), methodName}}));
  }
  return val.toString();
}

/// @brief Monadic extraction of a required JSON value parameter.
std::expected<QJsonValue, JsonRpcException> requireValueParam(const QJsonObject& params,
                                                              const QString& key,
                                                              const QString& methodName) {
  if (!params.contains(key)) {
    return std::unexpected(JsonRpcException(
        JsonRpcError::kInvalidParams, QStringLiteral("Missing required parameter: %1").arg(key),
        QJsonObject{{QStringLiteral("method"), methodName}}));
  }
  return params.value(key);
}

/// @brief Resolve objectId param to QObject*, throw JsonRpcException on failure.
QObject* resolveObjectParam(const QJsonObject& params, const QString& methodName) {
  auto res = tryResolveObjectParam(params, methodName);
  if (!res) {
    throw res.error();
  }
  return *res;
}

/// @brief Resolve objectId param to QWidget*, throw JsonRpcException on failure.
QWidget* resolveWidgetParam(const QJsonObject& params, const QString& methodName) {
  auto res = tryResolveWidgetParam(params, methodName);
  if (!res) {
    throw res.error();
  }
  return *res;
}

/// @brief Resolve an optional view id param to a QGraphicsView*.
///
/// Absent means "no preference"; present but unusable is a caller error worth
/// reporting rather than silently ignoring.
///
/// The distinction matters more than it looks. toString() yields an empty
/// QString for a number, a bool, null, an object and an array alike, so
/// collapsing "absent" with "present but not a string" would let a mistyped id
/// silently select a different code path -- in qt.ui.hitTest, one that reads
/// x/y in an entirely different coordinate space and answers confidently about
/// the wrong object.
QGraphicsView* resolveViewParam(const QJsonObject& params, const QString& key,
                                const QString& methodName) {
  const QJsonValue raw = params.value(key);
  if (raw.isUndefined() || raw.isNull()) {
    return nullptr;
  }
  if (!raw.isString() || raw.toString().isEmpty()) {
    throw JsonRpcException(
        JsonRpcError::kInvalidParams,
        QStringLiteral("Parameter '%1' must be a non-empty object id string").arg(key),
        QJsonObject{{QStringLiteral("method"), methodName},
                    {key, raw},
                    {QStringLiteral("hint"),
                     QStringLiteral("Use qt.objects.search to discover a QGraphicsView id")}});
  }
  const QString viewId = raw.toString();

  QObject* obj = ObjectResolver::resolve(viewId);
  if (!obj) {
    throw JsonRpcException(ErrorCode::kObjectNotFound,
                           QStringLiteral("Object not found: %1").arg(viewId),
                           QJsonObject{{QStringLiteral("method"), methodName}, {key, viewId}});
  }

  auto* view = qobject_cast<QGraphicsView*>(obj);
  if (!view) {
    throw JsonRpcException(ErrorCode::kNotGraphicsView,
                           QStringLiteral("Object is not a QGraphicsView: %1").arg(viewId),
                           QJsonObject{{QStringLiteral("method"), methodName},
                                       {key, viewId},
                                       {QStringLiteral("className"),
                                        QString::fromUtf8(obj->metaObject()->className())}});
  }
  return view;
}

/// @brief Read a required numeric coordinate from params.
///
/// QJsonValue::toInt() returns 0 for an absent key, a string, a bool, a
/// non-whole double and an out-of-range integer alike. Viewport (0, 0) is the
/// top-left of a rendered scene and very often holds a real item, so silently
/// defaulting turns a malformed request into a confident wrong answer instead
/// of an error. Fractional values are legitimate -- the geometry this API hands
/// back is fractional by design -- so they are rounded, not truncated.
double requireCoordinate(const QJsonObject& params, const QString& key, const QString& methodName) {
  const QJsonValue raw = params.value(key);
  if (!raw.isDouble()) {
    throw JsonRpcException(JsonRpcError::kInvalidParams,
                           QStringLiteral("Parameter '%1' must be a number").arg(key),
                           QJsonObject{{QStringLiteral("method"), methodName}, {key, raw}});
  }
  return raw.toDouble();
}

InputSimulator::MouseButton parseMouseButton(const QString& button) {
  if (button == QStringLiteral("right"))
    return InputSimulator::MouseButton::Right;
  if (button == QStringLiteral("middle"))
    return InputSimulator::MouseButton::Middle;
  return InputSimulator::MouseButton::Left;
}

QGraphicsView* resolveGraphicsItemView(QGraphicsObject* item, QGraphicsView* requestedView,
                                       const QString& methodName) {
  QGraphicsScene* scene = item->scene();
  if (!scene) {
    throw JsonRpcException(
        ErrorCode::kObjectNotFound, QStringLiteral("Graphics item is not in a scene"),
        QJsonObject{{QStringLiteral("method"), methodName},
                    {QStringLiteral("objectId"), ObjectRegistry::instance()->objectId(item)}});
  }

  if (requestedView) {
    if (scene->views().contains(requestedView)) {
      return requestedView;
    }

    QJsonArray candidates;
    const QList<QGraphicsView*> sceneViews = scene->views();
    for (QGraphicsView* view : sceneViews) {
      candidates.append(ObjectRegistry::instance()->objectId(view));
    }
    throw JsonRpcException(
        JsonRpcError::kInvalidParams,
        QStringLiteral("viewObjectId does not render this item's scene"),
        QJsonObject{{QStringLiteral("method"), methodName}, {QStringLiteral("views"), candidates}});
  }

  const QList<QGraphicsView*> sceneViews = scene->views();
  for (QGraphicsView* view : sceneViews) {
    if (view && view->isVisible()) {
      return view;
    }
  }
  for (QGraphicsView* view : sceneViews) {
    if (view) {
      return view;
    }
  }

  throw JsonRpcException(ErrorCode::kObjectNotFound,
                         QStringLiteral("Graphics item has no rendering QGraphicsView"),
                         QJsonObject{{QStringLiteral("method"), methodName}});
}

void queueWidgetClick(QWidget* widget, InputSimulator::MouseButton button, const QPoint& point,
                      bool doubleClick, Qt::KeyboardModifiers modifiers) {
  QPointer<QWidget> safeWidget(widget);
  QMetaObject::invokeMethod(
      widget,
      [safeWidget, button, point, doubleClick, modifiers]() {
        if (!safeWidget) {
          return;
        }
        if (doubleClick) {
          InputSimulator::mouseDoubleClick(safeWidget, button, point, modifiers);
        } else {
          InputSimulator::mouseClick(safeWidget, button, point, modifiers);
        }
      },
      Qt::QueuedConnection);
}

#ifdef QTPILOT_HAS_QML
void queueWindowClick(QWindow* window, InputSimulator::MouseButton button, const QPoint& point,
                      bool doubleClick, Qt::KeyboardModifiers modifiers) {
  QPointer<QWindow> safeWindow(window);
  QMetaObject::invokeMethod(
      window,
      [safeWindow, button, point, doubleClick, modifiers]() {
        if (!safeWindow) {
          return;
        }
        if (doubleClick) {
          InputSimulator::mouseDoubleClick(safeWindow, button, point, modifiers);
        } else {
          InputSimulator::mouseClick(safeWindow, button, point, modifiers);
        }
      },
      Qt::QueuedConnection);
}
#endif

QJsonObject handleUiClickLike(const QJsonObject& params, const QString& methodName,
                              bool doubleClick) {
  QObject* obj = resolveObjectParam(params, methodName);
  const QString objectId = params[QStringLiteral("objectId")].toString();
  const InputSimulator::MouseButton button =
      parseMouseButton(params[QStringLiteral("button")].toString(QStringLiteral("left")));
  const Qt::KeyboardModifiers modifiers =
      ModifierParser::parse(params.value(QStringLiteral("modifiers")), methodName);

  if (auto* item = qobject_cast<QGraphicsObject*>(obj)) {
    QGraphicsView* requestedView =
        resolveViewParam(params, QStringLiteral("viewObjectId"), methodName);
    QGraphicsView* view = resolveGraphicsItemView(item, requestedView, methodName);
    const ItemTargeting::Target target = ItemTargeting::resolve(item, view, params, methodName);
    const QPoint point = target.viewportPoint;
    if (!QRect(QPoint(0, 0), view->viewport()->size()).contains(point)) {
      throw JsonRpcException(
          ErrorCode::kCoordinateOutOfBounds,
          QStringLiteral("Graphics item click point is outside the view viewport"),
          QJsonObject{{QStringLiteral("method"), methodName},
                      {QStringLiteral("objectId"), objectId},
                      {QStringLiteral("viewObjectId"), ObjectRegistry::instance()->objectId(view)},
                      {QStringLiteral("x"), point.x()},
                      {QStringLiteral("y"), point.y()}});
    }

    queueWidgetClick(view->viewport(), button, point, doubleClick, modifiers);
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("deferred"), true},
                       {QStringLiteral("target"), QStringLiteral("graphicsItem")},
                       {QStringLiteral("adjusted"), target.adjusted},
                       {QStringLiteral("viewObjectId"), ObjectRegistry::instance()->objectId(view)},
                       {QStringLiteral("position"), QJsonObject{{QStringLiteral("x"), point.x()},
                                                                {QStringLiteral("y"), point.y()}}}};
  }

#ifdef QTPILOT_HAS_QML
  if (auto* item = qobject_cast<QQuickItem*>(obj)) {
    QQuickWindow* w = item->window();
    if (!w) {
      throw JsonRpcException(
          ErrorCode::kWidgetNotVisible,
          QStringLiteral("QQuickItem is not on a window (not rendered): %1").arg(objectId),
          QJsonObject{{QStringLiteral("objectId"), objectId}});
    }
    QPoint point;
    const QJsonValue rawPosition = params.value(QStringLiteral("position"));
    if (!rawPosition.isUndefined() && !rawPosition.isNull()) {
      if (!rawPosition.isObject()) {
        throw JsonRpcException(
            JsonRpcError::kInvalidParams,
            QStringLiteral("Parameter 'position' must be an object with numeric x/y fields"),
            QJsonObject{{QStringLiteral("method"), methodName},
                        {QStringLiteral("position"), rawPosition}});
      }
      const QJsonObject position = rawPosition.toObject();
      point = QPoint(qRound(requireCoordinate(position, QStringLiteral("x"), methodName)),
                     qRound(requireCoordinate(position, QStringLiteral("y"), methodName)));
    } else {
      point = QPoint(qRound(item->width() / 2.0), qRound(item->height() / 2.0));
    }
    const QPoint scenePos = item->mapToScene(QPointF(point)).toPoint();
    queueWindowClick(w, button, scenePos, doubleClick, modifiers);
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("deferred"), true}};
  }
#endif

  auto* widget = qobject_cast<QWidget*>(obj);
  if (!widget) {
    throw JsonRpcException(
        ErrorCode::kObjectNotWidget, QStringLiteral("Object is not a widget: %1").arg(objectId),
        QJsonObject{
            {QStringLiteral("objectId"), objectId},
            {QStringLiteral("className"), QString::fromUtf8(obj->metaObject()->className())}});
  }

  QPoint point;
  const QJsonValue rawPosition = params.value(QStringLiteral("position"));
  if (!rawPosition.isUndefined() && !rawPosition.isNull()) {
    if (!rawPosition.isObject()) {
      throw JsonRpcException(
          JsonRpcError::kInvalidParams,
          QStringLiteral("Parameter 'position' must be an object with numeric x/y fields"),
          QJsonObject{{QStringLiteral("method"), methodName},
                      {QStringLiteral("position"), rawPosition}});
    }
    const QJsonObject position = rawPosition.toObject();
    point = QPoint(qRound(requireCoordinate(position, QStringLiteral("x"), methodName)),
                   qRound(requireCoordinate(position, QStringLiteral("y"), methodName)));
  }

  queueWidgetClick(widget, button, point, doubleClick, modifiers);
  return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("deferred"), true}};
}

/// @brief The context menu currently on screen, or nullptr.
///
/// A popped-up QMenu is the active popup widget; when several are stacked (a
/// submenu over its parent) the active one is the innermost, which is what a
/// caller is looking at.
QMenu* activeContextMenu() {
  if (auto* popup = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
    return popup;
  }
  const QWidgetList widgets = QApplication::topLevelWidgets();
  for (QWidget* widget : widgets) {
    if (auto* menu = qobject_cast<QMenu*>(widget)) {
      if (menu->isVisible()) {
        return menu;
      }
    }
  }
  return nullptr;
}

/// @brief The open menu, or a structured "nothing is open" error.
/// @brief An optional boolean parameter: @p fallback when absent, an error when
/// present but not a boolean. A string "false" read as true would be the worst
/// kind of wrong for a switch like 'deferred'.
bool optionalBool(const QJsonObject& params, const QString& key, bool fallback,
                  const QString& method) {
  const QJsonValue value = params.value(key);
  if (value.isUndefined() || value.isNull()) {
    return fallback;
  }
  if (!value.isBool()) {
    throw JsonRpcException(
        JsonRpcError::kInvalidParams, QStringLiteral("Parameter '%1' must be a boolean").arg(key),
        QJsonObject{{QStringLiteral("method"), method}, {QStringLiteral("parameter"), key}});
  }
  return value.toBool();
}

QMenu* requireActiveMenu(const QString& methodName) {
  QMenu* menu = activeContextMenu();
  if (!menu) {
    throw JsonRpcException(ErrorCode::kNoActiveMenu, QStringLiteral("No context menu is open"),
                           QJsonObject{{QStringLiteral("method"), methodName}});
  }
  return menu;
}

/// @brief Describe a menu's entries in the order they are shown.
QJsonArray describeMenuItems(QMenu* menu) {
  QJsonArray items;
  const QList<QAction*> actions = menu->actions();
  for (QAction* action : actions) {
    QJsonObject entry;
    entry[QStringLiteral("text")] = action->text();
    entry[QStringLiteral("enabled")] = action->isEnabled();
    entry[QStringLiteral("visible")] = action->isVisible();
    entry[QStringLiteral("checkable")] = action->isCheckable();
    entry[QStringLiteral("checked")] = action->isChecked();
    entry[QStringLiteral("separator")] = action->isSeparator();
    entry[QStringLiteral("hasSubmenu")] = action->menu() != nullptr;
    entry[QStringLiteral("objectId")] = ObjectRegistry::instance()->objectId(action);
    items.append(entry);
  }
  return items;
}

/// @brief Run @p prepared on @p obj from the event loop, after this request returns.
///
/// Queued on @p obj itself, so Qt discards the call if @p obj is destroyed first.
/// Errors cannot reach the caller any more; they are logged.
void queueInvocation(QObject* obj, PreparedInvocation prepared) {
  QMetaObject::invokeMethod(
      obj,
      [obj, prepared = std::move(prepared)]() {
        const auto outcome = prepared.invoke(obj);
        if (!outcome) {
          qWarning().noquote() << "[qtPilot] deferred qt.methods.invoke did not run:"
                               << outcome.error().message;
        }
      },
      Qt::QueuedConnection);
}

/// @brief Post a context-menu event to @p widget at @p point.
///
/// Posted rather than sent: a handler that answers with QMenu::exec() spins its
/// own event loop and would not return until the menu closed, with the RPC
/// handler -- which runs on the GUI thread -- still inside the call.
void queueContextMenuEvent(QWidget* widget, const QPoint& point) {
  QPointer<QWidget> safeWidget(widget);
  QMetaObject::invokeMethod(
      widget,
      [safeWidget, point]() {
        if (!safeWidget) {
          return;
        }
        QContextMenuEvent event(QContextMenuEvent::Mouse, point, safeWidget->mapToGlobal(point));
        QCoreApplication::sendEvent(safeWidget, &event);
      },
      Qt::QueuedConnection);
}

QJsonObject handleUiContextMenu(const QJsonObject& params) {
  const QString kMethod = QStringLiteral("qt.ui.contextMenu");
  QObject* obj = resolveObjectParam(params, kMethod);
  const QString objectId = params[QStringLiteral("objectId")].toString();

  if (auto* item = qobject_cast<QGraphicsObject*>(obj)) {
    QGraphicsView* requestedView =
        resolveViewParam(params, QStringLiteral("viewObjectId"), kMethod);
    QGraphicsView* view = resolveGraphicsItemView(item, requestedView, kMethod);
    const ItemTargeting::Target target = ItemTargeting::resolve(item, view, params, kMethod);

    queueContextMenuEvent(view->viewport(), target.viewportPoint);
    return QJsonObject{
        {QStringLiteral("ok"), true},
        {QStringLiteral("deferred"), true},
        {QStringLiteral("target"), QStringLiteral("graphicsItem")},
        {QStringLiteral("adjusted"), target.adjusted},
        {QStringLiteral("viewObjectId"), ObjectRegistry::instance()->objectId(view)},
        {QStringLiteral("position"), QJsonObject{{QStringLiteral("x"), target.viewportPoint.x()},
                                                 {QStringLiteral("y"), target.viewportPoint.y()}}}};
  }

  auto* widget = qobject_cast<QWidget*>(obj);
  if (!widget) {
    throw JsonRpcException(
        ErrorCode::kObjectNotWidget, QStringLiteral("Object is not a widget: %1").arg(objectId),
        QJsonObject{
            {QStringLiteral("objectId"), objectId},
            {QStringLiteral("className"), QString::fromUtf8(obj->metaObject()->className())}});
  }

  QPoint point = widget->rect().center();
  const QJsonValue rawPosition = params.value(QStringLiteral("position"));
  if (rawPosition.isObject()) {
    const QJsonObject position = rawPosition.toObject();
    point = QPoint(qRound(requireCoordinate(position, QStringLiteral("x"), kMethod)),
                   qRound(requireCoordinate(position, QStringLiteral("y"), kMethod)));
  }

  queueContextMenuEvent(widget, point);
  return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("deferred"), true}};
}

QJsonObject handleUiActiveMenu() {
  QMenu* menu = requireActiveMenu(QStringLiteral("qt.ui.activeMenu"));
  return QJsonObject{{QStringLiteral("objectId"), ObjectRegistry::instance()->objectId(menu)},
                     {QStringLiteral("title"), menu->title()},
                     {QStringLiteral("items"), describeMenuItems(menu)}};
}

/// @brief Choose @p action in @p menu the way a user does: make it the current
/// entry and press Return.
///
/// Not QAction::trigger(). That fires the action's triggered() signal, but the
/// menu itself never learns an entry was chosen, so a QMenu::exec() caller --
/// the usual way to run a context menu -- gets nullptr back and does nothing.
void chooseMenuEntry(QMenu* menu, QAction* action) {
  menu->setActiveAction(action);
  QKeyEvent press(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
  QCoreApplication::sendEvent(menu, &press);
}

/// @brief Whether @p action can still be chosen from @p menu, as a user could.
std::expected<void, QString> stillChoosable(const QPointer<QMenu>& menu,
                                            const QPointer<QAction>& action, const QString& label) {
  if (menu.isNull() || action.isNull()) {
    return std::unexpected(QStringLiteral("the menu for '%1' was destroyed").arg(label));
  }
  if (!menu->isVisible()) {
    return std::unexpected(QStringLiteral("the menu for '%1' is no longer open").arg(label));
  }
  if (!menu->actions().contains(action.data())) {
    return std::unexpected(QStringLiteral("'%1' is no longer in its menu").arg(label));
  }
  if (!action->isEnabled() || !action->isVisible()) {
    return std::unexpected(QStringLiteral("'%1' is no longer enabled").arg(label));
  }
  return {};
}

QJsonObject handleUiActivateMenuItem(const QJsonObject& params) {
  const QString kMethod = QStringLiteral("qt.ui.activateMenuItem");
  const QString text = params.value(QStringLiteral("text")).toString();
  // Deferred unless asked otherwise: choosing an entry runs application code,
  // and a modal dialog opened from it would re-enter the request's dispatch.
  const bool deferred = optionalBool(params, QStringLiteral("deferred"), true, kMethod);
  if (text.isEmpty()) {
    throw JsonRpcException(JsonRpcError::kInvalidParams,
                           QStringLiteral("Parameter 'text' is required"),
                           QJsonObject{{QStringLiteral("method"), kMethod}});
  }

  QMenu* menu = requireActiveMenu(kMethod);
  QJsonArray offered;
  const QList<QAction*> actions = menu->actions();
  for (QAction* action : actions) {
    if (action->isSeparator()) {
      continue;
    }
    // Compare with the mnemonic marker removed, so a caller writes what the
    // entry reads as on screen rather than "&Delete".
    const QString label = action->text();
    QString plain = label;
    plain.remove(QLatin1Char('&'));
    offered.append(label);
    if (label == text || plain == text) {
      if (!action->isEnabled()) {
        throw JsonRpcException(
            ErrorCode::kMenuItemNotFound, QStringLiteral("Menu item '%1' is disabled").arg(text),
            QJsonObject{{QStringLiteral("method"), kMethod}, {QStringLiteral("text"), text}});
      }
      // Choosing an entry that opens a submenu only opens the submenu.
      if (action->menu()) {
        throw JsonRpcException(
            JsonRpcError::kInvalidParams,
            QStringLiteral("Menu item '%1' opens a submenu and cannot be chosen").arg(text),
            QJsonObject{{QStringLiteral("method"), kMethod}, {QStringLiteral("text"), text}});
      }
      // Describe the entry before choosing it: choosing runs application code,
      // which may destroy the menu and every action in it.
      const QJsonObject chosen{
          {QStringLiteral("ok"), true},
          {QStringLiteral("text"), label},
          {QStringLiteral("objectId"), ObjectRegistry::instance()->objectId(action)},
          {QStringLiteral("deferred"), deferred}};
      if (deferred) {
        QPointer<QMenu> safeMenu(menu);
        QPointer<QAction> safeAction(action);
        QMetaObject::invokeMethod(
            menu,
            [safeMenu, safeAction, label]() {
              // Things may have moved on since the reply went out. A user can only
              // choose an enabled entry of a menu that is open, and neither can we.
              const auto choosable = stillChoosable(safeMenu, safeAction, label);
              if (!choosable) {
                qWarning().noquote() << "[qtPilot] deferred qt.ui.activateMenuItem did not run:"
                                     << choosable.error();
                return;
              }
              chooseMenuEntry(safeMenu, safeAction);
            },
            Qt::QueuedConnection);
      } else {
        chooseMenuEntry(menu, action);
      }
      return chosen;
    }
  }

  throw JsonRpcException(ErrorCode::kMenuItemNotFound,
                         QStringLiteral("No menu item labelled '%1'").arg(text),
                         QJsonObject{{QStringLiteral("method"), kMethod},
                                     {QStringLiteral("text"), text},
                                     {QStringLiteral("offered"), offered}});
}

QJsonObject handleUiSendKeys(const QJsonObject& params) {
  QObject* obj = resolveObjectParam(params, QStringLiteral("qt.ui.sendKeys"));
  const QString objectId = params[QStringLiteral("objectId")].toString();
  const QString text = params[QStringLiteral("text")].toString();
  const QString sequence = params[QStringLiteral("sequence")].toString();
  const QJsonValue rawModifiers = params.value(QStringLiteral("modifiers"));

  // A sequence already spells its own modifiers ("Ctrl+S"), so accepting both
  // would leave two sources for one thing and no obvious winner. Refusing the
  // combination keeps the caller's intent unambiguous.
  if (!sequence.isEmpty() && !rawModifiers.isUndefined() && !rawModifiers.isNull()) {
    throw JsonRpcException(
        JsonRpcError::kInvalidParams,
        QStringLiteral("Parameters 'sequence' and 'modifiers' cannot be combined; spell the "
                       "modifiers inside the sequence, or use 'text' with 'modifiers'"),
        QJsonObject{{QStringLiteral("method"), QStringLiteral("qt.ui.sendKeys")},
                    {QStringLiteral("sequence"), sequence},
                    {QStringLiteral("modifiers"), rawModifiers}});
  }

  const Qt::KeyboardModifiers modifiers =
      ModifierParser::parse(rawModifiers, QStringLiteral("qt.ui.sendKeys"));

  if (auto* item = qobject_cast<QGraphicsObject*>(obj)) {
    QGraphicsView* requestedView =
        resolveViewParam(params, QStringLiteral("viewObjectId"), QStringLiteral("qt.ui.sendKeys"));
    QGraphicsView* view =
        resolveGraphicsItemView(item, requestedView, QStringLiteral("qt.ui.sendKeys"));
    item->setFocus(Qt::OtherFocusReason);
    view->viewport()->setFocus(Qt::OtherFocusReason);
    if (!text.isEmpty()) {
      InputSimulator::sendText(view->viewport(), text, modifiers);
    }
    if (!sequence.isEmpty()) {
      InputSimulator::sendKeySequence(view->viewport(), sequence);
    }
    return QJsonObject{
        {QStringLiteral("ok"), true},
        {QStringLiteral("target"), QStringLiteral("graphicsItem")},
        {QStringLiteral("viewObjectId"), ObjectRegistry::instance()->objectId(view)}};
  }

#ifdef QTPILOT_HAS_QML
  if (auto* item = qobject_cast<QQuickItem*>(obj)) {
    QQuickWindow* w = item->window();
    if (!w) {
      throw JsonRpcException(
          ErrorCode::kWidgetNotVisible,
          QStringLiteral("QQuickItem is not on a window (not rendered): %1").arg(objectId),
          QJsonObject{{QStringLiteral("objectId"), objectId}});
    }
    item->forceActiveFocus();
    if (!text.isEmpty()) {
      InputSimulator::sendText(w, text, modifiers);
    }
    if (!sequence.isEmpty()) {
      InputSimulator::sendKeySequence(w, sequence);
    }
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("target"), QStringLiteral("quickItem")}};
  }
#endif

  auto* widget = qobject_cast<QWidget*>(obj);
  if (!widget) {
    throw JsonRpcException(
        ErrorCode::kObjectNotWidget, QStringLiteral("Object is not a widget: %1").arg(objectId),
        QJsonObject{
            {QStringLiteral("objectId"), objectId},
            {QStringLiteral("className"), QString::fromUtf8(obj->metaObject()->className())}});
  }

  if (!text.isEmpty()) {
    InputSimulator::sendText(widget, text, modifiers);
  }
  if (!sequence.isEmpty()) {
    InputSimulator::sendKeySequence(widget, sequence);
  }
  return QJsonObject{{QStringLiteral("ok"), true}};
}

}  // anonymous namespace

// ============================================================================
// Constructor - register all method groups
// ============================================================================

NativeModeApi::NativeModeApi(JsonRpcHandler* handler, QObject* parent)
    : QObject(parent), m_handler(handler) {
  registerSystemMethods();
  registerObjectMethods();
  registerPropertyMethods();
  registerMethodMethods();
  registerSignalMethods();
  registerEventMethods();
  registerUiMethods();
  registerNameMapMethods();
  registerQmlMethods();
  registerModelMethods();
}

// ============================================================================
// System methods: qt.ping, qt.version
// ============================================================================

void NativeModeApi::registerSystemMethods() {
  // qt.ping - liveness check
  m_handler->RegisterMethod(QStringLiteral("qt.ping"), [](const QString& /*params*/) -> QString {
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    QJsonObject result;
    result[QStringLiteral("pong")] = true;
    result[QStringLiteral("timestamp")] = now;
    result[QStringLiteral("eventLoopLatency")] = 0;

    return envelopeToString(ResponseEnvelope::wrap(result));
  });

  // qt.version - version info with deprecation notice
  m_handler->RegisterMethod(QStringLiteral("qt.version"), [](const QString& /*params*/) -> QString {
    QJsonArray deprecated;
    deprecated.append(QStringLiteral("qtpilot.*"));

    QJsonObject result;
    result[QStringLiteral("version")] = QString::fromUtf8(kVersion);
    result[QStringLiteral("protocolVersion")] = kProtocolVersion;
    result[QStringLiteral("protocol")] = QStringLiteral("jsonrpc-2.0");
    result[QStringLiteral("name")] = QStringLiteral("qtPilot");
    result[QStringLiteral("mode")] = QStringLiteral("native");
    result[QStringLiteral("deprecated")] = deprecated;

    return envelopeToString(ResponseEnvelope::wrap(result));
  });

  // qt.sync - flush pending events and synchronize with main loop
  m_handler->RegisterMethod(QStringLiteral("qt.sync"), [](const QString& /*params*/) -> QString {
    const qint64 startMs = QDateTime::currentMSecsSinceEpoch();

    // 1. Process all pending posted events in the Qt main event loop.
    // Because Qt's event loop is strictly FIFO, all earlier queued click/key events
    // and their immediately triggered signal emissions run to completion here.
    QCoreApplication::processEvents(QEventLoop::AllEvents);

    // 2. Flush any deferred deletions resulting from widget teardown.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    const qint64 finishMs = QDateTime::currentMSecsSinceEpoch();

    QJsonObject result;
    result[QStringLiteral("synced")] = true;
    result[QStringLiteral("timestamp")] = finishMs;
    result[QStringLiteral("elapsedMs")] = finishMs - startMs;

    // Report active modal or popup widgets if opened by the processed events
    if (auto* modal = QApplication::activeModalWidget()) {
      result[QStringLiteral("activeModal")] = ObjectRegistry::instance()->objectId(modal);
    }
    if (auto* popup = QApplication::activePopupWidget()) {
      result[QStringLiteral("activePopup")] = ObjectRegistry::instance()->objectId(popup);
    }

    return envelopeToString(ResponseEnvelope::wrap(result));
  });
}

// ============================================================================
// Object discovery: qt.objects.*
// ============================================================================

void NativeModeApi::registerObjectMethods() {
  // qt.objects.tree - object tree
  m_handler->RegisterMethod(QStringLiteral("qt.objects.tree"),
                            [](const QString& params) -> QString {
                              auto p = parseParams(params);

                              QObject* rootObj = nullptr;
                              QString rootId = p[QStringLiteral("root")].toString();
                              if (!rootId.isEmpty()) {
                                rootObj = ObjectResolver::resolve(rootId);
                              }

                              int maxDepth = p[QStringLiteral("maxDepth")].toInt(-1);

                              QJsonObject tree = serializeObjectTree(rootObj, maxDepth);
                              return envelopeToString(ResponseEnvelope::wrap(tree));
                            });

  // qt.objects.inspect - consolidated read-only introspection with parts selector
  m_handler->RegisterMethod(
      QStringLiteral("qt.objects.inspect"), [](const QString& params) -> QString {
        auto p = parseParams(params);
        QObject* obj = resolveObjectParam(p, QStringLiteral("qt.objects.inspect"));
        QString objectId = p[QStringLiteral("objectId")].toString();

        static const QStringList allParts = {
            QStringLiteral("info"),    QStringLiteral("properties"), QStringLiteral("methods"),
            QStringLiteral("signals"), QStringLiteral("qml"),        QStringLiteral("geometry"),
            QStringLiteral("model")};

        QStringList requestedParts;
        QJsonValue partsValue = p[QStringLiteral("parts")];
        if (partsValue.isUndefined() || partsValue.isNull()) {
          requestedParts << QStringLiteral("info");
        } else if (partsValue.isString()) {
          QString s = partsValue.toString();
          if (s == QStringLiteral("all")) {
            requestedParts = allParts;
          } else {
            requestedParts << s;
          }
        } else if (partsValue.isArray()) {
          for (const auto& v : partsValue.toArray()) {
            requestedParts << v.toString();
          }
        }

        for (const QString& part : requestedParts) {
          if (!allParts.contains(part)) {
            QJsonArray validArray;
            for (const QString& v : allParts)
              validArray.append(v);
            validArray.append(QStringLiteral("all"));
            throw JsonRpcException(ErrorCode::kInvalidField,
                                   QStringLiteral("Unknown part: %1").arg(part),
                                   QJsonObject{{QStringLiteral("field"), part},
                                               {QStringLiteral("validFields"), validArray}});
          }
        }

        QJsonObject result;

        if (requestedParts.contains(QStringLiteral("info"))) {
          result[QStringLiteral("info")] = MetaInspector::objectInfo(obj);
        }
        if (requestedParts.contains(QStringLiteral("properties"))) {
          bool declaredOnly = p[QStringLiteral("declaredOnly")].toBool(false);
          QString propertyName = p[QStringLiteral("propertyName")].toString();
          result[QStringLiteral("properties")] =
              MetaInspector::listProperties(obj, declaredOnly, propertyName);
        }
        if (requestedParts.contains(QStringLiteral("methods"))) {
          result[QStringLiteral("methods")] = MetaInspector::listMethods(obj);
        }
        if (requestedParts.contains(QStringLiteral("signals"))) {
          result[QStringLiteral("signals")] = MetaInspector::listSignals(obj);
        }
        if (requestedParts.contains(QStringLiteral("qml"))) {
          QmlItemInfo qmlInfo = inspectQmlItem(obj);
          if (qmlInfo.isQmlItem) {
            QJsonObject qmlResult;
            qmlResult[QStringLiteral("isQmlItem")] = true;
            qmlResult[QStringLiteral("qmlId")] = qmlInfo.qmlId;
            qmlResult[QStringLiteral("qmlFile")] = qmlInfo.qmlFile;
            qmlResult[QStringLiteral("qmlTypeName")] = qmlInfo.shortTypeName;
            result[QStringLiteral("qml")] = qmlResult;
          } else {
            result[QStringLiteral("qml")] = QJsonValue();
          }
        }
        if (requestedParts.contains(QStringLiteral("geometry"))) {
          if (auto* item = qobject_cast<QGraphicsObject*>(obj)) {
            // Scene coordinates, matching what "pos" already reports. Doubles,
            // not ints: graphics coordinates are qreal.
            //
            // Taken from HitTest::graphicsItemGeometry rather than recomputed,
            // so the boundingRect()-not-childrenBoundingRect() decision lives
            // in exactly one place. The shape here stays flat to match the
            // widget branch below.
            const QJsonObject sceneRect =
                HitTest::graphicsItemGeometry(item)[QStringLiteral("scene")].toObject();
            QJsonObject geom;
            geom[QStringLiteral("x")] = sceneRect.value(QStringLiteral("x"));
            geom[QStringLiteral("y")] = sceneRect.value(QStringLiteral("y"));
            geom[QStringLiteral("width")] = sceneRect.value(QStringLiteral("width"));
            geom[QStringLiteral("height")] = sceneRect.value(QStringLiteral("height"));
            geom[QStringLiteral("visible")] = item->isVisible();
            geom[QStringLiteral("coordinateSpace")] = QStringLiteral("scene");
            result[QStringLiteral("geometry")] = geom;
          } else if (auto* widget = qobject_cast<QWidget*>(obj)) {
            QJsonObject geom;
            geom[QStringLiteral("x")] = widget->x();
            geom[QStringLiteral("y")] = widget->y();
            geom[QStringLiteral("width")] = widget->width();
            geom[QStringLiteral("height")] = widget->height();
            geom[QStringLiteral("visible")] = widget->isVisible();
            // Emitted on both branches so it is a discriminator a client can
            // switch on, rather than a presence test that cannot tell a widget
            // from an older probe build.
            geom[QStringLiteral("coordinateSpace")] = QStringLiteral("parent");
            result[QStringLiteral("geometry")] = geom;
          } else {
            result[QStringLiteral("geometry")] = QJsonValue();
          }
        }
        if (requestedParts.contains(QStringLiteral("model"))) {
          QAbstractItemModel* model = ModelNavigator::resolveModel(obj);
          if (model) {
            QJsonObject modelInfo;
            modelInfo[QStringLiteral("rowCount")] = model->rowCount();
            modelInfo[QStringLiteral("columnCount")] = model->columnCount();
            QJsonObject roleNames;
            const auto roles = model->roleNames();
            for (auto it = roles.constBegin(); it != roles.constEnd(); ++it) {
              roleNames[QString::number(it.key())] = QString::fromUtf8(it.value());
            }
            modelInfo[QStringLiteral("roleNames")] = roleNames;
            modelInfo[QStringLiteral("hasChildren")] = model->hasChildren();
            result[QStringLiteral("model")] = modelInfo;
          } else {
            result[QStringLiteral("model")] = QJsonValue();
          }
        }

        return envelopeToString(ResponseEnvelope::wrap(result, objectId));
      });

  // qt.objects.search - unified discovery: objectName / className / properties filters
  m_handler->RegisterMethod(
      QStringLiteral("qt.objects.search"), [](const QString& params) -> QString {
        auto p = parseParams(params);
        QString objectName = p[QStringLiteral("objectName")].toString();
        if (objectName.isEmpty()) {
          objectName = p[QStringLiteral("name")].toString();
        }
        QString className = p[QStringLiteral("className")].toString();
        if (className.isEmpty()) {
          className = p[QStringLiteral("class_name")].toString();
        }
        QJsonObject propFilters = p[QStringLiteral("properties")].toObject();
        QString rootId = p[QStringLiteral("root")].toString();
        if (rootId.isEmpty()) {
          rootId = p[QStringLiteral("rootId")].toString();
        }
        if (rootId.isEmpty()) {
          rootId = p[QStringLiteral("root_id")].toString();
        }
        int limit = p.contains(QStringLiteral("limit")) ? p[QStringLiteral("limit")].toInt() : 50;

        if (limit < 0) {
          throw JsonRpcException(
              JsonRpcError::kInvalidParams, QStringLiteral("limit must be >= 0"),
              QJsonObject{{QStringLiteral("method"), QStringLiteral("qt.objects.search")}});
        }

        QObject* rootObj = nullptr;
        if (!rootId.isEmpty()) {
          auto rootRes = ObjectResolver::resolveExpected(rootId);
          if (!rootRes) {
            throw JsonRpcException(
                ErrorCode::kObjectNotFound, QStringLiteral("Root object not found: %1").arg(rootId),
                QJsonObject{{QStringLiteral("method"), QStringLiteral("qt.objects.search")},
                            {QStringLiteral("root"), rootId}});
          }
          rootObj = *rootRes;
          if (rootObj->thread() != QThread::currentThread()) {
            throw JsonRpcException(
                JsonRpcError::kInvalidParams,
                QStringLiteral("Root object lives on another thread and cannot be searched: %1")
                    .arg(rootId),
                QJsonObject{{QStringLiteral("method"), QStringLiteral("qt.objects.search")},
                            {QStringLiteral("root"), rootId}});
          }
        }

        // Only objects this thread owns are looked at (see collectOwned()). Each
        // candidate is then watched through a QPointer: property filters and ID
        // generation run application getters, which may destroy it -- or any
        // candidate after it. Those are skipped and counted, never dereferenced.
        // No registry lock is held here, so a getter that waits on a thread that
        // creates objects cannot deadlock against the creation hook.
        auto* registry = ObjectRegistry::instance();
        const OwnedObjects candidates =
            registry->collectOwned(ObjectFilter{className, objectName}, rootObj);

        enum class Skip { Destroyed, Mismatch };
        using Candidate = QPointer<QObject>;

        auto alive = [](const Candidate& weak) -> std::expected<QObject*, Skip> {
          if (weak.isNull()) {
            return std::unexpected(Skip::Destroyed);
          }
          return weak.data();
        };

        auto matchProperties = [&](const Candidate& weak) -> std::expected<QObject*, Skip> {
          for (auto it = propFilters.constBegin(); it != propFilters.constEnd(); ++it) {
            auto propRes = MetaInspector::getPropertyExpected(weak.data(), it.key());
            if (weak.isNull()) {
              return std::unexpected(Skip::Destroyed);
            }
            if (!propRes.has_value() || *propRes != it.value()) {
              return std::unexpected(Skip::Mismatch);
            }
          }
          return weak.data();
        };

        auto describeMatch = [&](const Candidate& weak) -> std::expected<QJsonObject, Skip> {
          // objectId() reads the `text` property of unnamed objects -- this one and
          // its siblings -- and comes back empty if a getter destroyed anything.
          // Once whatever died is gone, a second attempt usually succeeds.
          QString objId;
          for (int attempt = 0; attempt < 3 && objId.isEmpty() && !weak.isNull(); ++attempt) {
            objId = registry->objectId(weak.data());
          }
          if (objId.isEmpty()) {
            return std::unexpected(Skip::Destroyed);
          }
          return alive(weak).transform([&](QObject* obj) {
            QJsonObject entry;
            entry[QStringLiteral("objectId")] = objId;
            entry[QStringLiteral("className")] = QString::fromUtf8(obj->metaObject()->className());
            entry[QStringLiteral("objectName")] = obj->objectName();
            entry[QStringLiteral("numericId")] = ObjectResolver::assignNumericId(obj);
            return entry;
          });
        };

        QJsonArray matches;
        bool truncated = false;
        int skippedDestroyed = 0;
        for (const Candidate& weak : candidates.objects) {
          const auto matched =
              alive(weak).and_then([&](QObject*) { return matchProperties(weak); });
          // Only a real match beyond the limit makes the result truncated.
          if (matched && matches.size() >= limit) {
            truncated = true;
            break;
          }
          const auto outcome = matched.and_then([&](QObject*) { return describeMatch(weak); });
          if (outcome) {
            matches.append(*outcome);
          } else if (outcome.error() == Skip::Destroyed) {
            ++skippedDestroyed;
          }
        }

        QJsonObject result;
        result[QStringLiteral("objects")] = matches;
        result[QStringLiteral("count")] = matches.size();
        result[QStringLiteral("truncated")] = truncated;
        if (skippedDestroyed > 0) {
          result[QStringLiteral("skippedDestroyed")] = skippedDestroyed;
        }
        if (candidates.skippedForeignThread > 0) {
          result[QStringLiteral("skippedForeignThread")] = candidates.skippedForeignThread;
        }
        return envelopeToString(ResponseEnvelope::wrap(result));
      });
}

// ============================================================================
// Properties: qt.properties.*
// ============================================================================

void NativeModeApi::registerPropertyMethods() {
  // qt.properties.get
  m_handler->RegisterMethod(
      QStringLiteral("qt.properties.get"), [](const QString& params) -> QString {
        auto p = parseParams(params);
        QString objectId = p[QStringLiteral("objectId")].toString();

        auto res = tryResolveObjectParam(p, QStringLiteral("qt.properties.get"))
                       .and_then([&](QObject* obj) {
                         return requireStringParam(p, QStringLiteral("name"),
                                                   QStringLiteral("qt.properties.get"))
                             .and_then([&](const QString& name) {
                               return MetaInspector::getPropertyExpected(obj, name).transform_error(
                                   [&](const PropertyError& err) {
                                     return JsonRpcException(
                                         ErrorCode::kPropertyNotFound, err.message,
                                         QJsonObject{{QStringLiteral("objectId"), objectId},
                                                     {QStringLiteral("property"), name}});
                                   });
                             });
                       })
                       .transform([&](const QJsonValue& value) {
                         QJsonObject result;
                         result[QStringLiteral("value")] = value;
                         return envelopeToString(ResponseEnvelope::wrap(result, objectId));
                       });

        if (!res) {
          throw res.error();
        }
        return *res;
      });

  // qt.properties.set
  m_handler->RegisterMethod(
      QStringLiteral("qt.properties.set"), [](const QString& params) -> QString {
        auto p = parseParams(params);
        QString objectId = p[QStringLiteral("objectId")].toString();

        auto res = tryResolveObjectParam(p, QStringLiteral("qt.properties.set"))
                       .and_then([&](QObject* obj) {
                         return requireStringParam(p, QStringLiteral("name"),
                                                   QStringLiteral("qt.properties.set"))
                             .and_then([&](const QString& name) {
                               return requireValueParam(p, QStringLiteral("value"),
                                                        QStringLiteral("qt.properties.set"))
                                   .and_then([&](const QJsonValue& value) {
                                     return MetaInspector::setPropertyExpected(obj, name, value)
                                         .transform_error([&](const PropertyError& err) {
                                           int code = (err.kind == PropertyErrorKind::ReadOnly)
                                                          ? ErrorCode::kPropertyReadOnly
                                                          : ErrorCode::kPropertyTypeMismatch;
                                           return JsonRpcException(
                                               code, err.message,
                                               QJsonObject{{QStringLiteral("objectId"), objectId},
                                                           {QStringLiteral("property"), name}});
                                         });
                                   });
                             });
                       })
                       .transform([&]() {
                         QJsonObject result;
                         result[QStringLiteral("ok")] = true;
                         return envelopeToString(ResponseEnvelope::wrap(result, objectId));
                       });

        if (!res) {
          throw res.error();
        }
        return *res;
      });
}

// ============================================================================
// Methods: qt.methods.*
// ============================================================================

void NativeModeApi::registerMethodMethods() {
  // qt.methods.invoke
  m_handler->RegisterMethod(
      QStringLiteral("qt.methods.invoke"), [](const QString& params) -> QString {
        auto p = parseParams(params);
        QString objectId = p[QStringLiteral("objectId")].toString();

        const bool deferred =
            optionalBool(p, QStringLiteral("deferred"), false, QStringLiteral("qt.methods.invoke"));

        auto toRpcError = [&](const QString& method) {
          return [&objectId, method](const MethodError& err) {
            int code = (err.kind == MethodErrorKind::NotFound) ? ErrorCode::kMethodNotFound
                                                               : ErrorCode::kMethodInvocationFailed;
            return JsonRpcException(code, err.message,
                                    QJsonObject{{QStringLiteral("objectId"), objectId},
                                                {QStringLiteral("method"), method}});
          };
        };

        // Preparation happens now in both modes, so a bad method or argument is
        // reported to this caller. Only the call itself is deferred.
        auto run =
            [&](QObject* obj, const QString& method,
                PreparedInvocation prepared) -> std::expected<QJsonObject, JsonRpcException> {
          if (deferred) {
            // Queued to the target's thread: for another thread's object, one whose
            // event loop may never run it, after the caller was told it was queued.
            if (obj->thread() != QThread::currentThread()) {
              return std::unexpected(JsonRpcException(
                  JsonRpcError::kInvalidParams,
                  QStringLiteral("Cannot defer a call on an object another thread owns: %1")
                      .arg(objectId),
                  QJsonObject{{QStringLiteral("objectId"), objectId},
                              {QStringLiteral("method"), method}}));
            }
            queueInvocation(obj, std::move(prepared));
            return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("deferred"), true}};
          }
          return prepared.invoke(obj)
              .transform([](const QJsonValue& value) {
                return QJsonObject{{QStringLiteral("result"), value}};
              })
              .transform_error(toRpcError(method));
        };

        auto res = tryResolveObjectParam(p, QStringLiteral("qt.methods.invoke"))
                       .and_then([&](QObject* obj) {
                         return requireStringParam(p, QStringLiteral("method"),
                                                   QStringLiteral("qt.methods.invoke"))
                             .and_then([&](const QString& method) {
                               return MetaInspector::prepareInvocation(
                                          obj, method, p[QStringLiteral("args")].toArray())
                                   .transform_error(toRpcError(method))
                                   .and_then([&](PreparedInvocation prepared) {
                                     return run(obj, method, std::move(prepared));
                                   });
                             });
                       })
                       .transform([&](const QJsonObject& resultObj) {
                         return envelopeToString(ResponseEnvelope::wrap(resultObj, objectId));
                       });

        if (!res) {
          throw res.error();
        }
        return *res;
      });
}

// ============================================================================
// Signals: qt.signals.*
// ============================================================================

void NativeModeApi::registerSignalMethods() {
  // qt.signals.subscribe
  m_handler->RegisterMethod(
      QStringLiteral("qt.signals.subscribe"), [](const QString& params) -> QString {
        auto p = parseParams(params);
        QString objectId = p[QStringLiteral("objectId")].toString();
        QString signal = p[QStringLiteral("signal")].toString();

        if (objectId.isEmpty() || signal.isEmpty()) {
          throw JsonRpcException(
              JsonRpcError::kInvalidParams,
              QStringLiteral("Missing required parameters: objectId, signal"),
              QJsonObject{{QStringLiteral("method"), QStringLiteral("qt.signals.subscribe")}});
        }

        auto res = SignalMonitor::instance()
                       ->subscribeExpected(objectId, signal)
                       .transform_error([&](const SignalError& err) {
                         int code = (err.kind == SignalErrorKind::ObjectNotFound)
                                        ? ErrorCode::kObjectNotFound
                                        : ErrorCode::kSignalNotFound;
                         return JsonRpcException(code, err.message,
                                                 QJsonObject{{QStringLiteral("objectId"), objectId},
                                                             {QStringLiteral("signal"), signal}});
                       })
                       .transform([&](const QString& subId) {
                         QJsonObject result;
                         result[QStringLiteral("subscriptionId")] = subId;
                         return envelopeToString(ResponseEnvelope::wrap(result, objectId));
                       });

        if (!res) {
          throw res.error();
        }
        return *res;
      });

  // qt.signals.unsubscribe
  m_handler->RegisterMethod(
      QStringLiteral("qt.signals.unsubscribe"), [](const QString& params) -> QString {
        auto p = parseParams(params);
        QString subscriptionId = p[QStringLiteral("subscriptionId")].toString();

        if (subscriptionId.isEmpty()) {
          throw JsonRpcException(
              JsonRpcError::kInvalidParams,
              QStringLiteral("Missing required parameter: subscriptionId"),
              QJsonObject{{QStringLiteral("method"), QStringLiteral("qt.signals.unsubscribe")}});
        }

        SignalMonitor::instance()->unsubscribe(subscriptionId);
        QJsonObject result;
        result[QStringLiteral("ok")] = true;
        return envelopeToString(ResponseEnvelope::wrap(result));
      });

  // qt.signals.setLifecycle
  m_handler->RegisterMethod(QStringLiteral("qt.signals.setLifecycle"),
                            [](const QString& params) -> QString {
                              auto p = parseParams(params);
                              bool enabled = p[QStringLiteral("enabled")].toBool();

                              SignalMonitor::instance()->setLifecycleNotificationsEnabled(enabled);
                              QJsonObject result;
                              result[QStringLiteral("enabled")] = enabled;
                              return envelopeToString(ResponseEnvelope::wrap(result));
                            });
}

// ============================================================================
// Event capture: qt.events.*
// ============================================================================

void NativeModeApi::registerEventMethods() {
  // qt.events.start - start global event capture
  m_handler->RegisterMethod(QStringLiteral("qt.events.start"),
                            [](const QString& /*params*/) -> QString {
                              EventCapture::instance()->startCapture();
                              QJsonObject result;
                              result[QStringLiteral("capturing")] = true;
                              return envelopeToString(ResponseEnvelope::wrap(result));
                            });

  // qt.events.stop - stop global event capture
  m_handler->RegisterMethod(QStringLiteral("qt.events.stop"),
                            [](const QString& /*params*/) -> QString {
                              EventCapture::instance()->stopCapture();
                              QJsonObject result;
                              result[QStringLiteral("capturing")] = false;
                              return envelopeToString(ResponseEnvelope::wrap(result));
                            });
}

// ============================================================================
// UI interaction: qt.ui.*
// ============================================================================

void NativeModeApi::registerUiMethods() {
  // qt.ui.click
  m_handler->RegisterMethod(QStringLiteral("qt.ui.click"), [](const QString& params) -> QString {
    auto p = parseParams(params);
    QString objectId = p[QStringLiteral("objectId")].toString();

    // Defer the actual click. This handler runs inside the WebSocket message dispatch
    // (QWebSocketPrivate::processData). Clicking synchronously runs the target's slot
    // inline — and if that slot spins a nested/modal event loop (e.g. a button that opens
    // a modal dialog, or a Save that shows a modal progress dialog), the nested loop
    // re-enters WebSocket frame processing. QtWebSockets is not reentrant, so processData
    // dereferences a half-updated state and SIGSEGVs. Queuing the click makes it run from
    // the top of the event loop after processData has fully unwound. QPointer guards
    // against the widget being destroyed before the queued call fires. Graphics
    // items use the same queued path, targeted at the rendering view's viewport.
    QJsonObject result = handleUiClickLike(p, QStringLiteral("qt.ui.click"), false);
    return envelopeToString(ResponseEnvelope::wrap(result, objectId));
  });

  // qt.ui.doubleClick
  m_handler->RegisterMethod(
      QStringLiteral("qt.ui.doubleClick"), [](const QString& params) -> QString {
        auto p = parseParams(params);
        QString objectId = p[QStringLiteral("objectId")].toString();
        QJsonObject result = handleUiClickLike(p, QStringLiteral("qt.ui.doubleClick"), true);
        return envelopeToString(ResponseEnvelope::wrap(result, objectId));
      });

  // qt.ui.contextMenu
  m_handler->RegisterMethod(QStringLiteral("qt.ui.contextMenu"),
                            [](const QString& params) -> QString {
                              auto p = parseParams(params);
                              QString objectId = p[QStringLiteral("objectId")].toString();
                              QJsonObject result = handleUiContextMenu(p);
                              return envelopeToString(ResponseEnvelope::wrap(result, objectId));
                            });

  // qt.ui.activeMenu
  m_handler->RegisterMethod(QStringLiteral("qt.ui.activeMenu"),
                            [](const QString& /*params*/) -> QString {
                              QJsonObject result = handleUiActiveMenu();
                              return envelopeToString(ResponseEnvelope::wrap(result));
                            });

  // qt.ui.activateMenuItem
  m_handler->RegisterMethod(QStringLiteral("qt.ui.activateMenuItem"),
                            [](const QString& params) -> QString {
                              auto p = parseParams(params);
                              QJsonObject result = handleUiActivateMenuItem(p);
                              return envelopeToString(ResponseEnvelope::wrap(result));
                            });

  // qt.ui.sendKeys
  m_handler->RegisterMethod(QStringLiteral("qt.ui.sendKeys"), [](const QString& params) -> QString {
    auto p = parseParams(params);
    QString objectId = p[QStringLiteral("objectId")].toString();

    QJsonObject result = handleUiSendKeys(p);
    return envelopeToString(ResponseEnvelope::wrap(result, objectId));
  });

  // qt.ui.clickItem - select/click/edit an item by itemPath (string[]) or path (int[]).
  m_handler->RegisterMethod(
      QStringLiteral("qt.ui.clickItem"), [](const QString& params) -> QString {
        auto p = parseParams(params);
        QObject* obj = resolveObjectParam(p, QStringLiteral("qt.ui.clickItem"));
        QString objectId = p[QStringLiteral("objectId")].toString();

        const bool hasPath = p.contains(QStringLiteral("path"));
        const bool hasItemPath = p.contains(QStringLiteral("itemPath"));
        if (hasPath == hasItemPath) {
          throw JsonRpcException(
              JsonRpcError::kInvalidParams,
              QStringLiteral("Exactly one of 'path' or 'itemPath' is required"),
              QJsonObject{{QStringLiteral("method"), QStringLiteral("qt.ui.clickItem")}});
        }

        auto* view = qobject_cast<QAbstractItemView*>(obj);
        auto* combo = view ? nullptr : qobject_cast<QComboBox*>(obj);
        QAbstractItemModel* model = nullptr;
        if (view) {
          model = view->model();
        } else if (combo) {
          model = combo->model();
        }

        if (!model) {
          throw JsonRpcException(ErrorCode::kNotAModel,
                                 QStringLiteral("Object is not a view or combo box with a model"),
                                 QJsonObject{{QStringLiteral("objectId"), objectId},
                                             {QStringLiteral("className"),
                                              QString::fromUtf8(obj->metaObject()->className())}});
        }

        const int column = p[QStringLiteral("column")].toInt(0);
        const QString action = p[QStringLiteral("action")].toString(QStringLiteral("click"));

        // Early column validation — must happen before path resolution so that
        // e.g. combo boxes (single column) report kInvalidColumn rather than
        // kItemNotFound when caller passes a column > 0.
        {
          // For top-level column count we query the root's columns. Specific
          // parent levels may have differing columnCount() but this upper bound
          // is what the caller expressed intent against.
          const int rootCols = combo ? 1 : model->columnCount(QModelIndex());
          if (column < 0 || column >= rootCols) {
            throw JsonRpcException(
                ErrorCode::kInvalidColumn,
                QStringLiteral("Column %1 out of range (columnCount=%2)").arg(column).arg(rootCols),
                QJsonObject{{QStringLiteral("column"), column},
                            {QStringLiteral("columnCount"), rootCols}});
          }
        }

        // Resolve the row-identity index.
        QModelIndex rowTarget;
        QJsonObject notFoundDetail;

        if (hasPath) {
          QList<int> rowPath;
          QJsonArray pathArr = p[QStringLiteral("path")].toArray();
          for (const QJsonValue& v : pathArr)
            rowPath.append(v.toInt());
          auto pathRes = ModelNavigator::pathToIndexExpected(model, rowPath);
          if (!pathRes || (!pathRes->isValid() && !rowPath.isEmpty())) {
            int failedSegment = pathRes ? 0 : pathRes.error();
            QModelIndex walk;
            for (int i = 0; i < failedSegment; ++i) {
              ModelNavigator::ensureFetched(model, walk);
              walk = model->index(rowPath[i], 0, walk);
            }
            ModelNavigator::ensureFetched(model, walk);
            QJsonArray partial;
            for (int i = 0; i < failedSegment; ++i)
              partial.append(rowPath[i]);
            notFoundDetail =
                QJsonObject{{QStringLiteral("mode"), QStringLiteral("row")},
                            {QStringLiteral("failedSegment"), failedSegment},
                            {QStringLiteral("requestedRow"), rowPath.value(failedSegment, -1)},
                            {QStringLiteral("availableRows"), model->rowCount(walk)},
                            {QStringLiteral("partialPath"), partial}};
          } else {
            rowTarget = *pathRes;
          }
        } else {
          QStringList itemPath;
          QJsonArray ipArr = p[QStringLiteral("itemPath")].toArray();
          for (const QJsonValue& v : ipArr)
            itemPath.append(v.toString());
          auto textRes =
              ModelNavigator::textPathToIndexExpected(model, itemPath, Qt::DisplayRole, column);
          if (!textRes || (!textRes->isValid() && !itemPath.isEmpty())) {
            int failedSegment = textRes ? 0 : textRes.error();
            QJsonArray partial;
            QModelIndex walk;
            for (int i = 0; i < failedSegment; ++i) {
              ModelNavigator::ensureFetched(model, walk);
              const int rows = model->rowCount(walk);
              for (int r = 0; r < rows; ++r) {
                const QModelIndex cell = model->index(r, column, walk);
                if (model->data(cell, Qt::DisplayRole).toString() == itemPath[i]) {
                  partial.append(r);
                  walk = model->index(r, 0, walk);
                  break;
                }
              }
            }
            notFoundDetail =
                QJsonObject{{QStringLiteral("mode"), QStringLiteral("text")},
                            {QStringLiteral("failedSegment"), failedSegment},
                            {QStringLiteral("segmentText"), itemPath.value(failedSegment)},
                            {QStringLiteral("partialPath"), partial}};
          } else {
            rowTarget = *textRes;
          }
        }

        if (!rowTarget.isValid()) {
          throw JsonRpcException(ErrorCode::kItemNotFound, QStringLiteral("Item not found"),
                                 notFoundDetail);
        }

        // Validate action column. QComboBox is single-column.
        const int colCount = combo ? 1 : model->columnCount(rowTarget.parent());
        if (column < 0 || column >= colCount) {
          throw JsonRpcException(
              ErrorCode::kInvalidColumn,
              QStringLiteral("Column %1 out of range (columnCount=%2)").arg(column).arg(colCount),
              QJsonObject{{QStringLiteral("column"), column},
                          {QStringLiteral("columnCount"), colCount}});
        }

        if (combo) {
          // Combo boxes: length-1 paths only; actions collapse to setCurrentIndex.
          if (action == QStringLiteral("edit")) {
            QJsonArray pathOut;
            pathOut.append(rowTarget.row());
            throw JsonRpcException(ErrorCode::kNotEditable,
                                   QStringLiteral("QComboBox cells are not inline-editable"),
                                   QJsonObject{{QStringLiteral("path"), pathOut},
                                               {QStringLiteral("editColumn"), column}});
          }
          if (action == QStringLiteral("select") || action == QStringLiteral("click") ||
              action == QStringLiteral("doubleClick")) {
            combo->setCurrentIndex(rowTarget.row());
          } else {
            throw JsonRpcException(
                JsonRpcError::kInvalidParams, QStringLiteral("Unknown action: %1").arg(action),
                QJsonObject{{QStringLiteral("action"), action},
                            {QStringLiteral("validActions"),
                             QJsonArray{QStringLiteral("select"), QStringLiteral("click"),
                                        QStringLiteral("doubleClick")}}});
          }
        } else {
          const QModelIndex actionTarget =
              model->index(rowTarget.row(), column, rowTarget.parent());

          const bool wantExpand =
              p.contains(QStringLiteral("expand")) ? p[QStringLiteral("expand")].toBool() : true;
          const bool wantScroll =
              p.contains(QStringLiteral("scroll")) ? p[QStringLiteral("scroll")].toBool() : true;
          if (wantExpand) {
            if (auto* treeView = qobject_cast<QTreeView*>(view)) {
              for (QModelIndex anc = actionTarget.parent(); anc.isValid(); anc = anc.parent()) {
                treeView->expand(anc);
              }
            }
          }
          if (wantScroll)
            view->scrollTo(actionTarget);

          if (action == QStringLiteral("select")) {
            view->setCurrentIndex(actionTarget);
          } else if (action == QStringLiteral("click")) {
            view->setCurrentIndex(actionTarget);
            const QRect rect = view->visualRect(actionTarget);
            InputSimulator::mouseClick(view->viewport(), InputSimulator::MouseButton::Left,
                                       rect.center());
          } else if (action == QStringLiteral("doubleClick")) {
            view->setCurrentIndex(actionTarget);
            const QRect rect = view->visualRect(actionTarget);
            InputSimulator::mouseDoubleClick(view->viewport(), InputSimulator::MouseButton::Left,
                                             rect.center());
          } else if (action == QStringLiteral("edit")) {
            const int editColumn = p.contains(QStringLiteral("editColumn"))
                                       ? p[QStringLiteral("editColumn")].toInt()
                                       : column;
            if (editColumn < 0 || editColumn >= colCount) {
              throw JsonRpcException(ErrorCode::kInvalidColumn,
                                     QStringLiteral("editColumn %1 out of range (columnCount=%2)")
                                         .arg(editColumn)
                                         .arg(colCount),
                                     QJsonObject{{QStringLiteral("editColumn"), editColumn},
                                                 {QStringLiteral("columnCount"), colCount}});
            }
            const QModelIndex editIdx =
                model->index(rowTarget.row(), editColumn, rowTarget.parent());
            if (!(model->flags(editIdx) & Qt::ItemIsEditable)) {
              QJsonArray pathOut;
              for (QModelIndex w = editIdx; w.isValid(); w = w.parent())
                pathOut.prepend(w.row());
              throw JsonRpcException(ErrorCode::kNotEditable,
                                     QStringLiteral("Cell is not editable"),
                                     QJsonObject{{QStringLiteral("path"), pathOut},
                                                 {QStringLiteral("editColumn"), editColumn}});
            }
            view->setCurrentIndex(editIdx);
            view->edit(editIdx);
          } else {
            throw JsonRpcException(
                JsonRpcError::kInvalidParams, QStringLiteral("Unknown action: %1").arg(action),
                QJsonObject{{QStringLiteral("action"), action},
                            {QStringLiteral("validActions"),
                             QJsonArray{QStringLiteral("select"), QStringLiteral("click"),
                                        QStringLiteral("doubleClick"), QStringLiteral("edit")}}});
          }
        }

        QJsonObject result = ModelNavigator::indexToRowData(model, rowTarget, {Qt::DisplayRole});
        result[QStringLiteral("found")] = true;
        result[QStringLiteral("action")] = action;
        if (hasItemPath)
          result[QStringLiteral("itemPath")] = p[QStringLiteral("itemPath")];
        return envelopeToString(ResponseEnvelope::wrap(result, objectId));
      });

  // qt.ui.screenshot
  m_handler->RegisterMethod(
      QStringLiteral("qt.ui.screenshot"), [](const QString& params) -> QString {
        auto p = parseParams(params);
        QWidget* widget = resolveWidgetParam(p, QStringLiteral("qt.ui.screenshot"));
        QString objectId = p[QStringLiteral("objectId")].toString();

        bool fullWindow = p[QStringLiteral("fullWindow")].toBool(false);
        QJsonObject region = p[QStringLiteral("region")].toObject();

        QByteArray base64;
        if (fullWindow) {
          base64 = Screenshot::captureWindow(widget);
        } else if (!region.isEmpty()) {
          QRect rect(region[QStringLiteral("x")].toInt(), region[QStringLiteral("y")].toInt(),
                     region[QStringLiteral("width")].toInt(),
                     region[QStringLiteral("height")].toInt());
          base64 = Screenshot::captureRegion(widget, rect);
        } else {
          base64 = Screenshot::captureWidget(widget);
        }

        QJsonObject result;
        result[QStringLiteral("image")] = QString::fromLatin1(base64);
        return envelopeToString(ResponseEnvelope::wrap(result, objectId));
      });

  // qt.ui.geometry
  m_handler->RegisterMethod(QStringLiteral("qt.ui.geometry"), [](const QString& params) -> QString {
    auto p = parseParams(params);
    QString objectId = p[QStringLiteral("objectId")].toString();

    // A QGraphicsView scene item is a QObject but never a QWidget, so the widget
    // path used to reject it -- leaving its on-screen position knowable only by
    // calibrating the view transform by hand. Map it through its view(s) instead.
    QObject* obj = resolveObjectParam(p, QStringLiteral("qt.ui.geometry"));
    // Resolved up front on both paths: a viewObjectId that the widget path
    // cannot use is a caller error, not something to drop on the floor.
    QGraphicsView* view =
        resolveViewParam(p, QStringLiteral("viewObjectId"), QStringLiteral("qt.ui.geometry"));

    if (auto* item = qobject_cast<QGraphicsObject*>(obj)) {
      // A view that does not render this item's scene would silently produce a
      // null rect -- indistinguishable from "not on screen yet". Name the views
      // the item does have instead.
      QGraphicsScene* scene = item->scene();
      if (view && (!scene || !scene->views().contains(view))) {
        QJsonArray candidates;
        if (scene) {
          const QList<QGraphicsView*> sceneViews = scene->views();
          for (QGraphicsView* candidate : sceneViews) {
            candidates.append(ObjectRegistry::instance()->objectId(candidate));
          }
        }
        throw JsonRpcException(
            JsonRpcError::kInvalidParams,
            QStringLiteral("viewObjectId does not render this item's scene: %1")
                .arg(p[QStringLiteral("viewObjectId")].toString()),
            QJsonObject{{QStringLiteral("method"), QStringLiteral("qt.ui.geometry")},
                        {QStringLiteral("objectId"), objectId},
                        {QStringLiteral("views"), candidates}});
      }

      QJsonObject geo = HitTest::graphicsItemGeometry(item, view);
      return envelopeToString(ResponseEnvelope::wrap(geo, objectId));
    }

    if (view) {
      throw JsonRpcException(
          JsonRpcError::kInvalidParams,
          QStringLiteral("viewObjectId is only meaningful when objectId names a "
                         "QGraphicsObject; %1 is not one")
              .arg(objectId),
          QJsonObject{
              {QStringLiteral("method"), QStringLiteral("qt.ui.geometry")},
              {QStringLiteral("objectId"), objectId},
              {QStringLiteral("className"), QString::fromUtf8(obj->metaObject()->className())}});
    }

#ifdef QTPILOT_HAS_QML
    if (auto* item = qobject_cast<QQuickItem*>(obj)) {
      QJsonObject geo = HitTest::itemGeometry(item);
      return envelopeToString(ResponseEnvelope::wrap(geo, objectId));
    }
#endif

    // Cast the object already in hand rather than re-resolving the same id:
    // a registry miss falls back to a full tree search, so resolving twice can
    // mean walking the tree twice for every widget geometry call.
    auto* widget = qobject_cast<QWidget*>(obj);
    if (!widget) {
      throw JsonRpcException(
          ErrorCode::kObjectNotWidget, QStringLiteral("Object is not a widget: %1").arg(objectId),
          QJsonObject{
              {QStringLiteral("objectId"), objectId},
              {QStringLiteral("className"), QString::fromUtf8(obj->metaObject()->className())}});
    }
    QJsonObject geo = HitTest::widgetGeometry(widget);
    return envelopeToString(ResponseEnvelope::wrap(geo, objectId));
  });

  // qt.ui.hitTest
  m_handler->RegisterMethod(QStringLiteral("qt.ui.hitTest"), [](const QString& params) -> QString {
    auto p = parseParams(params);

    // Scoped form: x/y are viewport coordinates of the named QGraphicsView, and
    // the search runs inside its scene. Callers that already know the view want
    // this; it also avoids QApplication::widgetAt, which is unreliable headless.
    QGraphicsView* view =
        resolveViewParam(p, QStringLiteral("viewObjectId"), QStringLiteral("qt.ui.hitTest"));

    const double x = requireCoordinate(p, QStringLiteral("x"), QStringLiteral("qt.ui.hitTest"));
    const double y = requireCoordinate(p, QStringLiteral("y"), QStringLiteral("qt.ui.hitTest"));

    if (view) {
      // Keep the object, do not look it up again by id: the round trip costs a
      // registry search that can miss for an untracked item, which is the only
      // reason a "className": "unknown" was ever reachable here.
      QGraphicsObject* item = HitTest::graphicsItemAt(view, QPointF(x, y));
      if (!item) {
        throw JsonRpcException(
            ErrorCode::kObjectNotFound,
            QStringLiteral("No scene item found at viewport point (%1, %2)").arg(x).arg(y),
            QJsonObject{
                {QStringLiteral("x"), x},
                {QStringLiteral("y"), y},
                {QStringLiteral("viewObjectId"), p[QStringLiteral("viewObjectId")].toString()}});
      }
      QJsonObject result;
      result[QStringLiteral("objectId")] = ObjectRegistry::instance()->objectId(item);
      result[QStringLiteral("className")] = QString::fromUtf8(item->metaObject()->className());
      result[QStringLiteral("viewObjectId")] = p[QStringLiteral("viewObjectId")].toString();
      return envelopeToString(ResponseEnvelope::wrap(result));
    }

    // Global form: screen coordinates, rounded to the pixel grid the widget
    // hit test works in.
    const QPoint globalPos(qRound(x), qRound(y));
    QString foundId = HitTest::widgetIdAt(globalPos);
#ifdef QTPILOT_HAS_QML
    if (foundId.isEmpty()) {
      foundId = HitTest::quickItemIdAt(globalPos);
    }
#endif
    if (foundId.isEmpty()) {
      throw JsonRpcException(ErrorCode::kObjectNotFound,
                             QStringLiteral("No widget found at point (%1, %2)").arg(x).arg(y),
                             QJsonObject{{QStringLiteral("x"), x}, {QStringLiteral("y"), y}});
    }

    QObject* obj = ObjectRegistry::instance()->findById(foundId);
    QJsonObject result;
    result[QStringLiteral("objectId")] = foundId;
    result[QStringLiteral("className")] =
        obj ? QString::fromUtf8(obj->metaObject()->className()) : QStringLiteral("unknown");
    return envelopeToString(ResponseEnvelope::wrap(result));
  });
}

// ============================================================================
// Name map: qt.names.*
// ============================================================================

void NativeModeApi::registerNameMapMethods() {
  // qt.names.register
  m_handler->RegisterMethod(
      QStringLiteral("qt.names.register"), [](const QString& params) -> QString {
        auto p = parseParams(params);
        QString name = p[QStringLiteral("name")].toString();
        QString path = p[QStringLiteral("path")].toString();

        if (name.isEmpty() || path.isEmpty()) {
          throw JsonRpcException(
              JsonRpcError::kInvalidParams,
              QStringLiteral("Missing required parameters: name, path"),
              QJsonObject{{QStringLiteral("method"), QStringLiteral("qt.names.register")}});
        }

        SymbolicNameMap::instance()->registerName(name, path);
        QJsonObject result;
        result[QStringLiteral("ok")] = true;
        return envelopeToString(ResponseEnvelope::wrap(result));
      });

  // qt.names.unregister
  m_handler->RegisterMethod(
      QStringLiteral("qt.names.unregister"), [](const QString& params) -> QString {
        auto p = parseParams(params);
        QString name = p[QStringLiteral("name")].toString();

        if (name.isEmpty()) {
          throw JsonRpcException(
              JsonRpcError::kInvalidParams, QStringLiteral("Missing required parameter: name"),
              QJsonObject{{QStringLiteral("method"), QStringLiteral("qt.names.unregister")}});
        }

        SymbolicNameMap::instance()->unregisterName(name);
        QJsonObject result;
        result[QStringLiteral("ok")] = true;
        return envelopeToString(ResponseEnvelope::wrap(result));
      });

  // qt.names.list
  m_handler->RegisterMethod(QStringLiteral("qt.names.list"),
                            [](const QString& /*params*/) -> QString {
                              QJsonObject names = SymbolicNameMap::instance()->allNames();
                              return envelopeToString(ResponseEnvelope::wrap(names));
                            });

  // qt.names.validate
  m_handler->RegisterMethod(QStringLiteral("qt.names.validate"),
                            [](const QString& /*params*/) -> QString {
                              QJsonArray validations = SymbolicNameMap::instance()->validateNames();
                              return envelopeToString(ResponseEnvelope::wrap(validations));
                            });

  // qt.names.load
  m_handler->RegisterMethod(QStringLiteral("qt.names.load"), [](const QString& params) -> QString {
    auto p = parseParams(params);
    auto res =
        requireStringParam(p, QStringLiteral("filePath"), QStringLiteral("qt.names.load"))
            .and_then([](const QString& filePath) {
              return SymbolicNameMap::instance()->loadFromFileExpected(filePath).transform_error(
                  [&](const QString& err) {
                    return JsonRpcException(ErrorCode::kNameMapLoadError, err,
                                            QJsonObject{{QStringLiteral("filePath"), filePath}});
                  });
            })
            .transform([]() {
              QJsonObject names = SymbolicNameMap::instance()->allNames();
              QJsonObject result;
              result[QStringLiteral("ok")] = true;
              result[QStringLiteral("count")] = names.size();
              return envelopeToString(ResponseEnvelope::wrap(result));
            });

    if (!res) {
      throw res.error();
    }
    return *res;
  });

  // qt.names.save
  m_handler->RegisterMethod(QStringLiteral("qt.names.save"), [](const QString& params) -> QString {
    auto p = parseParams(params);
    auto res =
        requireStringParam(p, QStringLiteral("filePath"), QStringLiteral("qt.names.save"))
            .and_then([](const QString& filePath) {
              return SymbolicNameMap::instance()->saveToFileExpected(filePath).transform_error(
                  [&](const QString& err) {
                    return JsonRpcException(ErrorCode::kNameMapLoadError, err,
                                            QJsonObject{{QStringLiteral("filePath"), filePath}});
                  });
            })
            .transform([]() {
              QJsonObject result;
              result[QStringLiteral("ok")] = true;
              return envelopeToString(ResponseEnvelope::wrap(result));
            });

    if (!res) {
      throw res.error();
    }
    return *res;
  });
}

// ============================================================================
// QML introspection: qt.qml.*
// ============================================================================

void NativeModeApi::registerQmlMethods() {
  // (No QML-specific methods: qt.objects.inspect with parts=["qml"] covers qml metadata.)
}

// ============================================================================
// Model/View introspection: qt.models.*
// ============================================================================

void NativeModeApi::registerModelMethods() {
  // qt.models.list - discover all QAbstractItemModel instances
  m_handler->RegisterMethod(QStringLiteral("qt.models.list"),
                            [](const QString& /*params*/) -> QString {
                              QJsonArray models = ModelNavigator::listModels();
                              return envelopeToString(ResponseEnvelope::wrap(models));
                            });

  // qt.models.data - fetch model data with pagination and role filtering
  m_handler->RegisterMethod(QStringLiteral("qt.models.data"), [](const QString& params) -> QString {
    auto p = parseParams(params);
    auto modelRes = tryResolveModelParam(p, QStringLiteral("qt.models.data"));
    if (!modelRes) {
      throw modelRes.error();
    }
    QAbstractItemModel* model = *modelRes;
    QString objectId = p[QStringLiteral("objectId")].toString();

    // Resolve parent path.
    QList<int> parentPath;
    QJsonArray parentArr = p[QStringLiteral("parent")].toArray();
    for (const QJsonValue& v : parentArr)
      parentPath.append(v.toInt());

    if (!parentPath.isEmpty()) {
      auto parentRes = ModelNavigator::pathToIndexExpected(model, parentPath);
      if (!parentRes || !parentRes->isValid()) {
        int failed = parentRes ? 0 : parentRes.error();
        // Compute available rows at the failure point for the error detail.
        QModelIndex walk;
        for (int i = 0; i < failed; ++i) {
          ModelNavigator::ensureFetched(model, walk);
          walk = model->index(parentPath[i], 0, walk);
        }
        ModelNavigator::ensureFetched(model, walk);
        throw JsonRpcException(
            ErrorCode::kInvalidParentPath,
            QStringLiteral("Parent path invalid at segment %1").arg(failed),
            QJsonObject{{QStringLiteral("path"), parentArr},
                        {QStringLiteral("failedSegment"), failed},
                        {QStringLiteral("availableRows"), model->rowCount(walk)}});
      }
    }

    int offset = p[QStringLiteral("offset")].toInt(0);
    int limit = p[QStringLiteral("limit")].toInt(-1);

    // Resolve roles parameter.
    QList<int> resolvedRoles;
    QJsonArray rolesParam = p[QStringLiteral("roles")].toArray();
    for (const QJsonValue& roleVal : rolesParam) {
      if (roleVal.isDouble()) {
        resolvedRoles.append(roleVal.toInt());
      } else if (roleVal.isString()) {
        QString roleName = roleVal.toString();
        auto roleRes = ModelNavigator::resolveRoleNameExpected(model, roleName);
        if (!roleRes) {
          throw JsonRpcException(
              ErrorCode::kModelRoleNotFound, QStringLiteral("Role not found: %1").arg(roleName),
              QJsonObject{{QStringLiteral("roleName"), roleName},
                          {QStringLiteral("availableRoles"), ModelNavigator::getRoleNames(model)}});
        }
        resolvedRoles.append(*roleRes);
      }
    }

    QJsonObject data =
        ModelNavigator::getModelData(model, parentPath, offset, limit, resolvedRoles);
    return envelopeToString(ResponseEnvelope::wrap(data, objectId));
  });

  // qt.models.search - recursive value search with match modes (lazy-aware)
  m_handler->RegisterMethod(
      QStringLiteral("qt.models.search"), [](const QString& params) -> QString {
        auto p = parseParams(params);
        auto modelRes = tryResolveModelParam(p, QStringLiteral("qt.models.search"));
        if (!modelRes) {
          throw modelRes.error();
        }
        QAbstractItemModel* model = *modelRes;
        QString objectId = p[QStringLiteral("objectId")].toString();

        // parent
        QList<int> parentPath;
        QJsonArray parentArr = p[QStringLiteral("parent")].toArray();
        for (const QJsonValue& v : parentArr)
          parentPath.append(v.toInt());

        QModelIndex parentIdx;
        if (!parentPath.isEmpty()) {
          auto parentRes = ModelNavigator::pathToIndexExpected(model, parentPath);
          if (!parentRes || !parentRes->isValid()) {
            int failed = parentRes ? 0 : parentRes.error();
            throw JsonRpcException(ErrorCode::kInvalidParentPath,
                                   QStringLiteral("Parent path invalid at segment %1").arg(failed),
                                   QJsonObject{{QStringLiteral("path"), parentArr},
                                               {QStringLiteral("failedSegment"), failed}});
          }
          parentIdx = *parentRes;
        }

        // opts
        ModelNavigator::FindOptions opts;
        opts.value = p[QStringLiteral("value")].toString();
        opts.column = p[QStringLiteral("column")].toInt(0);

        QJsonValue roleVal = p[QStringLiteral("role")];
        if (roleVal.isDouble()) {
          opts.role = roleVal.toInt();
        } else {
          QString roleName = roleVal.toString(QStringLiteral("display"));
          auto roleRes = ModelNavigator::resolveRoleNameExpected(model, roleName);
          if (!roleRes) {
            throw JsonRpcException(
                ErrorCode::kModelRoleNotFound, QStringLiteral("Role not found: %1").arg(roleName),
                QJsonObject{
                    {QStringLiteral("roleName"), roleName},
                    {QStringLiteral("availableRoles"), ModelNavigator::getRoleNames(model)}});
          }
          opts.role = *roleRes;
        }

        QString matchMode = p[QStringLiteral("match")].toString(QStringLiteral("contains"));
        if (matchMode == QStringLiteral("exact"))
          opts.match = ModelNavigator::MatchMode::Exact;
        else if (matchMode == QStringLiteral("contains"))
          opts.match = ModelNavigator::MatchMode::Contains;
        else if (matchMode == QStringLiteral("startsWith"))
          opts.match = ModelNavigator::MatchMode::StartsWith;
        else if (matchMode == QStringLiteral("endsWith"))
          opts.match = ModelNavigator::MatchMode::EndsWith;
        else if (matchMode == QStringLiteral("regex"))
          opts.match = ModelNavigator::MatchMode::Regex;
        else {
          throw JsonRpcException(JsonRpcError::kInvalidParams,
                                 QStringLiteral("Unknown match mode: %1").arg(matchMode),
                                 QJsonObject{{QStringLiteral("match"), matchMode}});
        }
        opts.maxHits = p[QStringLiteral("maxHits")].toInt(10);

        // Compile regex if needed.
        auto compileRes = ModelNavigator::compileFindOptionsExpected(opts);
        if (!compileRes) {
          throw JsonRpcException(ErrorCode::kInvalidRegex,
                                 QStringLiteral("Invalid regex: %1").arg(compileRes.error()),
                                 QJsonObject{{QStringLiteral("pattern"), opts.value},
                                             {QStringLiteral("error"), compileRes.error()}});
        }

        QJsonArray matches;
        bool truncated = ModelNavigator::findRecursive(model, parentIdx, opts, matches);

        QJsonObject result;
        result[QStringLiteral("matches")] = matches;
        result[QStringLiteral("count")] = matches.size();
        result[QStringLiteral("truncated")] = truncated;
        return envelopeToString(ResponseEnvelope::wrap(result, objectId));
      });
}

}  // namespace qtPilot
