// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "api/native_mode_api.h"

#include "api/error_codes.h"
#include "api/response_envelope.h"
#include "api/symbolic_name_map.h"
#include "core/object_registry.h"
#include "core/object_resolver.h"
#include "interaction/hit_test.h"
#include "interaction/input_simulator.h"
#include "interaction/screenshot.h"
#include "introspection/event_capture.h"
#include "introspection/meta_inspector.h"
#include "introspection/model_navigator.h"
#include "introspection/object_id.h"
#include "introspection/qml_inspector.h"
#include "introspection/signal_monitor.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
             QStringLiteral("Use qt.objects.find or qt.objects.tree to discover valid IDs")}});
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
// System methods: qt.ping, qt.version, qt.modes
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
    result[QStringLiteral("version")] = QStringLiteral("0.1.0");
    result[QStringLiteral("protocol")] = QStringLiteral("jsonrpc-2.0");
    result[QStringLiteral("name")] = QStringLiteral("qtPilot");
    result[QStringLiteral("mode")] = QStringLiteral("native");
    result[QStringLiteral("deprecated")] = deprecated;

    return envelopeToString(ResponseEnvelope::wrap(result));
  });

  // qt.modes - available API modes
  m_handler->RegisterMethod(QStringLiteral("qt.modes"), [](const QString& /*params*/) -> QString {
    QJsonArray modes;
    modes.append(QStringLiteral("native"));
    modes.append(QStringLiteral("computer_use"));
    modes.append(QStringLiteral("chrome"));

    return envelopeToString(ResponseEnvelope::wrap(modes));
  });
}

// ============================================================================
// Object discovery: qt.objects.*
// ============================================================================

void NativeModeApi::registerObjectMethods() {
  // qt.objects.find - find by objectName
  m_handler->RegisterMethod(
      QStringLiteral("qt.objects.find"), [](const QString& params) -> QString {
        auto p = parseParams(params);
        QString name = p[QStringLiteral("name")].toString();
        if (name.isEmpty()) {
          throw JsonRpcException(
              JsonRpcError::kInvalidParams, QStringLiteral("Missing required parameter: name"),
              QJsonObject{{QStringLiteral("method"), QStringLiteral("qt.objects.find")}});
        }

        // Optional root
        QObject* rootObj = nullptr;
        QString rootId = p[QStringLiteral("root")].toString();
        if (!rootId.isEmpty()) {
          rootObj = ObjectResolver::resolve(rootId);
        }

        QObject* found = ObjectRegistry::instance()->findByObjectName(name, rootObj);
        if (!found) {
          throw JsonRpcException(
              ErrorCode::kObjectNotFound,
              QStringLiteral("Object not found with name: %1").arg(name),
              QJsonObject{{QStringLiteral("name"), name},
                          {QStringLiteral("hint"),
                           QStringLiteral("Use qt.objects.tree to see all objects, or "
                                          "qt.objects.findByClass to search by class")}});
        }

        QString objectId = ObjectRegistry::instance()->objectId(found);
        int numericId = ObjectResolver::assignNumericId(found);

        QJsonObject result;
        result[QStringLiteral("objectId")] = objectId;
        result[QStringLiteral("className")] = QString::fromUtf8(found->metaObject()->className());
        result[QStringLiteral("numericId")] = numericId;

        return envelopeToString(ResponseEnvelope::wrap(result, objectId));
      });

  // qt.objects.findByClass - find all by class name
  m_handler->RegisterMethod(
      QStringLiteral("qt.objects.findByClass"), [](const QString& params) -> QString {
        auto p = parseParams(params);
        QString className = p[QStringLiteral("className")].toString();
        if (className.isEmpty()) {
          throw JsonRpcException(
              JsonRpcError::kInvalidParams, QStringLiteral("Missing required parameter: className"),
              QJsonObject{{QStringLiteral("method"), QStringLiteral("qt.objects.findByClass")}});
        }

        QObject* rootObj = nullptr;
        QString rootId = p[QStringLiteral("root")].toString();
        if (!rootId.isEmpty()) {
          rootObj = ObjectResolver::resolve(rootId);
        }

        QList<QObject*> found = ObjectRegistry::instance()->findAllByClassName(className, rootObj);

        QJsonArray objects;
        for (QObject* obj : found) {
          QString objId = ObjectRegistry::instance()->objectId(obj);
          int numId = ObjectResolver::assignNumericId(obj);
          QJsonObject entry;
          entry[QStringLiteral("objectId")] = objId;
          entry[QStringLiteral("className")] = QString::fromUtf8(obj->metaObject()->className());
          entry[QStringLiteral("numericId")] = numId;
          objects.append(entry);
        }

        QJsonObject result;
        result[QStringLiteral("objects")] = objects;

        return envelopeToString(ResponseEnvelope::wrap(result));
      });

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

  // qt.objects.info - basic object info
  m_handler->RegisterMethod(
      QStringLiteral("qt.objects.info"), [](const QString& params) -> QString {
        auto p = parseParams(params);
        QObject* obj = resolveObjectParam(p, QStringLiteral("qt.objects.info"));
        QString objectId = p[QStringLiteral("objectId")].toString();

        QJsonObject info = MetaInspector::objectInfo(obj);
        return envelopeToString(ResponseEnvelope::wrap(info, objectId));
      });

  // qt.objects.inspect - convenience: info + properties + methods + signals
  m_handler->RegisterMethod(
      QStringLiteral("qt.objects.inspect"), [](const QString& params) -> QString {
        auto p = parseParams(params);
        QObject* obj = resolveObjectParam(p, QStringLiteral("qt.objects.inspect"));
        QString objectId = p[QStringLiteral("objectId")].toString();

        QJsonObject result;
        result[QStringLiteral("info")] = MetaInspector::objectInfo(obj);
        result[QStringLiteral("properties")] = MetaInspector::listProperties(obj);
        result[QStringLiteral("methods")] = MetaInspector::listMethods(obj);
        result[QStringLiteral("signals")] = MetaInspector::listSignals(obj);

        return envelopeToString(ResponseEnvelope::wrap(result, objectId));
      });

  // qt.objects.query - rich query with className and property filters
  m_handler->RegisterMethod(
      QStringLiteral("qt.objects.query"), [](const QString& params) -> QString {
        auto p = parseParams(params);
        QString className = p[QStringLiteral("className")].toString();
        QJsonObject propFilters = p[QStringLiteral("properties")].toObject();

        QObject* rootObj = nullptr;
        QString rootId = p[QStringLiteral("root")].toString();
        if (!rootId.isEmpty()) {
          rootObj = ObjectResolver::resolve(rootId);
        }

        // Get candidate objects
        QList<QObject*> candidates;
        if (!className.isEmpty()) {
          candidates = ObjectRegistry::instance()->findAllByClassName(className, rootObj);
        } else {
          candidates = ObjectRegistry::instance()->allObjects();
        }

        // Filter by property values if specified
        QJsonArray matches;
        for (QObject* obj : candidates) {
          if (!propFilters.isEmpty()) {
            bool match = true;
            for (auto it = propFilters.constBegin(); it != propFilters.constEnd(); ++it) {
              try {
                QJsonValue actual = MetaInspector::getProperty(obj, it.key());
                if (actual != it.value()) {
                  match = false;
                  break;
                }
              } catch (...) {
                match = false;
                break;
              }
            }
            if (!match)
              continue;
          }

          QString objId = ObjectRegistry::instance()->objectId(obj);
          int numId = ObjectResolver::assignNumericId(obj);
          QJsonObject entry;
          entry[QStringLiteral("objectId")] = objId;
          entry[QStringLiteral("className")] = QString::fromUtf8(obj->metaObject()->className());
          entry[QStringLiteral("numericId")] = numId;
          matches.append(entry);
        }

        return envelopeToString(ResponseEnvelope::wrap(matches));
      });
}

// ============================================================================
// Properties: qt.properties.*
// ============================================================================

void NativeModeApi::registerPropertyMethods() {
  // qt.properties.list
  m_handler->RegisterMethod(
      QStringLiteral("qt.properties.list"), [](const QString& params) -> QString {
        auto p = parseParams(params);
        QObject* obj = resolveObjectParam(p, QStringLiteral("qt.properties.list"));
        QString objectId = p[QStringLiteral("objectId")].toString();

        QJsonArray props = MetaInspector::listProperties(obj);
        return envelopeToString(ResponseEnvelope::wrap(props, objectId));
      });

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
          QJsonObject result;
          result[QStringLiteral("success")] = ok;
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
  // qt.methods.list
  m_handler->RegisterMethod(
      QStringLiteral("qt.methods.list"), [](const QString& params) -> QString {
        auto p = parseParams(params);
        QObject* obj = resolveObjectParam(p, QStringLiteral("qt.methods.list"));
        QString objectId = p[QStringLiteral("objectId")].toString();

        QJsonArray methods = MetaInspector::listMethods(obj);
        return envelopeToString(ResponseEnvelope::wrap(methods, objectId));
      });

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
  // qt.signals.list
  m_handler->RegisterMethod(
      QStringLiteral("qt.signals.list"), [](const QString& params) -> QString {
        auto p = parseParams(params);
        QObject* obj = resolveObjectParam(p, QStringLiteral("qt.signals.list"));
        QString objectId = p[QStringLiteral("objectId")].toString();

        QJsonArray signalList = MetaInspector::listSignals(obj);
        return envelopeToString(ResponseEnvelope::wrap(signalList, objectId));
      });

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
        result[QStringLiteral("success")] = true;
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
  // qt.events.startCapture - start global event capture
  m_handler->RegisterMethod(QStringLiteral("qt.events.startCapture"),
                            [](const QString& /*params*/) -> QString {
                              EventCapture::instance()->startCapture();
                              QJsonObject result;
                              result[QStringLiteral("capturing")] = true;
                              return envelopeToString(ResponseEnvelope::wrap(result));
                            });

  // qt.events.stopCapture - stop global event capture
  m_handler->RegisterMethod(QStringLiteral("qt.events.stopCapture"),
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
    QWidget* widget = resolveWidgetParam(p, QStringLiteral("qt.ui.click"));
    QString objectId = p[QStringLiteral("objectId")].toString();

    QString button = p[QStringLiteral("button")].toString(QStringLiteral("left"));
    QJsonObject pos = p[QStringLiteral("position")].toObject();

    InputSimulator::MouseButton btn = InputSimulator::MouseButton::Left;
    if (button == QStringLiteral("right"))
      btn = InputSimulator::MouseButton::Right;
    else if (button == QStringLiteral("middle"))
      btn = InputSimulator::MouseButton::Middle;

    QPoint clickPos;
    if (!pos.isEmpty()) {
      clickPos = QPoint(pos[QStringLiteral("x")].toInt(), pos[QStringLiteral("y")].toInt());
    }

    InputSimulator::mouseClick(widget, btn, clickPos);

    QJsonObject result;
    result[QStringLiteral("success")] = true;
    return envelopeToString(ResponseEnvelope::wrap(result, objectId));
  });

  // qt.ui.sendKeys
  m_handler->RegisterMethod(QStringLiteral("qt.ui.sendKeys"), [](const QString& params) -> QString {
    auto p = parseParams(params);
    QWidget* widget = resolveWidgetParam(p, QStringLiteral("qt.ui.sendKeys"));
    QString objectId = p[QStringLiteral("objectId")].toString();

    QString text = p[QStringLiteral("text")].toString();
    QString sequence = p[QStringLiteral("sequence")].toString();

    if (!text.isEmpty()) {
      InputSimulator::sendText(widget, text);
    }
    if (!sequence.isEmpty()) {
      InputSimulator::sendKeySequence(widget, sequence);
    }

    QJsonObject result;
    result[QStringLiteral("success")] = true;
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
    QWidget* widget = resolveWidgetParam(p, QStringLiteral("qt.ui.geometry"));
    QString objectId = p[QStringLiteral("objectId")].toString();

    QJsonObject geo = HitTest::widgetGeometry(widget);
    return envelopeToString(ResponseEnvelope::wrap(geo, objectId));
  });

  // qt.ui.hitTest
  m_handler->RegisterMethod(QStringLiteral("qt.ui.hitTest"), [](const QString& params) -> QString {
    auto p = parseParams(params);
    int x = p[QStringLiteral("x")].toInt();
    int y = p[QStringLiteral("y")].toInt();

    QString foundId = HitTest::widgetIdAt(QPoint(x, y));
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
        result[QStringLiteral("success")] = true;
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
        result[QStringLiteral("success")] = true;
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
    result[QStringLiteral("success")] = true;
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
    QJsonObject result;
    result[QStringLiteral("success")] = ok;
    return envelopeToString(ResponseEnvelope::wrap(result));
  });
}

// ============================================================================
// QML introspection: qt.qml.*
// ============================================================================

void NativeModeApi::registerQmlMethods() {
  // qt.qml.inspect - get QML metadata for any object
  m_handler->RegisterMethod(QStringLiteral("qt.qml.inspect"), [](const QString& params) -> QString {
    auto p = parseParams(params);
    QObject* obj = resolveObjectParam(p, QStringLiteral("qt.qml.inspect"));
    QString objectId = p[QStringLiteral("objectId")].toString();

#ifndef QTPILOT_HAS_QML
    throw JsonRpcException(
        ErrorCode::kQmlNotAvailable,
        QStringLiteral("QML support not compiled (Qt Quick not found)"),
        QJsonObject{{QStringLiteral("method"), QStringLiteral("qt.qml.inspect")}});
#else
            QmlItemInfo qmlInfo = inspectQmlItem(obj);

            QJsonObject result;
            result[QStringLiteral("isQmlItem")] = qmlInfo.isQmlItem;
            if (qmlInfo.isQmlItem) {
                result[QStringLiteral("qmlId")] = qmlInfo.qmlId;
                result[QStringLiteral("qmlFile")] = qmlInfo.qmlFile;
                result[QStringLiteral("qmlTypeName")] = qmlInfo.shortTypeName;
            }

            return envelopeToString(ResponseEnvelope::wrap(result, objectId));
#endif
  });
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

  // qt.models.info - get model metadata (rows, columns, roles)
  m_handler->RegisterMethod(QStringLiteral("qt.models.info"), [](const QString& params) -> QString {
    auto p = parseParams(params);
    QObject* obj = resolveObjectParam(p, QStringLiteral("qt.models.info"));
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

    QJsonObject info = ModelNavigator::getModelInfo(model);
    return envelopeToString(ResponseEnvelope::wrap(info, objectId));
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

    int offset = p[QStringLiteral("offset")].toInt(0);
    int limit = p[QStringLiteral("limit")].toInt(-1);
    int parentRow = p[QStringLiteral("parentRow")].toInt(-1);
    int parentCol = p[QStringLiteral("parentCol")].toInt(0);

    // Resolve roles parameter
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
        ModelNavigator::getModelData(model, offset, limit, resolvedRoles, parentRow, parentCol);
    return envelopeToString(ResponseEnvelope::wrap(data, objectId));
  });
}

}  // namespace qtPilot
