// Copyright (c) 2024 QtMCP Contributors
// SPDX-License-Identifier: MIT

#include "qt_env_setup.h"

#include <cstdio>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibraryInfo>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace {

/// @brief Check if a directory has bin/Qt*Core*.dll and plugins/platforms/.
bool hasQtPrefixLayout(const QDir& d) {
  QDir binDir(d.filePath(QStringLiteral("bin")));
  if (!binDir.exists())
    return false;

  QStringList coreMatches =
      binDir.entryList({QStringLiteral("Qt*Core.dll"), QStringLiteral("Qt*Core*.dll")}, QDir::Files);
  if (coreMatches.isEmpty())
    return false;

  return QFileInfo::exists(d.filePath(QStringLiteral("plugins/platforms")));
}

/// @brief Check if a directory contains Qt*Core*.dll directly (i.e. is bin/).
bool dirContainsQtCore(const QDir& d) {
  return !d.entryList({QStringLiteral("Qt*Core.dll"), QStringLiteral("Qt*Core*.dll")}, QDir::Files)
              .isEmpty();
}

/// @brief Resolve a user-provided path to a Qt prefix directory.
///
/// Accepts any of these and navigates to the prefix:
///   <prefix>                        -> as-is
///   <prefix>/bin                    -> parent
///   <prefix>/lib                    -> parent
///   <prefix>/plugins                -> parent
///   <prefix>/plugins/platforms      -> grandparent
///
/// @return Absolute path to the Qt prefix, or empty if unresolvable.
QString resolveQtPrefix(const QString& dir) {
  QDir d(dir);
  if (!d.exists())
    return QString();

  // Already a valid prefix?
  if (hasQtPrefixLayout(d))
    return d.absolutePath();

  // User pointed at bin/ (contains Qt*Core*.dll)?
  if (dirContainsQtCore(d)) {
    QDir parent(d);
    if (parent.cdUp() && hasQtPrefixLayout(parent))
      return parent.absolutePath();
  }

  // User pointed at plugins/ (has platforms/ subdir)?
  if (d.exists(QStringLiteral("platforms"))) {
    QDir parent(d);
    if (parent.cdUp() && hasQtPrefixLayout(parent))
      return parent.absolutePath();
  }

  // User pointed at plugins/platforms/ (has qwindows.dll)?
  if (QFileInfo::exists(d.filePath(QStringLiteral("qwindows.dll")))
      || QFileInfo::exists(d.filePath(QStringLiteral("qwindowsd.dll")))) {
    QDir grandparent(d);
    if (grandparent.cdUp() && grandparent.cdUp() && hasQtPrefixLayout(grandparent))
      return grandparent.absolutePath();
  }

  // User pointed at lib/?
  if (d.dirName().compare(QStringLiteral("lib"), Qt::CaseInsensitive) == 0) {
    QDir parent(d);
    if (parent.cdUp() && hasQtPrefixLayout(parent))
      return parent.absolutePath();
  }

  return QString();
}

/// @brief Check if QT_PLUGIN_PATH already points to a valid plugins dir.
bool existingEnvIsAdequate() {
  QByteArray pluginPath = qgetenv("QT_PLUGIN_PATH");
  if (pluginPath.isEmpty())
    return false;

  // Verify the plugin path contains a platforms/ subdir
  QDir pluginsDir(QString::fromLocal8Bit(pluginPath));
  return pluginsDir.exists(QStringLiteral("platforms"));
}

/// @brief Try to infer a Qt prefix from QLibraryInfo.
/// Works because the launcher itself is built against a Qt installation.
QString qtPrefixFromLibraryInfo() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  QString pluginsPath = QLibraryInfo::path(QLibraryInfo::PluginsPath);
#else
  QString pluginsPath = QLibraryInfo::location(QLibraryInfo::PluginsPath);
#endif

  if (pluginsPath.isEmpty())
    return QString();

  QDir pluginsDir(pluginsPath);
  if (!pluginsDir.exists() || !pluginsDir.exists(QStringLiteral("platforms")))
    return QString();

  // Qt prefix is typically one level up from plugins/
  QDir prefixDir(pluginsPath);
  if (!prefixDir.cdUp())
    return QString();

  QString prefix = prefixDir.absolutePath();

  // Verify bin/ exists too
  if (!QFileInfo::exists(prefix + QStringLiteral("/bin")))
    return QString();

  return prefix;
}

/// @brief Scan a directory for platforms/qwindows.dll (windeployqt layout).
/// @return The directory path if a platforms/ subdir is found, else empty.
QString qtPrefixFromPlatformsDir(const QString& directory) {
  QDir dir(directory);
  if (!dir.exists())
    return QString();

  if (QFileInfo::exists(dir.filePath(QStringLiteral("platforms/qwindows.dll")))
      || QFileInfo::exists(dir.filePath(QStringLiteral("platforms/qwindowsd.dll")))) {
    return dir.absolutePath();
  }

  return QString();
}

/// @brief Apply Qt environment variables to the current process.
void applyEnvironment(const QString& binPath, const QString& pluginsPath) {
#ifdef Q_OS_WIN
  // Prepend Qt bin dir to PATH
  QString currentPath = QString::fromLocal8Bit(qgetenv("PATH"));
  QString nativeBinPath = QDir::toNativeSeparators(binPath);

  if (!currentPath.contains(nativeBinPath, Qt::CaseInsensitive)) {
    QString newPath = nativeBinPath + QStringLiteral(";") + currentPath;
    SetEnvironmentVariableW(L"PATH", newPath.toStdWString().c_str());
    qputenv("PATH", newPath.toLocal8Bit());
  }

  // Set QT_PLUGIN_PATH if not already set
  QByteArray existingPluginPath = qgetenv("QT_PLUGIN_PATH");
  if (existingPluginPath.isEmpty()) {
    QString nativePluginsPath = QDir::toNativeSeparators(pluginsPath);
    SetEnvironmentVariableW(L"QT_PLUGIN_PATH", nativePluginsPath.toStdWString().c_str());
    qputenv("QT_PLUGIN_PATH", nativePluginsPath.toLocal8Bit());
  }
#else
  // Linux: LD_LIBRARY_PATH and QT_PLUGIN_PATH
  QString currentLdPath = QString::fromLocal8Bit(qgetenv("LD_LIBRARY_PATH"));
  if (!currentLdPath.contains(binPath)) {
    QString newLdPath = binPath + QStringLiteral(":") + currentLdPath;
    qputenv("LD_LIBRARY_PATH", newLdPath.toLocal8Bit());
  }

  QByteArray existingPluginPath = qgetenv("QT_PLUGIN_PATH");
  if (existingPluginPath.isEmpty()) {
    qputenv("QT_PLUGIN_PATH", pluginsPath.toLocal8Bit());
  }
#endif
}

}  // namespace

namespace qtmcp {

QtEnvironmentResult ensureQtEnvironment(const QString& qtDir,
                                        const QString& targetExe,
                                        bool quiet) {
  QtEnvironmentResult result;

  // --- Step 1: Explicit --qt-dir flag ---
  if (!qtDir.isEmpty()) {
    QString resolved = resolveQtPrefix(qtDir);
    if (!resolved.isEmpty()) {
      result.qtDir = resolved;
      result.binPath = QDir(resolved).filePath(QStringLiteral("bin"));
      result.pluginsPath = QDir(resolved).filePath(QStringLiteral("plugins"));
      result.source = (resolved == QDir(qtDir).absolutePath())
                          ? QStringLiteral("--qt-dir flag")
                          : QStringLiteral("--qt-dir flag (resolved from %1)").arg(qtDir);
    } else {
      result.warnings.append(
          QStringLiteral("--qt-dir '%1' does not look like a Qt installation "
                         "(could not find bin/Qt*Core*.dll and plugins/platforms/)")
              .arg(qtDir));
      // Fall through to other strategies
    }
  }

  // --- Step 2: Existing environment variables ---
  if (result.qtDir.isEmpty() && existingEnvIsAdequate()) {
    result.source = QStringLiteral("existing environment variables");
    result.applied = false;
    if (!quiet) {
      fprintf(stderr, "[qtmcp-launch] Qt environment already configured via existing QT_PLUGIN_PATH\n");
    }
    return result;
  }

  // --- Step 3: QLibraryInfo (launcher's own Qt) ---
  if (result.qtDir.isEmpty()) {
    QString prefix = qtPrefixFromLibraryInfo();
    if (!prefix.isEmpty()) {
      result.qtDir = prefix;
      result.binPath = prefix + QStringLiteral("/bin");
      result.pluginsPath = prefix + QStringLiteral("/plugins");
      result.source = QStringLiteral("QLibraryInfo (launcher's Qt)");
    }
  }

  // --- Step 3b: Build-time Qt prefix (compiled into the launcher by CMake) ---
#ifdef QTMCP_BUILD_QT_PREFIX
  if (result.qtDir.isEmpty()) {
    QString resolved = resolveQtPrefix(QStringLiteral(QTMCP_BUILD_QT_PREFIX));
    if (!resolved.isEmpty()) {
      result.qtDir = resolved;
      result.binPath = QDir(resolved).filePath(QStringLiteral("bin"));
      result.pluginsPath = QDir(resolved).filePath(QStringLiteral("plugins"));
      result.source = QStringLiteral("build-time Qt prefix");
    }
  }
#endif

  // --- Step 4: Scan target app's directory ---
  if (result.qtDir.isEmpty()) {
    QString targetDir = QFileInfo(targetExe).absolutePath();
    QString prefix = qtPrefixFromPlatformsDir(targetDir);
    if (!prefix.isEmpty()) {
      result.qtDir = prefix;
      result.binPath = prefix;
      result.pluginsPath = prefix;
      result.source = QStringLiteral("target app directory (windeployqt layout)");
    }
  }

  // --- Step 5: Scan launcher's own directory ---
  if (result.qtDir.isEmpty()) {
    QString launcherDir = QCoreApplication::applicationDirPath();
    QString prefix = qtPrefixFromPlatformsDir(launcherDir);
    if (!prefix.isEmpty()) {
      result.qtDir = prefix;
      result.binPath = prefix;
      result.pluginsPath = prefix;
      result.source = QStringLiteral("launcher directory");
    }
  }

  // --- Apply environment if we found something ---
  if (!result.qtDir.isEmpty()) {
    applyEnvironment(result.binPath, result.pluginsPath);
    result.applied = true;

    if (!quiet) {
      fprintf(stderr, "[qtmcp-launch] Qt detected via %s\n", qPrintable(result.source));
      fprintf(stderr, "[qtmcp-launch]   Qt dir:      %s\n",
              qPrintable(QDir::toNativeSeparators(result.qtDir)));
      fprintf(stderr, "[qtmcp-launch]   PATH entry:  %s\n",
              qPrintable(QDir::toNativeSeparators(result.binPath)));
      fprintf(stderr, "[qtmcp-launch]   Plugin path: %s\n",
              qPrintable(QDir::toNativeSeparators(result.pluginsPath)));
    }
  } else {
    if (!quiet) {
      fprintf(stderr,
              "[qtmcp-launch] WARNING: Could not auto-detect Qt installation.\n"
              "[qtmcp-launch]   Probe injection may fail if Qt DLLs are not on PATH.\n"
              "[qtmcp-launch]   Use --qt-dir to specify your Qt installation path.\n");
    }
    result.warnings.append(QStringLiteral("Could not auto-detect Qt installation"));
  }

  for (const QString& warning : result.warnings) {
    if (!quiet) {
      fprintf(stderr, "[qtmcp-launch] WARNING: %s\n", qPrintable(warning));
    }
  }

  return result;
}

}  // namespace qtmcp
