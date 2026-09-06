// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "transport/jsonrpc_handler.h"

#include "core/version.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

// Introspection components
#include "core/object_registry.h"
#include "interaction/hit_test.h"
#include "interaction/input_simulator.h"
#include "interaction/screenshot.h"
#include "introspection/meta_inspector.h"
#include "introspection/object_id.h"
#include "introspection/signal_monitor.h"

#include <QPointer>
#include <QWidget>
// Unconditional: qtpilot.getGeometry casts to QWindow* outside the QML guard,
// and <QWidget> only reaches qwindowdefs.h, which forward-declares QWindow.
// qobject_cast needs the complete type.
#include <QWindow>

#ifdef QTPILOT_HAS_QML
#include <QQuickItem>
#include <QQuickWindow>
#include <QRectF>
#endif

#ifdef QTPILOT_HAS_NLOHMANN_JSON
#include <nlohmann/json.hpp>
#endif

#ifdef QTPILOT_HAS_SPDLOG
#include <spdlog/spdlog.h>
#define LOG_DEBUG(msg) spdlog::debug(msg)
#define LOG_WARN(msg) spdlog::warn(msg)
#define LOG_ERROR(msg) spdlog::error(msg)
#else
#define LOG_DEBUG(msg) qDebug() << msg
#define LOG_WARN(msg) qWarning() << msg
#define LOG_ERROR(msg) qCritical() << msg
#endif

namespace qtPilot {

#ifdef QTPILOT_HAS_NLOHMANN_JSON
using json = nlohmann::json;
#endif

JsonRpcHandler::JsonRpcHandler(QObject* parent) : QObject(parent) {
  RegisterBuiltinMethods();
}

QString JsonRpcHandler::HandleMessage(const QString& message) {
#ifdef QTPILOT_HAS_NLOHMANN_JSON
  // Use nlohmann_json for parsing
  json request;
  try {
    request = json::parse(message.toStdString());
  } catch (const json::parse_error& e) {
    qCritical() << "JSON parse error:" << e.what();
    return CreateErrorResponse("null", JsonRpcError::kParseError, "Parse error");
  }

  // Validate JSON-RPC 2.0 structure
  if (!request.contains("jsonrpc") || request["jsonrpc"] != "2.0") {
    return CreateErrorResponse("null", JsonRpcError::kInvalidRequest,
                               "Invalid Request: missing or invalid jsonrpc version");
  }

  if (!request.contains("method") || !request["method"].is_string()) {
    return CreateErrorResponse("null", JsonRpcError::kInvalidRequest,
                               "Invalid Request: missing or invalid method");
  }

  QString method = QString::fromStdString(request["method"].get<std::string>());
  QString id_str = "null";
  bool is_notification = !request.contains("id");

  if (!is_notification) {
    if (request["id"].is_string()) {
      id_str = QString("\"%1\"").arg(QString::fromStdString(request["id"].get<std::string>()));
    } else if (request["id"].is_number()) {
      id_str = QString::number(request["id"].get<int>());
    } else if (request["id"].is_null()) {
      id_str = "null";
    }
  }

  // Get params (default to empty object)
  QString params_str = "{}";
  if (request.contains("params")) {
    params_str = QString::fromStdString(request["params"].dump());
  }
#else
  // Use QJsonDocument for parsing
  QJsonParseError parseError;
  QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &parseError);

  if (parseError.error != QJsonParseError::NoError) {
    qCritical() << "JSON parse error:" << parseError.errorString();
    return CreateErrorResponse("null", JsonRpcError::kParseError, "Parse error");
  }

  QJsonObject request = doc.object();

  // Validate JSON-RPC 2.0 structure
  if (!request.contains("jsonrpc") || request["jsonrpc"].toString() != "2.0") {
    return CreateErrorResponse("null", JsonRpcError::kInvalidRequest,
                               "Invalid Request: missing or invalid jsonrpc version");
  }

  if (!request.contains("method") || !request["method"].isString()) {
    return CreateErrorResponse("null", JsonRpcError::kInvalidRequest,
                               "Invalid Request: missing or invalid method");
  }

  QString method = request["method"].toString();
  QString id_str = "null";
  bool is_notification = !request.contains("id");

  if (!is_notification) {
    QJsonValue idValue = request["id"];
    if (idValue.isString()) {
      id_str = QString("\"%1\"").arg(idValue.toString());
    } else if (idValue.isDouble()) {
      id_str = QString::number(static_cast<int>(idValue.toDouble()));
    } else if (idValue.isNull()) {
      id_str = "null";
    }
  }

  // Get params (default to empty object)
  QString params_str = "{}";
  if (request.contains("params")) {
    QJsonValue paramsValue = request["params"];
    if (paramsValue.isObject()) {
      params_str =
          QString::fromUtf8(QJsonDocument(paramsValue.toObject()).toJson(QJsonDocument::Compact));
    } else if (paramsValue.isArray()) {
      params_str =
          QString::fromUtf8(QJsonDocument(paramsValue.toArray()).toJson(QJsonDocument::Compact));
    }
  }
#endif

  // Method dispatch logging gated behind QT_LOGGING_RULES=qtPilot.jsonrpc.debug=true

  // Handle notifications by emitting signal and returning empty
  if (is_notification) {
#ifdef QTPILOT_HAS_NLOHMANN_JSON
    QJsonValue paramsValue;
    if (request.contains("params")) {
      // Convert nlohmann::json to QJsonValue
      QString paramsJson = QString::fromStdString(request["params"].dump());
      paramsValue = QJsonDocument::fromJson(paramsJson.toUtf8()).object();
    }
#else
    QJsonValue paramsValue = request.value("params");
#endif
    emit NotificationReceived(method, paramsValue);
    return QString();  // No response for notifications
  }

  // Find and invoke method handler
  auto it = methods_.find(method);
  if (it == methods_.end()) {
    return CreateErrorResponse(id_str, JsonRpcError::kMethodNotFound,
                               QString("Method not found: %1").arg(method));
  }

  try {
    QString result = it->second(params_str);
    return CreateSuccessResponse(id_str, result);
  } catch (const JsonRpcException& e) {
    qCritical() << "Method" << method << "threw structured error:" << e.errorMessage();
    return CreateErrorResponse(id_str, e.code(), e.errorMessage(), e.data());
  } catch (const std::exception& e) {
    qCritical() << "Method" << method << "threw exception:" << e.what();
    return CreateErrorResponse(id_str, JsonRpcError::kInternalError,
                               QString("Internal error: %1").arg(e.what()));
  }
}

void JsonRpcHandler::RegisterMethod(const QString& method, MethodHandler handler) {
  methods_[method] = std::move(handler);
  qDebug() << "Registered method:" << method;
}

void JsonRpcHandler::UnregisterMethod(const QString& method) {
  methods_.erase(method);
  qDebug() << "Unregistered method:" << method;
}

QString JsonRpcHandler::CreateSuccessResponse(const QString& id, const QString& result) {
  return QString(R"({"jsonrpc":"2.0","id":%1,"result":%2})").arg(id, result);
}

QString JsonRpcHandler::CreateErrorResponse(const QString& id, int code, const QString& message) {
  // Escape message for JSON
  QString escaped_message = message;
  escaped_message.replace("\\", "\\\\");
  escaped_message.replace("\"", "\\\"");
  escaped_message.replace("\n", "\\n");
  escaped_message.replace("\r", "\\r");
  escaped_message.replace("\t", "\\t");

  return QString(R"({"jsonrpc":"2.0","id":%1,"error":{"code":%2,"message":"%3"}})")
      .arg(id)
      .arg(code)
      .arg(escaped_message);
}

QString JsonRpcHandler::CreateErrorResponse(const QString& id, int code, const QString& message,
                                            const QJsonObject& data) {
  QJsonObject errorObj;
  errorObj["code"] = code;
  errorObj["message"] = message;
  if (!data.isEmpty()) {
    errorObj["data"] = data;
  }

  QJsonObject response;
  response["jsonrpc"] = "2.0";
  // Parse id - could be number, string, or null
  if (id == "null") {
    response["id"] = QJsonValue::Null;
  } else if (id.startsWith('"')) {
    response["id"] = id.mid(1, id.length() - 2);
  } else {
    bool ok = false;
    int numId = id.toInt(&ok);
    if (ok) {
      response["id"] = numId;
    } else {
      response["id"] = QJsonValue::Null;
    }
  }
  response["error"] = errorObj;

  return QString::fromUtf8(QJsonDocument(response).toJson(QJsonDocument::Compact));
}

namespace {

/// @brief A visual target resolved from an object id.
///
/// Exactly one of the pointers is non-null. Centralising the cast chain keeps
/// the five handlers that accept visual targets (click, sendKeys, screenshot,
/// getGeometry, hitTest) from each re-deriving it -- and, importantly, keeps
/// the #ifdef in one place instead of interleaving it with an if/else chain in
/// five separate handlers, where a QTPILOT_HAS_QML=OFF build (never exercised
/// in CI) could silently get a different control-flow graph.
struct VisualTarget {
  QWidget* widget = nullptr;
  QWindow* window = nullptr;  // any QWindow, including a QQuickWindow
#ifdef QTPILOT_HAS_QML
  QQuickWindow* quickWindow = nullptr;
  QQuickItem* item = nullptr;
#endif

  bool isValid() const {
    return widget != nullptr || window != nullptr
#ifdef QTPILOT_HAS_QML
           || item != nullptr
#endif
        ;
  }
};

/// @brief Cast @a obj to whichever visual type it is.
/// @throws std::runtime_error naming every type the caller accepts.
VisualTarget resolveVisualTarget(QObject* obj, const QString& id) {
  VisualTarget target;
  target.widget = qobject_cast<QWidget*>(obj);
  if (!target.widget) {
#ifdef QTPILOT_HAS_QML
    target.quickWindow = qobject_cast<QQuickWindow*>(obj);
    target.item = target.quickWindow ? nullptr : qobject_cast<QQuickItem*>(obj);
#endif
    // A QQuickWindow is a QWindow, so this also covers the Quick case; plain
    // QWindow targets (QRasterWindow, QOpenGLWindow) land here too.
    target.window = qobject_cast<QWindow*>(obj);
  }

  if (!target.isValid()) {
    throw std::runtime_error("Object is not a widget, window, or QML item: " + id.toStdString());
  }
  return target;
}

#ifdef QTPILOT_HAS_QML
/// @brief The window a rendered item belongs to.
/// @throws std::runtime_error if the item is not on a window.
QQuickWindow* requireItemWindow(QQuickItem* item, const QString& id) {
  QQuickWindow* window = item->window();
  if (!window) {
    throw std::runtime_error("QQuickItem is not on a window (not rendered): " + id.toStdString());
  }
  return window;
}
#endif

}  // namespace

void JsonRpcHandler::RegisterBuiltinMethods() {
  // ping - basic connectivity test
  RegisterMethod("ping", [](const QString& /*params*/) -> QString { return R"("pong")"; });

  // getVersion - return qtPilot version info
  RegisterMethod("getVersion", [](const QString& /*params*/) -> QString {
    QJsonObject result;
    result["version"] = kVersion;
    result["protocolVersion"] = kProtocolVersion;
    result["protocol"] = "jsonrpc-2.0";
    result["name"] = "qtPilot";
    return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
  });

  // getModes - return available API modes
  RegisterMethod("getModes", [](const QString& /*params*/) -> QString {
    QJsonArray modes;
    modes.append("native");
    modes.append("computer_use");
    modes.append("chrome");
    return QString::fromUtf8(QJsonDocument(modes).toJson(QJsonDocument::Compact));
  });

  // echo - echo back params (for testing)
  RegisterMethod("echo", [](const QString& params) -> QString { return params; });

  // qtpilot.echo - namespaced echo for integration testing (per RESEARCH.md spec)
  RegisterMethod("qtpilot.echo", [](const QString& params) -> QString { return params; });

  // ========================================================================
  // Object Discovery Methods (OBJ-01, OBJ-02, OBJ-03, OBJ-04)
  // ========================================================================

  // OBJ-01: findByObjectName
  RegisterMethod("qtpilot.findByObjectName", [](const QString& params) -> QString {
    QJsonDocument doc = QJsonDocument::fromJson(params.toUtf8());
    QString name = doc.object()["name"].toString();
    QString root = doc.object()["root"].toString();

    QObject* rootObj = root.isEmpty() ? nullptr : ObjectRegistry::instance()->findById(root);
    QObject* found = ObjectRegistry::instance()->findByObjectName(name, rootObj);

    if (!found) {
      throw std::runtime_error("Object not found: " + name.toStdString());
    }

    QString id = ObjectRegistry::instance()->objectId(found);
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"id", id}}).toJson(QJsonDocument::Compact));
  });

  // OBJ-02: findByClassName
  RegisterMethod("qtpilot.findByClassName", [](const QString& params) -> QString {
    QJsonDocument doc = QJsonDocument::fromJson(params.toUtf8());
    QString className = doc.object()["className"].toString();
    QString root = doc.object()["root"].toString();

    QObject* rootObj = root.isEmpty() ? nullptr : ObjectRegistry::instance()->findById(root);
    QList<QObject*> found = ObjectRegistry::instance()->findAllByClassName(className, rootObj);

    QJsonArray ids;
    for (QObject* obj : found) {
      ids.append(ObjectRegistry::instance()->objectId(obj));
    }

    return QString::fromUtf8(
        QJsonDocument(QJsonObject{{"ids", ids}}).toJson(QJsonDocument::Compact));
  });

  // OBJ-03: getObjectTree
  RegisterMethod("qtpilot.getObjectTree", [](const QString& params) -> QString {
    QJsonDocument doc = QJsonDocument::fromJson(params.toUtf8());
    QString root = doc.object()["root"].toString();
    int maxDepth = doc.object()["maxDepth"].toInt(-1);

    QObject* rootObj = root.isEmpty() ? nullptr : ObjectRegistry::instance()->findById(root);

    QJsonObject tree = serializeObjectTree(rootObj, maxDepth);
    return QString::fromUtf8(QJsonDocument(tree).toJson(QJsonDocument::Compact));
  });

  // OBJ-04: getObjectInfo
  RegisterMethod("qtpilot.getObjectInfo", [](const QString& params) -> QString {
    QJsonDocument doc = QJsonDocument::fromJson(params.toUtf8());
    QJsonObject params_obj = doc.object();
    QString id = params_obj["id"].toString();
    if (id.isEmpty())
      id = params_obj["objectId"].toString();

    QObject* obj = ObjectRegistry::instance()->findById(id);
    if (!obj) {
      throw std::runtime_error("Object not found: " + id.toStdString());
    }

    QJsonObject info = MetaInspector::objectInfo(obj);
    return QString::fromUtf8(QJsonDocument(info).toJson(QJsonDocument::Compact));
  });

  // ========================================================================
  // Property Methods (OBJ-05, OBJ-06, OBJ-07)
  // ========================================================================

  // OBJ-05: listProperties
  RegisterMethod("qtpilot.listProperties", [](const QString& params) -> QString {
    QJsonDocument doc = QJsonDocument::fromJson(params.toUtf8());
    QJsonObject params_obj = doc.object();
    QString id = params_obj["id"].toString();
    if (id.isEmpty())
      id = params_obj["objectId"].toString();

    QObject* obj = ObjectRegistry::instance()->findById(id);
    if (!obj) {
      throw std::runtime_error("Object not found: " + id.toStdString());
    }

    QJsonArray props = MetaInspector::listProperties(obj);
    return QString::fromUtf8(QJsonDocument(props).toJson(QJsonDocument::Compact));
  });

  // OBJ-06: getProperty
  RegisterMethod("qtpilot.getProperty", [](const QString& params) -> QString {
    QJsonDocument doc = QJsonDocument::fromJson(params.toUtf8());
    QJsonObject params_obj = doc.object();
    QString id = params_obj["id"].toString();
    if (id.isEmpty())
      id = params_obj["objectId"].toString();
    QString name = doc.object()["name"].toString();

    QObject* obj = ObjectRegistry::instance()->findById(id);
    if (!obj) {
      throw std::runtime_error("Object not found: " + id.toStdString());
    }

    QJsonValue value = MetaInspector::getProperty(obj, name);
    return QString::fromUtf8(
        QJsonDocument(QJsonObject{{"value", value}}).toJson(QJsonDocument::Compact));
  });

  // OBJ-07: setProperty
  RegisterMethod("qtpilot.setProperty", [](const QString& params) -> QString {
    QJsonDocument doc = QJsonDocument::fromJson(params.toUtf8());
    QJsonObject params_obj = doc.object();
    QString id = params_obj["id"].toString();
    if (id.isEmpty())
      id = params_obj["objectId"].toString();
    QString name = doc.object()["name"].toString();
    QJsonValue value = doc.object()["value"];

    QObject* obj = ObjectRegistry::instance()->findById(id);
    if (!obj) {
      throw std::runtime_error("Object not found: " + id.toStdString());
    }

    bool ok = MetaInspector::setProperty(obj, name, value);
    return QString::fromUtf8(
        QJsonDocument(QJsonObject{{"success", ok}}).toJson(QJsonDocument::Compact));
  });

  // ========================================================================
  // Method Invocation (OBJ-08, OBJ-09, OBJ-10)
  // ========================================================================

  // OBJ-08: listMethods
  RegisterMethod("qtpilot.listMethods", [](const QString& params) -> QString {
    QJsonDocument doc = QJsonDocument::fromJson(params.toUtf8());
    QJsonObject params_obj = doc.object();
    QString id = params_obj["id"].toString();
    if (id.isEmpty())
      id = params_obj["objectId"].toString();

    QObject* obj = ObjectRegistry::instance()->findById(id);
    if (!obj) {
      throw std::runtime_error("Object not found: " + id.toStdString());
    }

    QJsonArray methods = MetaInspector::listMethods(obj);
    return QString::fromUtf8(QJsonDocument(methods).toJson(QJsonDocument::Compact));
  });

  // OBJ-09: invokeMethod
  RegisterMethod("qtpilot.invokeMethod", [](const QString& params) -> QString {
    QJsonDocument doc = QJsonDocument::fromJson(params.toUtf8());
    QJsonObject params_obj = doc.object();
    QString id = params_obj["id"].toString();
    if (id.isEmpty())
      id = params_obj["objectId"].toString();
    QString method = doc.object()["method"].toString();
    QJsonArray args = doc.object()["args"].toArray();

    QObject* obj = ObjectRegistry::instance()->findById(id);
    if (!obj) {
      throw std::runtime_error("Object not found: " + id.toStdString());
    }

    QJsonValue result = MetaInspector::invokeMethod(obj, method, args);
    return QString::fromUtf8(
        QJsonDocument(QJsonObject{{"result", result}}).toJson(QJsonDocument::Compact));
  });

  // OBJ-10: listSignals
  RegisterMethod("qtpilot.listSignals", [](const QString& params) -> QString {
    QJsonDocument doc = QJsonDocument::fromJson(params.toUtf8());
    QJsonObject params_obj = doc.object();
    QString id = params_obj["id"].toString();
    if (id.isEmpty())
      id = params_obj["objectId"].toString();

    QObject* obj = ObjectRegistry::instance()->findById(id);
    if (!obj) {
      throw std::runtime_error("Object not found: " + id.toStdString());
    }

    QJsonArray signalList = MetaInspector::listSignals(obj);
    return QString::fromUtf8(QJsonDocument(signalList).toJson(QJsonDocument::Compact));
  });

  // ========================================================================
  // Signal Monitoring (SIG-01, SIG-02)
  // ========================================================================

  // SIG-01: subscribeSignal
  RegisterMethod("qtpilot.subscribeSignal", [](const QString& params) -> QString {
    QJsonDocument doc = QJsonDocument::fromJson(params.toUtf8());
    QString objectId = doc.object()["objectId"].toString();
    QString signalName = doc.object()["signal"].toString();

    QString subId = SignalMonitor::instance()->subscribe(objectId, signalName);
    return QString::fromUtf8(
        QJsonDocument(QJsonObject{{"subscriptionId", subId}}).toJson(QJsonDocument::Compact));
  });

  // SIG-02: unsubscribeSignal
  RegisterMethod("qtpilot.unsubscribeSignal", [](const QString& params) -> QString {
    QJsonDocument doc = QJsonDocument::fromJson(params.toUtf8());
    QString subId = doc.object()["subscriptionId"].toString();

    SignalMonitor::instance()->unsubscribe(subId);
    return QString::fromUtf8(
        QJsonDocument(QJsonObject{{"success", true}}).toJson(QJsonDocument::Compact));
  });

  // Lifecycle notifications toggle
  RegisterMethod("qtpilot.setLifecycleNotifications", [](const QString& params) -> QString {
    QJsonDocument doc = QJsonDocument::fromJson(params.toUtf8());
    bool enabled = doc.object()["enabled"].toBool();

    SignalMonitor::instance()->setLifecycleNotificationsEnabled(enabled);
    return QString::fromUtf8(
        QJsonDocument(QJsonObject{{"enabled", enabled}}).toJson(QJsonDocument::Compact));
  });

  // ========================================================================
  // UI Interaction (UI-01, UI-02, UI-03, UI-04, UI-05)
  // ========================================================================

  // UI-01: click
  RegisterMethod("qtpilot.click", [](const QString& params) -> QString {
    QJsonDocument doc = QJsonDocument::fromJson(params.toUtf8());
    QJsonObject params_obj = doc.object();
    QString id = params_obj["id"].toString();
    if (id.isEmpty())
      id = params_obj["objectId"].toString();
    QString button = doc.object()["button"].toString("left");
    QJsonObject pos = doc.object()["position"].toObject();

    if (doc.object().contains("position") &&
        (pos.isEmpty() || !pos["x"].isDouble() || !pos["y"].isDouble())) {
      throw JsonRpcException(JsonRpcError::kInvalidParams,
                             QStringLiteral("position requires numeric 'x' and 'y'"));
    }

    QObject* obj = ObjectRegistry::instance()->findById(id);
    if (!obj) {
      throw std::runtime_error("Object not found: " + id.toStdString());
    }

    InputSimulator::MouseButton btn = InputSimulator::MouseButton::Left;
    if (button == "right")
      btn = InputSimulator::MouseButton::Right;
    else if (button == "middle")
      btn = InputSimulator::MouseButton::Middle;

    // Distinguish "no position given" from an explicit {0,0}. Testing the
    // QPoint would conflate them -- QPoint(0,0).isNull() is true -- so an
    // explicit top-left click would silently become a centre click.
    const bool hasPos = !pos.isEmpty();
    const QPoint clickPos = hasPos ? QPoint(pos["x"].toInt(), pos["y"].toInt()) : QPoint();

    const VisualTarget target = resolveVisualTarget(obj, id);
    if (target.widget) {
      if (hasPos) {
        InputSimulator::mouseClickAt(target.widget, btn, clickPos);
      } else {
        InputSimulator::mouseClick(target.widget, btn);
      }
    }
#ifdef QTPILOT_HAS_QML
    else if (target.item) {
      QQuickWindow* w = requireItemWindow(target.item, id);
      // An item-relative position (or its centre) has to become scene coords,
      // since that is the only space a QQuickWindow accepts.
      const QPointF itemPos =
          hasPos ? QPointF(clickPos) : QPointF(target.item->width() / 2, target.item->height() / 2);
      InputSimulator::mouseClick(w, btn, target.item->mapToScene(itemPos).toPoint());
    }
#endif
    else if (target.window) {
      InputSimulator::mouseClick(
          target.window, btn,
          hasPos ? clickPos : QPoint(target.window->width() / 2, target.window->height() / 2));
    }

    return QString::fromUtf8(
        QJsonDocument(QJsonObject{{"success", true}}).toJson(QJsonDocument::Compact));
  });

  // UI-02: sendKeys
  RegisterMethod("qtpilot.sendKeys", [](const QString& params) -> QString {
    QJsonDocument doc = QJsonDocument::fromJson(params.toUtf8());
    QJsonObject params_obj = doc.object();
    QString id = params_obj["id"].toString();
    if (id.isEmpty())
      id = params_obj["objectId"].toString();
    QString text = doc.object()["text"].toString();
    QString sequence = doc.object()["sequence"].toString();

    // Reject a request that would do nothing. Without this a typo'd parameter
    // name reports success while sending no input at all -- and on the QML path
    // it would still move focus as a side effect.
    if (text.isEmpty() && sequence.isEmpty()) {
      throw JsonRpcException(JsonRpcError::kInvalidParams,
                             QStringLiteral("sendKeys requires a non-empty 'text' or 'sequence'"));
    }

    QObject* obj = ObjectRegistry::instance()->findById(id);
    if (!obj) {
      throw std::runtime_error("Object not found: " + id.toStdString());
    }

    const VisualTarget target = resolveVisualTarget(obj, id);
    if (target.widget) {
      if (!text.isEmpty()) {
        InputSimulator::sendText(target.widget, text);
      }
      if (!sequence.isEmpty()) {
        InputSimulator::sendKeySequence(target.widget, sequence);
      }
    }
#ifdef QTPILOT_HAS_QML
    else if (target.item) {
      // The QWidget path calls setFocus() before typing; do the same here, or
      // the text lands on whichever item happened to hold focus already.
      //
      // forceActiveFocus() runs QML focus handlers synchronously and each
      // InputSimulator primitive pumps the event loop, so the item -- and the
      // window derived from it -- can be destroyed underneath us. Guard both
      // and re-derive the window after focus, since focus reparenting can even
      // move an item to a different window.
      QPointer<QQuickItem> itemGuard(target.item);
      requireItemWindow(target.item, id);  // fail fast if not rendered
      target.item->forceActiveFocus();
      if (!itemGuard) {
        throw std::runtime_error("Target was destroyed during focus change: " + id.toStdString());
      }

      QPointer<QQuickWindow> windowGuard(requireItemWindow(itemGuard, id));
      if (!text.isEmpty()) {
        InputSimulator::sendText(windowGuard, text);
        if (!itemGuard) {
          throw std::runtime_error("Target was destroyed while typing: " + id.toStdString());
        }
        if (!windowGuard) {
          throw std::runtime_error("Target window was destroyed while typing: " + id.toStdString());
        }
        if (itemGuard->window() != windowGuard.data()) {
          throw std::runtime_error("Target moved to another window while typing: " +
                                   id.toStdString());
        }
      }
      if (!sequence.isEmpty()) {
        InputSimulator::sendKeySequence(windowGuard, sequence);
      }
    }
#endif
    else if (target.window) {
      QPointer<QWindow> windowGuard(target.window);
      if (!text.isEmpty()) {
        InputSimulator::sendText(windowGuard, text);
        if (!windowGuard) {
          throw std::runtime_error("Target window was destroyed while typing: " + id.toStdString());
        }
      }
      if (!sequence.isEmpty()) {
        InputSimulator::sendKeySequence(windowGuard, sequence);
      }
    }

    return QString::fromUtf8(
        QJsonDocument(QJsonObject{{"success", true}}).toJson(QJsonDocument::Compact));
  });

  // UI-03: screenshot
  RegisterMethod("qtpilot.screenshot", [](const QString& params) -> QString {
    QJsonDocument doc = QJsonDocument::fromJson(params.toUtf8());
    QJsonObject params_obj = doc.object();
    QString id = params_obj["id"].toString();
    if (id.isEmpty())
      id = params_obj["objectId"].toString();
    bool fullWindow = doc.object()["fullWindow"].toBool(false);
    QJsonObject region = doc.object()["region"].toObject();

    QObject* obj = ObjectRegistry::instance()->findById(id);
    if (!obj) {
      throw std::runtime_error("Object not found: " + id.toStdString());
    }

    auto rectFromRegion = [&region]() {
      return QRect(region["x"].toInt(), region["y"].toInt(), region["width"].toInt(),
                   region["height"].toInt());
    };

    QByteArray base64;
    const VisualTarget target = resolveVisualTarget(obj, id);
    if (target.widget) {
      if (fullWindow) {
        base64 = Screenshot::captureWindow(target.widget);
      } else if (!region.isEmpty()) {
        base64 = Screenshot::captureRegion(target.widget, rectFromRegion());
      } else {
        base64 = Screenshot::captureWidget(target.widget);
      }
    }
#ifdef QTPILOT_HAS_QML
    // Qt Quick has no QWidget: grab via the QQuickWindow (QQuickWindow::grabWindow,
    // offscreen -- no screen-recording permission needed), cropping to the item.
    else if (target.quickWindow) {
      base64 = region.isEmpty() ? Screenshot::captureWindow(target.quickWindow)
                                : Screenshot::captureRegion(target.quickWindow, rectFromRegion());
    } else if (target.item) {
      QQuickWindow* w = requireItemWindow(target.item, id);
      if (fullWindow) {
        base64 = Screenshot::captureWindow(w);
      } else if (!region.isEmpty()) {
        base64 = Screenshot::captureRegion(w, rectFromRegion());
      } else {
        // Crop the window grab to the item's bounds (scene coords, logical px).
        const QRectF sceneRect =
            target.item->mapRectToScene(QRectF(0, 0, target.item->width(), target.item->height()));
        base64 = Screenshot::captureRegion(w, sceneRect.toRect());
      }
    }
#endif
    else {
      throw std::runtime_error("Screenshot target is not capturable: " + id.toStdString());
    }

    return QString::fromUtf8(QJsonDocument(QJsonObject{{"image", QString::fromLatin1(base64)}})
                                 .toJson(QJsonDocument::Compact));
  });

  // UI-04: getGeometry
  RegisterMethod("qtpilot.getGeometry", [](const QString& params) -> QString {
    QJsonDocument doc = QJsonDocument::fromJson(params.toUtf8());
    QJsonObject params_obj = doc.object();
    QString id = params_obj["id"].toString();
    if (id.isEmpty())
      id = params_obj["objectId"].toString();

    QObject* obj = ObjectRegistry::instance()->findById(id);
    if (!obj) {
      throw std::runtime_error("Object not found: " + id.toStdString());
    }

    QJsonObject geo;
    const VisualTarget target = resolveVisualTarget(obj, id);
    if (target.widget) {
      geo = HitTest::widgetGeometry(target.widget);
    }
#ifdef QTPILOT_HAS_QML
    else if (target.item) {
      geo = HitTest::itemGeometry(target.item);
    }
#endif
    else {
      geo = HitTest::windowGeometry(target.window);
    }

    return QString::fromUtf8(QJsonDocument(geo).toJson(QJsonDocument::Compact));
  });

  // UI-05: hitTest
  RegisterMethod("qtpilot.hitTest", [](const QString& params) -> QString {
    QJsonDocument doc = QJsonDocument::fromJson(params.toUtf8());
    int x = doc.object()["x"].toInt();
    int y = doc.object()["y"].toInt();
    QString parentId = doc.object()["parentId"].toString();

    QString foundId;
    if (!parentId.isEmpty()) {
      QObject* parentObj = ObjectRegistry::instance()->findById(parentId);
      if (!parentObj) {
        throw std::runtime_error("Parent object not found: " + parentId.toStdString());
      }

      const VisualTarget parentTarget = resolveVisualTarget(parentObj, parentId);
      if (parentTarget.widget) {
        QWidget* child = HitTest::childAt(parentTarget.widget, QPoint(x, y));
        if (child) {
          foundId = ObjectRegistry::instance()->objectId(child);
        }
      }
#ifdef QTPILOT_HAS_QML
      // For QML the position is scene (window-local) coordinates.
      else if (parentTarget.quickWindow) {
        if (QQuickItem* hit = HitTest::itemAt(parentTarget.quickWindow, QPointF(x, y))) {
          foundId = ObjectRegistry::instance()->objectId(hit);
        }
      } else if (parentTarget.item) {
        QQuickWindow* w = requireItemWindow(parentTarget.item, parentId);
        // Position is relative to the given item, so lift it into scene space.
        if (QQuickItem* hit = HitTest::itemAt(w, parentTarget.item->mapToScene(QPointF(x, y)))) {
          foundId = ObjectRegistry::instance()->objectId(hit);
        }
      }
#endif
      else {
        throw std::runtime_error("Parent is not hit-testable: " + parentId.toStdString());
      }
    } else {
      foundId = HitTest::widgetIdAt(QPoint(x, y));
#ifdef QTPILOT_HAS_QML
      // A pure Qt Quick app has no widget anywhere, so fall back to scanning
      // top-level QQuickWindows before reporting nothing.
      if (foundId.isEmpty()) {
        foundId = HitTest::quickItemIdAt(QPoint(x, y));
      }
#endif
    }

    if (foundId.isEmpty()) {
      return QString::fromUtf8(
          QJsonDocument(QJsonObject{{"id", QJsonValue::Null}}).toJson(QJsonDocument::Compact));
    }

    return QString::fromUtf8(
        QJsonDocument(QJsonObject{{"id", foundId}}).toJson(QJsonDocument::Compact));
  });
}

}  // namespace qtPilot
