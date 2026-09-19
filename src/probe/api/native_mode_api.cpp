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
#include <QMenu>
#include <QPointer>
#include <QTreeView>
#include <QWidget>

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

/// @brief Resolve objectId param to QObject*, throw JsonRpcException on failure.
QObject* resolveObjectParam(const QJsonObject& params, const QString& methodName) {
  QString objectId = params[QStringLiteral("objectId")].toString();
  if (objectId.isEmpty()) {
    throw JsonRpcException(JsonRpcError::kInvalidParams,
                           QStringLiteral("Missing required parameter: objectId"),
                           QJsonObject{{QStringLiteral("method"), methodName}});
  }

  QObject* obj = ObjectResolver::resolve(objectId);
  if (!obj) {
    throw JsonRpcException(
        ErrorCode::kObjectNotFound, QStringLiteral("Object not found: %1").arg(objectId),
        QJsonObject{
            {QStringLiteral("objectId"), objectId},
            {QStringLiteral("hint"),
             QStringLiteral("Use qt.objects.search or qt.objects.tree to discover valid IDs")}});
  }
  return obj;
}

/// @brief Resolve objectId param to QWidget*, throw JsonRpcException on failure.
QWidget* resolveWidgetParam(const QJsonObject& params, const QString& methodName) {
  QObject* obj = resolveObjectParam(params, methodName);
  QWidget* widget = qobject_cast<QWidget*>(obj);
  if (!widget) {
    QString objectId = params[QStringLiteral("objectId")].toString();
    throw JsonRpcException(
        ErrorCode::kObjectNotWidget, QStringLiteral("Object is not a widget: %1").arg(objectId),
        QJsonObject{
            {QStringLiteral("objectId"), objectId},
            {QStringLiteral("className"), QString::fromUtf8(obj->metaObject()->className())}});
  }
  return widget;
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

QJsonObject handleUiActivateMenuItem(const QJsonObject& params) {
  const QString kMethod = QStringLiteral("qt.ui.activateMenuItem");
  const QString text = params.value(QStringLiteral("text")).toString();
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
      // Triggering the action is what choosing the entry does. Clicking inside
      // the menu's own modal loop would be a different and much worse problem.
      action->trigger();
      menu->close();
      return QJsonObject{
          {QStringLiteral("ok"), true},
          {QStringLiteral("text"), label},
          {QStringLiteral("objectId"), ObjectRegistry::instance()->objectId(action)}};
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
  // qt.ping - measure event loop latency
  m_handler->RegisterMethod(QStringLiteral("qt.ping"), [](const QString& /*params*/) -> QString {
    qint64 before = QDateTime::currentMSecsSinceEpoch();
    QCoreApplication::processEvents();
    qint64 after = QDateTime::currentMSecsSinceEpoch();

    QJsonObject result;
    result[QStringLiteral("pong")] = true;
    result[QStringLiteral("timestamp")] = after;
    result[QStringLiteral("eventLoopLatency")] = after - before;

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
          result[QStringLiteral("properties")] = MetaInspector::listProperties(obj);
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
          rootObj = ObjectResolver::resolve(rootId);
          if (!rootObj) {
            throw JsonRpcException(
                ErrorCode::kObjectNotFound, QStringLiteral("Root object not found: %1").arg(rootId),
                QJsonObject{{QStringLiteral("method"), QStringLiteral("qt.objects.search")},
                            {QStringLiteral("root"), rootId}});
          }
        }

        QList<QObject*> candidates;
        if (!className.isEmpty()) {
          candidates = ObjectRegistry::instance()->findAllByClassName(className, rootObj);
        } else {
          candidates = ObjectRegistry::instance()->allObjects(rootObj);
        }

        QJsonArray matches;
        bool truncated = false;
        for (QObject* obj : candidates) {
          if (!objectName.isEmpty() && obj->objectName() != objectName) {
            continue;
          }
          if (!propFilters.isEmpty()) {
            bool ok = true;
            for (auto it = propFilters.constBegin(); it != propFilters.constEnd(); ++it) {
              try {
                QJsonValue actual = MetaInspector::getProperty(obj, it.key());
                if (actual != it.value()) {
                  ok = false;
                  break;
                }
              } catch (...) {
                ok = false;
                break;
              }
            }
            if (!ok)
              continue;
          }

          if (matches.size() >= limit) {
            truncated = true;
            break;
          }

          QString objId = ObjectRegistry::instance()->objectId(obj);
          int numId = ObjectResolver::assignNumericId(obj);
          QJsonObject entry;
          entry[QStringLiteral("objectId")] = objId;
          entry[QStringLiteral("className")] = QString::fromUtf8(obj->metaObject()->className());
          entry[QStringLiteral("objectName")] = obj->objectName();
          entry[QStringLiteral("numericId")] = numId;
          matches.append(entry);
        }

        QJsonObject result;
        result[QStringLiteral("objects")] = matches;
        result[QStringLiteral("count")] = matches.size();
        result[QStringLiteral("truncated")] = truncated;
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
        QObject* obj = resolveObjectParam(p, QStringLiteral("qt.properties.get"));
        QString objectId = p[QStringLiteral("objectId")].toString();
        QString name = p[QStringLiteral("name")].toString();

        if (name.isEmpty()) {
          throw JsonRpcException(
              JsonRpcError::kInvalidParams, QStringLiteral("Missing required parameter: name"),
              QJsonObject{{QStringLiteral("method"), QStringLiteral("qt.properties.get")}});
        }

        try {
          QJsonValue value = MetaInspector::getProperty(obj, name);
          QJsonObject result;
          result[QStringLiteral("value")] = value;
          return envelopeToString(ResponseEnvelope::wrap(result, objectId));
        } catch (const std::runtime_error& e) {
          throw JsonRpcException(ErrorCode::kPropertyNotFound, QString::fromStdString(e.what()),
                                 QJsonObject{{QStringLiteral("objectId"), objectId},
                                             {QStringLiteral("property"), name}});
        }
      });

  // qt.properties.set
  m_handler->RegisterMethod(
      QStringLiteral("qt.properties.set"), [](const QString& params) -> QString {
        auto p = parseParams(params);
        QObject* obj = resolveObjectParam(p, QStringLiteral("qt.properties.set"));
        QString objectId = p[QStringLiteral("objectId")].toString();
        QString name = p[QStringLiteral("name")].toString();

        if (name.isEmpty()) {
          throw JsonRpcException(
              JsonRpcError::kInvalidParams, QStringLiteral("Missing required parameter: name"),
              QJsonObject{{QStringLiteral("method"), QStringLiteral("qt.properties.set")}});
        }

        if (!p.contains(QStringLiteral("value"))) {
          throw JsonRpcException(
              JsonRpcError::kInvalidParams, QStringLiteral("Missing required parameter: value"),
              QJsonObject{{QStringLiteral("method"), QStringLiteral("qt.properties.set")}});
        }

        QJsonValue value = p[QStringLiteral("value")];

        try {
          bool ok = MetaInspector::setProperty(obj, name, value);
          if (!ok) {
            throw JsonRpcException(ErrorCode::kPropertyTypeMismatch,
                                   QStringLiteral("Property set failed"),
                                   QJsonObject{{QStringLiteral("objectId"), objectId},
                                               {QStringLiteral("property"), name}});
          }
          QJsonObject result;
          result[QStringLiteral("ok")] = true;
          return envelopeToString(ResponseEnvelope::wrap(result, objectId));
        } catch (const std::runtime_error& e) {
          // Distinguish read-only from not-found
          QString msg = QString::fromStdString(e.what());
          int code = msg.contains(QStringLiteral("read-only"), Qt::CaseInsensitive)
                         ? ErrorCode::kPropertyReadOnly
                         : ErrorCode::kPropertyTypeMismatch;
          throw JsonRpcException(code, msg,
                                 QJsonObject{{QStringLiteral("objectId"), objectId},
                                             {QStringLiteral("property"), name}});
        }
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
        QObject* obj = resolveObjectParam(p, QStringLiteral("qt.methods.invoke"));
        QString objectId = p[QStringLiteral("objectId")].toString();
        QString method = p[QStringLiteral("method")].toString();

        if (method.isEmpty()) {
          throw JsonRpcException(
              JsonRpcError::kInvalidParams, QStringLiteral("Missing required parameter: method"),
              QJsonObject{{QStringLiteral("method"), QStringLiteral("qt.methods.invoke")}});
        }

        QJsonArray args = p[QStringLiteral("args")].toArray();

        try {
          QJsonValue result = MetaInspector::invokeMethod(obj, method, args);
          QJsonObject resultObj;
          resultObj[QStringLiteral("result")] = result;
          return envelopeToString(ResponseEnvelope::wrap(resultObj, objectId));
        } catch (const std::runtime_error& e) {
          QString msg = QString::fromStdString(e.what());
          int code = msg.contains(QStringLiteral("not found"), Qt::CaseInsensitive)
                         ? ErrorCode::kMethodNotFound
                         : ErrorCode::kMethodInvocationFailed;
          throw JsonRpcException(code, msg,
                                 QJsonObject{{QStringLiteral("objectId"), objectId},
                                             {QStringLiteral("method"), method}});
        }
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

        try {
          QString subId = SignalMonitor::instance()->subscribe(objectId, signal);
          QJsonObject result;
          result[QStringLiteral("subscriptionId")] = subId;
          return envelopeToString(ResponseEnvelope::wrap(result, objectId));
        } catch (const std::runtime_error& e) {
          throw JsonRpcException(ErrorCode::kSignalNotFound, QString::fromStdString(e.what()),
                                 QJsonObject{{QStringLiteral("objectId"), objectId},
                                             {QStringLiteral("signal"), signal}});
        }
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
        int failedSegment = -1;
        QJsonObject notFoundDetail;

        if (hasPath) {
          QList<int> rowPath;
          QJsonArray pathArr = p[QStringLiteral("path")].toArray();
          for (const QJsonValue& v : pathArr)
            rowPath.append(v.toInt());
          rowTarget = ModelNavigator::pathToIndex(model, rowPath, &failedSegment);
          if (!rowTarget.isValid()) {
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
          }
        } else {
          QStringList itemPath;
          QJsonArray ipArr = p[QStringLiteral("itemPath")].toArray();
          for (const QJsonValue& v : ipArr)
            itemPath.append(v.toString());
          rowTarget = ModelNavigator::textPathToIndex(model, itemPath, column, &failedSegment);
          if (!rowTarget.isValid()) {
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
    QString filePath = p[QStringLiteral("filePath")].toString();

    if (filePath.isEmpty()) {
      throw JsonRpcException(
          JsonRpcError::kInvalidParams, QStringLiteral("Missing required parameter: filePath"),
          QJsonObject{{QStringLiteral("method"), QStringLiteral("qt.names.load")}});
    }

    bool ok = SymbolicNameMap::instance()->loadFromFile(filePath);
    if (!ok) {
      throw JsonRpcException(ErrorCode::kNameMapLoadError,
                             QStringLiteral("Failed to load name map from: %1").arg(filePath),
                             QJsonObject{{QStringLiteral("filePath"), filePath}});
    }

    QJsonObject names = SymbolicNameMap::instance()->allNames();
    QJsonObject result;
    result[QStringLiteral("ok")] = true;
    result[QStringLiteral("count")] = names.size();
    return envelopeToString(ResponseEnvelope::wrap(result));
  });

  // qt.names.save
  m_handler->RegisterMethod(QStringLiteral("qt.names.save"), [](const QString& params) -> QString {
    auto p = parseParams(params);
    QString filePath = p[QStringLiteral("filePath")].toString();

    if (filePath.isEmpty()) {
      throw JsonRpcException(
          JsonRpcError::kInvalidParams, QStringLiteral("Missing required parameter: filePath"),
          QJsonObject{{QStringLiteral("method"), QStringLiteral("qt.names.save")}});
    }

    bool ok = SymbolicNameMap::instance()->saveToFile(filePath);
    if (!ok) {
      throw JsonRpcException(ErrorCode::kNameMapLoadError,
                             QStringLiteral("Failed to save name map to: %1").arg(filePath),
                             QJsonObject{{QStringLiteral("filePath"), filePath}});
    }
    QJsonObject result;
    result[QStringLiteral("ok")] = true;
    return envelopeToString(ResponseEnvelope::wrap(result));
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
    QObject* obj = resolveObjectParam(p, QStringLiteral("qt.models.data"));
    QString objectId = p[QStringLiteral("objectId")].toString();

    QAbstractItemModel* model = ModelNavigator::resolveModel(obj);
    if (!model) {
      throw JsonRpcException(
          ErrorCode::kNotAModel,
          QStringLiteral("Object is not a model and does not have an associated model"),
          QJsonObject{{QStringLiteral("objectId"), objectId},
                      {QStringLiteral("hint"),
                       QStringLiteral("Use qt.models.list to discover available models")}});
    }

    // Resolve parent path.
    QList<int> parentPath;
    QJsonArray parentArr = p[QStringLiteral("parent")].toArray();
    for (const QJsonValue& v : parentArr)
      parentPath.append(v.toInt());

    if (!parentPath.isEmpty()) {
      int failed = -1;
      QModelIndex parentIdx = ModelNavigator::pathToIndex(model, parentPath, &failed);
      if (!parentIdx.isValid()) {
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
        int roleId = ModelNavigator::resolveRoleName(model, roleName);
        if (roleId < 0) {
          throw JsonRpcException(
              ErrorCode::kModelRoleNotFound, QStringLiteral("Role not found: %1").arg(roleName),
              QJsonObject{{QStringLiteral("roleName"), roleName},
                          {QStringLiteral("availableRoles"), ModelNavigator::getRoleNames(model)}});
        }
        resolvedRoles.append(roleId);
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
        QObject* obj = resolveObjectParam(p, QStringLiteral("qt.models.search"));
        QString objectId = p[QStringLiteral("objectId")].toString();

        QAbstractItemModel* model = ModelNavigator::resolveModel(obj);
        if (!model) {
          throw JsonRpcException(
              ErrorCode::kNotAModel,
              QStringLiteral("Object is not a model and does not have an associated model"),
              QJsonObject{{QStringLiteral("objectId"), objectId}});
        }

        // parent
        QList<int> parentPath;
        QJsonArray parentArr = p[QStringLiteral("parent")].toArray();
        for (const QJsonValue& v : parentArr)
          parentPath.append(v.toInt());

        QModelIndex parentIdx;
        if (!parentPath.isEmpty()) {
          int failed = -1;
          parentIdx = ModelNavigator::pathToIndex(model, parentPath, &failed);
          if (!parentIdx.isValid()) {
            throw JsonRpcException(ErrorCode::kInvalidParentPath,
                                   QStringLiteral("Parent path invalid at segment %1").arg(failed),
                                   QJsonObject{{QStringLiteral("path"), parentArr},
                                               {QStringLiteral("failedSegment"), failed}});
          }
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
          int roleId = ModelNavigator::resolveRoleName(model, roleName);
          if (roleId < 0) {
            throw JsonRpcException(
                ErrorCode::kModelRoleNotFound, QStringLiteral("Role not found: %1").arg(roleName),
                QJsonObject{
                    {QStringLiteral("roleName"), roleName},
                    {QStringLiteral("availableRoles"), ModelNavigator::getRoleNames(model)}});
          }
          opts.role = roleId;
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
        QString regexError;
        if (!ModelNavigator::compileFindOptions(opts, &regexError)) {
          throw JsonRpcException(ErrorCode::kInvalidRegex,
                                 QStringLiteral("Invalid regex: %1").arg(regexError),
                                 QJsonObject{{QStringLiteral("pattern"), opts.value},
                                             {QStringLiteral("error"), regexError}});
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
