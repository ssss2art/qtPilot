// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

// qtPilot Launcher
// Launches a Qt application with the qtPilot probe library injected.
// Works on both Windows (DLL injection) and Linux (LD_PRELOAD).

#include "injector.h"
#include "qt_env_setup.h"

#include <cstdio>
#include <cstdlib>

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#ifdef Q_OS_WIN
#include "elevation_windows.h"
#endif

namespace {

/// @brief Find the probe library adjacent to the launcher executable.
/// @param qtVersion If non-empty, filter matches to those containing
///   this version tag (e.g. "5.15").
/// @return Absolute path to the probe library, or empty string if not found.
QString findProbePath(const QString& qtVersion) {
  QDir exeDir(QCoreApplication::applicationDirPath());

#ifdef Q_OS_WIN
  const QStringList globPatterns = {QStringLiteral("qtPilot-probe*.dll")};
#else
  // Match both CMake build output (libqtPilot-probe*.so) and
  // archive-extracted probes (qtPilot-probe*.so)
  const QStringList globPatterns = {QStringLiteral("libqtPilot-probe*.so"),
                                    QStringLiteral("qtPilot-probe*.so")};
#endif

  QStringList searchDirs = {
      exeDir.absolutePath(),
      exeDir.absoluteFilePath(QStringLiteral("../lib")),
      exeDir.absoluteFilePath(QStringLiteral("lib")),
  };

  // Glob for probe libraries across all search dirs
  QStringList allMatches;
  for (const QString& dir : searchDirs) {
    QDir d(dir);
    if (!d.exists())
      continue;
    for (const QString& globPattern : globPatterns) {
      for (const QString& entry : d.entryList({globPattern}, QDir::Files, QDir::Name)) {
        QString fullPath = QFileInfo(d.filePath(entry)).absoluteFilePath();
        if (!allMatches.contains(fullPath))
          allMatches.append(fullPath);
      }
    }
  }

  // Filter by Qt version if specified
  if (!qtVersion.isEmpty()) {
    QString versionTag = QStringLiteral("qt") + qtVersion;
    QStringList filtered;
    for (const QString& path : allMatches) {
      if (QFileInfo(path).fileName().contains(versionTag))
        filtered.append(path);
    }
    allMatches = filtered;
  }

  if (allMatches.size() == 1)
    return allMatches.first();

  if (allMatches.size() > 1) {
    fprintf(stderr, "Error: Multiple probe libraries found:\n");
    for (const QString& p : allMatches)
      fprintf(stderr, "  %s\n", qPrintable(p));
    fprintf(stderr, "Use --qt-version to select one, or --probe for an exact path\n");
    return QString();
  }

  return QString();
}

}  // namespace

int main(int argc, char* argv[]) {
  QCoreApplication app(argc, argv);
  app.setApplicationName(QStringLiteral("qtpilot-launch"));
  app.setApplicationVersion(QStringLiteral("0.1.0"));

  // Set up command line parser
  QCommandLineParser parser;
  parser.setApplicationDescription(
      QStringLiteral("Launch Qt applications with qtPilot probe injected"));
  parser.addHelpOption();
  parser.addVersionOption();

  // Define options per CONTEXT.md decisions
  QCommandLineOption portOption(QStringList() << QStringLiteral("p") << QStringLiteral("port"),
                                QStringLiteral("WebSocket port for probe (default: 9222)"),
                                QStringLiteral("port"), QStringLiteral("9222"));
  parser.addOption(portOption);

  QCommandLineOption detachOption(QStringList() << QStringLiteral("d") << QStringLiteral("detach"),
                                  QStringLiteral("Run in background, don't wait for target"));
  parser.addOption(detachOption);

  QCommandLineOption quietOption(QStringList() << QStringLiteral("q") << QStringLiteral("quiet"),
                                 QStringLiteral("Suppress startup messages"));
  parser.addOption(quietOption);

  QCommandLineOption probeOption(
      QStringLiteral("probe"),
      QStringLiteral("Path to probe library (auto-detected if not specified)"),
      QStringLiteral("path"));
  parser.addOption(probeOption);

  QCommandLineOption qtVersionOption(
      QStringLiteral("qt-version"),
      QStringLiteral("Qt version to select probe for (e.g., 5.15, 6.8)"),
      QStringLiteral("version"));
  parser.addOption(qtVersionOption);

  QCommandLineOption injectChildrenOption(
      QStringLiteral("inject-children"),
      QStringLiteral("Automatically inject probe into child processes"));
  parser.addOption(injectChildrenOption);

  QCommandLineOption qtDirOption(
      QStringLiteral("qt-dir"),
      QStringLiteral("Path to Qt installation prefix (e.g., C:\\Qt\\6.8.0\\msvc2022_64). "
                     "Auto-detected if not specified."),
      QStringLiteral("path"));
  parser.addOption(qtDirOption);

  QCommandLineOption runAsAdminOption(
      QStringLiteral("run-as-admin"),
      QStringLiteral("Launch target with administrator privileges (Windows only)"));
  parser.addOption(runAsAdminOption);

  // Internal flag: signals this instance was re-launched elevated
  QCommandLineOption elevatedOption(QStringLiteral("elevated"),
                                    QStringLiteral("Internal: already elevated"));
  elevatedOption.setFlags(QCommandLineOption::HiddenFromHelp);
  parser.addOption(elevatedOption);

  // Positional arguments: target executable and its arguments
  parser.addPositionalArgument(QStringLiteral("target"),
                               QStringLiteral("Target executable to launch"));
  parser.addPositionalArgument(QStringLiteral("args"),
                               QStringLiteral("Arguments to pass to target"),
                               QStringLiteral("[args...]"));

  // Parse arguments
  parser.process(app);

  // Handle --run-as-admin: self-elevate if not already elevated
#ifdef Q_OS_WIN
  if (parser.isSet(runAsAdminOption) && !parser.isSet(elevatedOption)) {
    if (!qtPilot::isProcessElevated()) {
      // Re-launch self as admin, forwarding all arguments
      return qtPilot::relaunchElevated(QCoreApplication::applicationFilePath(),
                                     QCoreApplication::arguments().mid(1));
    }
    // Already elevated — fall through to normal launch
  }
#else
  if (parser.isSet(runAsAdminOption)) {
    fprintf(stderr,
            "Warning: --run-as-admin is only supported on Windows. "
            "Use sudo on Linux.\n");
  }
#endif

  // Get positional arguments
  QStringList positionalArgs = parser.positionalArguments();
  if (positionalArgs.isEmpty()) {
    fprintf(stderr, "Error: No target executable specified\n\n");
    parser.showHelp(1);  // Exits with code 1
  }

  // Build LaunchOptions
  qtPilot::LaunchOptions options;
  options.targetExecutable = positionalArgs.takeFirst();
  options.targetArgs = positionalArgs;  // Remaining args go to target
  options.detach = parser.isSet(detachOption);
  options.quiet = parser.isSet(quietOption);
  options.injectChildren = parser.isSet(injectChildrenOption);
  options.runAsAdmin = parser.isSet(runAsAdminOption) || parser.isSet(elevatedOption);
  options.qtDir = parser.value(qtDirOption);

  // Parse and validate port
  bool portOk = false;
  int portValue = parser.value(portOption).toInt(&portOk);
  if (!portOk || portValue < 0 || portValue > 65535) {
    fprintf(stderr, "Error: Invalid port value '%s' (must be 0-65535)\n",
            qPrintable(parser.value(portOption)));
    return 1;
  }
  options.port = static_cast<quint16>(portValue);

  // Resolve target executable path
  QFileInfo targetInfo(options.targetExecutable);
  if (!targetInfo.exists()) {
    // Try to find in PATH
    QString pathEnv = QString::fromLocal8Bit(qgetenv("PATH"));
#ifdef Q_OS_WIN
    QStringList pathDirs = pathEnv.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    QStringList extensions = {QStringLiteral(".exe"), QStringLiteral(".com"), QStringLiteral("")};
#else
    QStringList pathDirs = pathEnv.split(QLatin1Char(':'), Qt::SkipEmptyParts);
    QStringList extensions = {QStringLiteral("")};
#endif
    bool found = false;
    for (const QString& dir : pathDirs) {
      for (const QString& ext : extensions) {
        QString candidate = QDir(dir).filePath(options.targetExecutable + ext);
        if (QFileInfo::exists(candidate)) {
          options.targetExecutable = QFileInfo(candidate).absoluteFilePath();
          found = true;
          break;
        }
      }
      if (found)
        break;
    }
    if (!found) {
      fprintf(stderr, "Error: Target executable not found: %s\n",
              qPrintable(options.targetExecutable));
      return 1;
    }
  } else {
    options.targetExecutable = targetInfo.absoluteFilePath();
  }

  // Resolve probe path
  if (parser.isSet(probeOption)) {
    options.probePath = parser.value(probeOption);
    if (!QFileInfo::exists(options.probePath)) {
      fprintf(stderr, "Error: Probe library not found: %s\n", qPrintable(options.probePath));
      return 1;
    }
    options.probePath = QFileInfo(options.probePath).absoluteFilePath();
  } else {
    options.probePath = findProbePath(parser.value(qtVersionOption));
    if (options.probePath.isEmpty()) {
      fprintf(stderr, "Error: Could not find qtPilot probe library\n");
      fprintf(stderr, "Use --probe option to specify the path\n");
      return 1;
    }
  }

  // Auto-detect and configure Qt environment for probe injection
  qtPilot::QtEnvironmentResult envResult =
      qtPilot::ensureQtEnvironment(options.qtDir, options.targetExecutable, options.quiet);

  // Print startup message unless quiet
  if (!options.quiet) {
    fprintf(stderr, "[qtpilot-launch] Target: %s\n", qPrintable(options.targetExecutable));
    fprintf(stderr, "[qtpilot-launch] Probe: %s\n", qPrintable(options.probePath));
    fprintf(stderr, "[qtpilot-launch] Port: %u, Detach: %s, Inject children: %s, Admin: %s\n",
            static_cast<unsigned>(options.port), options.detach ? "yes" : "no",
            options.injectChildren ? "yes" : "no", options.runAsAdmin ? "yes" : "no");
    if (envResult.applied) {
      fprintf(stderr, "[qtpilot-launch] Qt env: %s (via %s)\n",
              qPrintable(QDir::toNativeSeparators(envResult.qtDir)), qPrintable(envResult.source));
    }
  }

  // Launch with probe injection
  qint64 pid = qtPilot::launchWithProbe(options);
  if (pid < 0) {
    fprintf(stderr, "Error: Failed to launch target with probe\n");
    return 1;
  }

  // Success
  if (!options.quiet) {
    fprintf(stderr, "[qtpilot-launch] Started process with PID %lld\n", static_cast<long long>(pid));
  }

  return 0;
}
