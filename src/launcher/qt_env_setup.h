// Copyright (c) 2024 QtMCP Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <QString>
#include <QStringList>

namespace qtmcp {

/// @brief Describes the Qt environment that was detected and applied.
struct QtEnvironmentResult {
  QString qtDir;         ///< Qt prefix directory (e.g. "C:/Qt/6.8.0/msvc2022_64")
  QString pluginsPath;   ///< Plugins directory (e.g. "C:/Qt/6.8.0/msvc2022_64/plugins")
  QString binPath;       ///< Bin directory with Qt DLLs
  QString source;        ///< Human-readable label for how it was found
  bool applied = false;  ///< True if env vars were set/modified
  QStringList warnings;  ///< Non-fatal diagnostic messages
};

/// @brief Auto-detect and configure Qt environment variables for probe injection.
///
/// Uses a resolution cascade to find the Qt installation. Sets PATH and
/// QT_PLUGIN_PATH in the current process environment so that CreateProcessW
/// (which inherits the parent environment) will propagate them to the target.
///
/// Resolution cascade (first valid path wins):
///   1. Explicit qtDir from --qt-dir CLI flag
///   2. Existing QT_PLUGIN_PATH / PATH env vars (user overrides)
///   3. QLibraryInfo::path(PluginsPath) from the launcher's own Qt linkage
///   4. Target app's directory -- scan for platforms/qwindows.dll
///   5. Launcher's own directory -- scan for co-located Qt DLLs
///
/// @param qtDir         Explicit Qt prefix from --qt-dir flag, or empty.
/// @param targetExe     Absolute path to the target executable.
/// @param quiet         Suppress informational log messages.
/// @return Result struct describing what was found and applied.
QtEnvironmentResult ensureQtEnvironment(const QString& qtDir,
                                        const QString& targetExe,
                                        bool quiet);

}  // namespace qtmcp
