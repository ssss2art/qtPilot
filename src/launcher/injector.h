// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <QString>
#include <QStringList>

namespace qtPilot {

/// @brief Options for launching a target application with the probe.
struct LaunchOptions {
  QString targetExecutable;     ///< Path to the target executable
  QStringList targetArgs;       ///< Command line arguments for the target
  QString probePath;            ///< Path to the probe library (DLL/SO)
  QString qtDir;                ///< Explicit Qt prefix (from --qt-dir), or empty for auto-detect
  quint16 port = 9222;          ///< WebSocket port for the probe server
  bool detach = false;          ///< If true, run in background (don't wait)
  bool quiet = false;           ///< If true, suppress startup messages
  bool injectChildren = false;  ///< If true, inject probe into child processes
  bool runAsAdmin = false;      ///< If true, elevate to admin before launching (Windows only)
};

/// @brief Launch a target application with the qtPilot probe injected.
///
/// On Windows, this uses CreateRemoteThread to inject the probe DLL into
/// the target process after creating it suspended.
///
/// On Linux, this sets LD_PRELOAD and execs the target.
///
/// @param options Launch configuration including target path, probe path, and flags.
/// @return Process ID on success, -1 on failure.
qint64 launchWithProbe(const LaunchOptions& options);

}  // namespace qtPilot
