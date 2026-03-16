// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "transport/jsonrpc_handler.h"

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

#include <QWidget>

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

  qDebug() << "Handling method:" << method << "with params:" << params_str;

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

void JsonRpcHandler::RegisterBuiltinMethods() {
  // ping - basic connectivity test
  RegisterMethod("ping", [](const QString& /*params*/) -> QString { return R"("pong")"; });

  // getVersion - return qtPilot version info
  RegisterMethod("getVersion", [](const QString& /*params*/) -> QString {
    QJsonObject result;
    result["version"] = "0.1.0";
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

    QObject* obj = ObjectRegistry::instance()->findById(id);
    if (!obj) {
      throw std::runtime_error("Object not found: " + id.toStdString());
    }

    QWidget* widget = qobject_cast<QWidget*>(obj);
    if (!widget) {
      throw std::runtime_error("Object is not a widget: " + id.toStdString());
    }

    InputSimulator::MouseButton btn = InputSimulator::MouseButton::Left;
    if (button == "right")
      btn = InputSimulator::MouseButton::Right;
    else if (button == "middle")
      btn = InputSimulator::MouseButton::Middle;

    QPoint clickPos;
    if (!pos.isEmpty()) {
      clickPos = QPoint(pos["x"].toInt(), pos["y"].toInt());
    }

    InputSimulator::mouseClick(widget, btn, clickPos);
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

    QObject* obj = ObjectRegistry::instance()->findById(id);
    if (!obj) {
      throw std::runtime_error("Object not found: " + id.toStdString());
    }

    QWidget* widget = qobject_cast<QWidget*>(obj);
    if (!widget) {
      throw std::runtime_error("Object is not a widget: " + id.toStdString());
    }

    if (!text.isEmpty()) {
      InputSimulator::sendText(widget, text);
    }
    if (!sequence.isEmpty()) {
      InputSimulator::sendKeySequence(widget, sequence);
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

    QWidget* widget = qobject_cast<QWidget*>(obj);
    if (!widget) {
      throw std::runtime_error("Object is not a widget: " + id.toStdString());
    }

    QByteArray base64;
    if (fullWindow) {
      base64 = Screenshot::captureWindow(widget);
    } else if (!region.isEmpty()) {
      QRect rect(region["x"].toInt(), region["y"].toInt(), region["width"].toInt(),
                 region["height"].toInt());
      base64 = Screenshot::captureRegion(widget, rect);
    } else {
      base64 = Screenshot::captureWidget(widget);
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

    QWidget* widget = qobject_cast<QWidget*>(obj);
    if (!widget) {
      throw std::runtime_error("Object is not a widget: " + id.toStdString());
    }

    QJsonObject geo = HitTest::widgetGeometry(widget);
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

      QWidget* parent = qobject_cast<QWidget*>(parentObj);
      if (!parent) {
        throw std::runtime_error("Parent is not a widget");
      }

      QWidget* child = HitTest::childAt(parent, QPoint(x, y));
      if (child) {
        foundId = ObjectRegistry::instance()->objectId(child);
      }
    } else {
      foundId = HitTest::widgetIdAt(QPoint(x, y));
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
