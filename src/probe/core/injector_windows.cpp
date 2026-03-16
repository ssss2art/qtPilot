// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "core/injector.h"

#ifdef QTPILOT_PLATFORM_WINDOWS

#include <QCoreApplication>
#include <QDebug>
#include <QTimer>

#ifdef QTPILOT_HAS_SPDLOG
#include <spdlog/spdlog.h>
#define LOG_INFO(msg) spdlog::info(msg)
#else
#define LOG_INFO(msg) qInfo() << msg
#endif

#include "core/probe.h"

#include <windows.h>

namespace qtPilot {

void InitializeProbe() {
  // Check if probe is disabled
  QByteArray enabled_env = qgetenv("QTPILOT_ENABLED");
  if (!enabled_env.isEmpty() && enabled_env == "0") {
    LOG_INFO("qtPilot Probe disabled via QTPILOT_ENABLED=0");
    return;
  }

  LOG_INFO("qtPilot Probe library loaded (Windows)");

  // Get port from environment or use default
  int port = 9999;
  QByteArray port_env = qgetenv("QTPILOT_PORT");
  if (!port_env.isEmpty()) {
    bool ok = false;
    int env_port = port_env.toInt(&ok);
    if (ok && env_port > 0 && env_port <= 65535) {
      port = env_port;
    }
  }

  // Check if QCoreApplication exists
  if (QCoreApplication::instance() != nullptr) {
    // Application exists, initialize via event loop
    Probe::instance()->setPort(static_cast<quint16>(port));
    QTimer::singleShot(0, []() { Probe::instance()->initialize(); });
  } else {
    LOG_INFO("QCoreApplication not yet created, will initialize when available");
    // Will be initialized lazily
  }
}

void ShutdownProbe() {
  LOG_INFO("qtPilot Probe library unloading (Windows)");
  if (Probe::instance()->isRunning()) {
    Probe::instance()->shutdown();
  }
}

}  // namespace qtPilot

// DLL entry point
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
  switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
      // Disable thread notifications for performance
      DisableThreadLibraryCalls(hinstDLL);
      qtPilot::InitializeProbe();
      break;

    case DLL_PROCESS_DETACH:
      // Only cleanup if process is not terminating
      if (lpvReserved == nullptr) {
        qtPilot::ShutdownProbe();
      }
      break;

    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
      // Not used
      break;
  }
  return TRUE;
}

#endif  // QTPILOT_PLATFORM_WINDOWS
