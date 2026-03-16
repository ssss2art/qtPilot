// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "core/probe.h"

#include <stdexcept>

#include <QCoreApplication>
#include <QDebug>

// Logging macros - use spdlog if available, otherwise Qt logging
#ifdef QTPILOT_HAS_SPDLOG
#include <spdlog/spdlog.h>
#define LOG_INFO(msg) spdlog::info(msg)
#define LOG_WARN(msg) spdlog::warn(msg)
#define LOG_ERROR(msg) spdlog::error(msg)
#else
#define LOG_INFO(msg) qInfo() << msg
#define LOG_WARN(msg) qWarning() << msg
#define LOG_ERROR(msg) qCritical() << msg
#endif

#include "accessibility/console_message_capture.h"
#include "api/chrome_mode_api.h"
#include "api/computer_use_mode_api.h"
#include "api/native_mode_api.h"
#include "api/symbolic_name_map.h"
#include "core/object_registry.h"
#include "core/object_resolver.h"
#include "introspection/event_capture.h"
#include "introspection/signal_monitor.h"
#include "transport/discovery_broadcaster.h"
#include "transport/websocket_server.h"

#ifdef Q_OS_WIN
#include "core/child_injector_windows.h"
#endif

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QWindow>

namespace qtPilot {

// Thread-safe singleton storage using Q_GLOBAL_STATIC
// This is safe for dynamically loaded DLLs (no TLS issues)
Q_GLOBAL_STATIC(Probe, s_probeInstance)

Probe* Probe::instance() {
  return s_probeInstance();
}

Probe::Probe()
    : QObject(nullptr),
      m_server(nullptr),
      m_port(9222),
      m_mode(QStringLiteral("all")),
      m_initialized(false),
      m_running(false) {
  // Read configuration from environment variables
  readConfiguration();

  // Log creation to stderr for debugging injection issues
  // Using fprintf because qDebug may not work before Qt is fully initialized
  fprintf(stderr, "[qtPilot] Probe singleton created (port=%u, mode=%s)\n",
          static_cast<unsigned>(m_port), qPrintable(m_mode));
}

Probe::~Probe() {
  shutdown();
  fprintf(stderr, "[qtPilot] Probe singleton destroyed\n");
}

void Probe::readConfiguration() {
  // Read port from environment
  QByteArray portEnv = qgetenv("QTPILOT_PORT");
  if (!portEnv.isEmpty()) {
    bool ok = false;
    int envPort = portEnv.toInt(&ok);
    if (ok && envPort >= 0 && envPort <= 65535) {
      m_port = static_cast<quint16>(envPort);
    }
  }

  // Read mode from environment
  QByteArray modeEnv = qgetenv("QTPILOT_MODE");
  if (!modeEnv.isEmpty()) {
    QString modeStr = QString::fromUtf8(modeEnv).toLower();
    if (modeStr == QLatin1String("native") || modeStr == QLatin1String("computer_use") ||
        modeStr == QLatin1String("chrome") || modeStr == QLatin1String("all")) {
      m_mode = modeStr;
    }
  }
}

bool Probe::initialize() {
  // Guard against multiple initialization
  if (m_initialized) {
    LOG_WARN("[qtPilot] Probe::initialize() called but already initialized");
    return true;
  }

  // CRITICAL: Verify QCoreApplication exists before using any Qt functionality
  // This is the main safety check for deferred initialization
  if (!QCoreApplication::instance()) {
    LOG_ERROR("[qtPilot] Probe::initialize() called but QCoreApplication does not exist!");
    fprintf(stderr, "[qtPilot] ERROR: Cannot initialize - QCoreApplication not created yet\n");
    return false;
  }

  // Mark as initialized first to prevent re-entry
  m_initialized = true;

  // Install object hooks for tracking QObjects
  installObjectHooks();

  // Scan objects that existed before hook installation
  // This includes QCoreApplication and any widgets created early
  ObjectRegistry* registry = ObjectRegistry::instance();

  // Scan QCoreApplication (and all its children)
  if (QCoreApplication::instance()) {
    registry->scanExistingObjects(QCoreApplication::instance());
  }

  // For GUI apps, also scan top-level windows/widgets
  // QGuiApplication::allWindows() returns QWindow*, which are QObjects
  if (auto* guiApp = qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
    for (QWindow* window : guiApp->allWindows()) {
      registry->scanExistingObjects(window);
    }
  }

  LOG_INFO("[qtPilot] Object hooks installed, tracking " +
           QString::number(registry->objectCount()) + " existing objects");
  fprintf(stderr, "[qtPilot] Object hooks installed, tracking %d existing objects\n",
          registry->objectCount());

  // Create and start WebSocket server
  m_server = new WebSocketServer(m_port, this);

  // Connect server signals to probe signals for external monitoring
  connect(m_server, &WebSocketServer::clientConnected, this, &Probe::clientConnected);
  connect(m_server, &WebSocketServer::clientDisconnected, this, &Probe::clientDisconnected);
  connect(m_server, &WebSocketServer::errorOccurred, this, &Probe::errorOccurred);

  // Start the server
  if (!m_server->start()) {
    LOG_ERROR("[qtPilot] Failed to start WebSocket server");
    fprintf(stderr, "[qtPilot] ERROR: Failed to start WebSocket server on port %u\n",
            static_cast<unsigned>(m_port));
    delete m_server;
    m_server = nullptr;
    m_initialized = false;
    return false;
  }

  // Sync actual port (may differ from requested if port 0 was used)
  m_port = m_server->port();

  // Ensure child processes auto-assign their own port to avoid conflicts
  qputenv("QTPILOT_PORT", "0");

#ifdef Q_OS_WIN
  // If child injection is requested, hook CreateProcessW so children get the probe
  if (qgetenv("QTPILOT_INJECT_CHILDREN") == "1") {
    installChildProcessHook();
  }
#endif

  m_running = true;

  // Start UDP discovery broadcaster
  m_broadcaster = new DiscoveryBroadcaster(m_port, m_mode, this);
  m_broadcaster->start();

  // Install console message capture (before API registration so early messages are caught)
  try {
    ConsoleMessageCapture::instance()->install();
    fprintf(stderr, "[qtPilot] Console message capture installed\n");
  } catch (...) {
    fprintf(stderr, "[qtPilot] WARNING: Failed to install console message capture\n");
  }

  // Register Native Mode API (qt.* namespaced methods)
  try {
    auto* nativeApi = new NativeModeApi(m_server->rpcHandler(), this);
    Q_UNUSED(nativeApi);
    fprintf(stderr, "[qtPilot] Native Mode API (qt.*) registered\n");
  } catch (const std::exception& e) {
    fprintf(stderr, "[qtPilot] WARNING: Failed to register Native Mode API: %s\n", e.what());
  } catch (...) {
    fprintf(stderr, "[qtPilot] WARNING: Failed to register Native Mode API (unknown exception)\n");
  }

  // Register Computer Use Mode API (cu.* namespaced methods)
  try {
    auto* cuApi = new ComputerUseModeApi(m_server->rpcHandler(), this);
    Q_UNUSED(cuApi);
    fprintf(stderr, "[qtPilot] Computer Use Mode API (cu.*) registered\n");
  } catch (const std::exception& e) {
    fprintf(stderr, "[qtPilot] WARNING: Failed to register Computer Use Mode API: %s\n", e.what());
  } catch (...) {
    fprintf(stderr,
            "[qtPilot] WARNING: Failed to register Computer Use Mode API (unknown exception)\n");
  }

  // Register Chrome Mode API (chr.* namespaced methods)
  try {
    auto* chrApi = new ChromeModeApi(m_server->rpcHandler(), this);
    Q_UNUSED(chrApi);
    fprintf(stderr, "[qtPilot] Chrome Mode API (chr.*) registered\n");
  } catch (const std::exception& e) {
    fprintf(stderr, "[qtPilot] WARNING: Failed to register Chrome Mode API: %s\n", e.what());
  } catch (...) {
    fprintf(stderr, "[qtPilot] WARNING: Failed to register Chrome Mode API (unknown exception)\n");
  }

  // Auto-load symbolic name map from env var or default file
  QString nameMapPath = qEnvironmentVariable("QTPILOT_NAME_MAP");
  if (nameMapPath.isEmpty()) {
    nameMapPath = QDir::currentPath() + QStringLiteral("/qtPilot-names.json");
  }
  if (QFile::exists(nameMapPath)) {
    SymbolicNameMap::instance()->loadFromFile(nameMapPath);
    LOG_INFO("[qtPilot] Loaded symbolic name map from " + nameMapPath);
    fprintf(stderr, "[qtPilot] Loaded name map from %s\n", qPrintable(nameMapPath));
  }

  // Clear numeric IDs and Chrome Mode refs on client disconnect
  connect(m_server, &WebSocketServer::clientDisconnected, this, []() {
    ObjectResolver::clearNumericIds();
    ChromeModeApi::clearRefs();
  });

  // Wire signal notifications to WebSocket client
  connect(SignalMonitor::instance(), &SignalMonitor::signalEmitted, this,
          [this](const QJsonObject& notification) {
            if (m_server) {
              QJsonObject rpcNotification;
              rpcNotification["jsonrpc"] = "2.0";
              rpcNotification["method"] = "qtpilot.signalEmitted";
              rpcNotification["params"] = notification;
              m_server->sendNotification(
                  QString::fromUtf8(QJsonDocument(rpcNotification).toJson(QJsonDocument::Compact)));
            }
          });

  connect(SignalMonitor::instance(), &SignalMonitor::objectCreated, this,
          [this](const QJsonObject& notification) {
            if (m_server) {
              QJsonObject rpcNotification;
              rpcNotification["jsonrpc"] = "2.0";
              rpcNotification["method"] = "qtpilot.objectCreated";
              rpcNotification["params"] = notification;
              m_server->sendNotification(
                  QString::fromUtf8(QJsonDocument(rpcNotification).toJson(QJsonDocument::Compact)));
            }
          });

  connect(SignalMonitor::instance(), &SignalMonitor::objectDestroyed, this,
          [this](const QJsonObject& notification) {
            if (m_server) {
              QJsonObject rpcNotification;
              rpcNotification["jsonrpc"] = "2.0";
              rpcNotification["method"] = "qtpilot.objectDestroyed";
              rpcNotification["params"] = notification;
              m_server->sendNotification(
                  QString::fromUtf8(QJsonDocument(rpcNotification).toJson(QJsonDocument::Compact)));
            }
          });

  // Wire event capture notifications to WebSocket client
  connect(EventCapture::instance(), &EventCapture::eventCaptured, this,
          [this](const QJsonObject& notification) {
            if (m_server) {
              QJsonObject rpcNotification;
              rpcNotification["jsonrpc"] = "2.0";
              rpcNotification["method"] = "qtpilot.eventCaptured";
              rpcNotification["params"] = notification;
              m_server->sendNotification(
                  QString::fromUtf8(QJsonDocument(rpcNotification).toJson(QJsonDocument::Compact)));
            }
          });

  LOG_INFO("[qtPilot] Probe initialized successfully");
  fprintf(stderr, "[qtPilot] Probe initialized on port %u\n", static_cast<unsigned>(m_port));

  return true;
}

bool Probe::isInitialized() const {
  return m_initialized;
}

void Probe::shutdown() {
  if (!m_initialized) {
    return;
  }

  LOG_INFO("[qtPilot] Probe shutting down...");
  fprintf(stderr, "[qtPilot] Probe shutting down\n");

#ifdef Q_OS_WIN
  // Remove the CreateProcessW hook before tearing down anything else
  uninstallChildProcessHook();
#endif

  // Uninstall object hooks first (before any Qt objects are destroyed)
  uninstallObjectHooks();

  // Stop discovery broadcaster (sends goodbye)
  if (m_broadcaster) {
    m_broadcaster->stop();
    delete m_broadcaster;
    m_broadcaster = nullptr;
  }

  // Stop and delete WebSocket server
  if (m_server) {
    m_server->stop();
    delete m_server;
    m_server = nullptr;
  }

  m_running = false;
  m_initialized = false;
}

void Probe::setPort(quint16 port) {
  if (m_initialized) {
    LOG_WARN("[qtPilot] Cannot change port after initialization");
    return;
  }
  m_port = port;
}

quint16 Probe::port() const {
  return m_port;
}

QString Probe::mode() const {
  return m_mode;
}

bool Probe::isRunning() const {
  return m_running;
}

WebSocketServer* Probe::server() const {
  return m_server;
}

}  // namespace qtPilot
