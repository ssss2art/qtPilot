// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "core/injector.h"

#ifdef QTPILOT_PLATFORM_LINUX

#include "core/probe.h"

#include <cstdlib>
#include <spdlog/spdlog.h>

#include <QCoreApplication>
#include <QTimer>

namespace qtPilot {

void InitializeProbe() {
  // Check if probe is disabled
  const char* enabled_env = std::getenv("QTPILOT_ENABLED");
  if (enabled_env != nullptr && std::string(enabled_env) == "0") {
    spdlog::info("qtPilot Probe disabled via QTPILOT_ENABLED=0");
    return;
  }

  spdlog::info("qtPilot Probe library loaded (Linux)");

  // Get port from environment or use default
  int port = 9999;
  const char* port_env = std::getenv("QTPILOT_PORT");
  if (port_env != nullptr) {
    port = std::atoi(port_env);
    if (port <= 0 || port > 65535) {
      port = 9999;
    }
  }

  // We need to wait for QCoreApplication to be created
  // Use a polling approach since we can't use Qt signals before app exists
  auto check_and_init = []() {
    if (QCoreApplication::instance() != nullptr) {
      // Application exists, initialize the probe
      // Use QTimer to ensure we're in the event loop
      QTimer::singleShot(0, []() {
        int port = 9999;
        const char* port_env = std::getenv("QTPILOT_PORT");
        if (port_env != nullptr) {
          port = std::atoi(port_env);
          if (port <= 0 || port > 65535) {
            port = 9999;
          }
        }

        Probe::Instance()->Initialize(port);
      });
      return true;
    }
    return false;
  };

  // Try immediate initialization if app already exists
  if (!check_and_init()) {
    spdlog::info("QCoreApplication not yet created, will initialize when available");
    // The probe will be initialized lazily when someone calls Probe::Instance()
    // and the application exists, or we could set up a more sophisticated
    // polling mechanism here if needed.

    // For now, we rely on the application calling Probe::Instance()->Initialize()
    // or the first JSON-RPC connection triggering initialization.
  }
}

void ShutdownProbe() {
  spdlog::info("qtPilot Probe library unloading (Linux)");
  if (Probe::Instance()->IsRunning()) {
    Probe::Instance()->Shutdown();
  }
}

}  // namespace qtPilot

// Library constructor - called when library is loaded via LD_PRELOAD
__attribute__((constructor)) static void qtPilotLibraryInit() {
  qtPilot::InitializeProbe();
}

// Library destructor - called when library is unloaded
__attribute__((destructor)) static void qtPilotLibraryFini() {
  qtPilot::ShutdownProbe();
}

#endif  // QTPILOT_PLATFORM_LINUX
