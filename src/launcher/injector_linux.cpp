// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

// Linux implementation of probe injection using LD_PRELOAD.
// This file is only compiled on Linux (see CMakeLists.txt).
//
// LD_PRELOAD causes the dynamic linker to load our probe library before
// the application's own libraries. The probe's __attribute__((constructor))
// function runs early and sets up the WebSocket server.

#include "injector.h"

#if defined(Q_OS_LINUX)

#include "child_wait_posix.h"

#include <cstdio>
#include <cstdlib>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>

namespace qtPilot {

qint64 launchWithProbe(const LaunchOptions& options) {
  // 1. Set up environment for the child process
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

  // Set QTPILOT_PORT for the probe
  env.insert(QStringLiteral("QTPILOT_PORT"), QString::number(options.port));

  // Get absolute path to probe library
  QString absProbe = QFileInfo(options.probePath).absoluteFilePath();

  // Prepend to LD_PRELOAD (preserve existing preloads)
  QString existingPreload = env.value(QStringLiteral("LD_PRELOAD"));
  if (existingPreload.isEmpty()) {
    env.insert(QStringLiteral("LD_PRELOAD"), absProbe);
  } else {
    // Prepend our library, space-separated
    env.insert(QStringLiteral("LD_PRELOAD"), absProbe + QLatin1Char(' ') + existingPreload);
  }

  if (!options.quiet) {
    fprintf(stderr, "[injector] LD_PRELOAD: %s\n",
            qPrintable(env.value(QStringLiteral("LD_PRELOAD"))));
    fprintf(stderr, "[injector] QTPILOT_PORT: %s\n",
            qPrintable(env.value(QStringLiteral("QTPILOT_PORT"))));
  }

  // Fork the process
  pid_t pid = fork();

  if (pid < 0) {
    // Fork failed
    if (!options.quiet) {
      perror("[injector] fork failed");
    }
    return -1;
  }

  if (pid == 0) {
    // Child process

    // Set environment variables
    setenv("QTPILOT_PORT", qPrintable(QString::number(options.port)), 1);

    // Enable child process injection if requested
    if (options.injectChildren) {
      setenv("QTPILOT_INJECT_CHILDREN", "1", 1);
    }

    // Set LD_PRELOAD
    const char* existingPreload = getenv("LD_PRELOAD");
    if (existingPreload && existingPreload[0] != '\0') {
      QString newPreload = absProbe + QLatin1Char(' ') + QString::fromLocal8Bit(existingPreload);
      setenv("LD_PRELOAD", qPrintable(newPreload), 1);
    } else {
      setenv("LD_PRELOAD", qPrintable(absProbe), 1);
    }

    // Build argv for execvp
    QByteArray targetBytes = options.targetExecutable.toLocal8Bit();
    QList<QByteArray> argBytes;
    for (const QString& arg : options.targetArgs) {
      argBytes.append(arg.toLocal8Bit());
    }

    // Create argv array
    std::vector<char*> argv;
    argv.push_back(targetBytes.data());
    for (QByteArray& arg : argBytes) {
      argv.push_back(arg.data());
    }
    argv.push_back(nullptr);

    // Execute the target
    execvp(targetBytes.constData(), argv.data());

    // If we get here, exec failed
    perror("[injector] execvp failed");
    _exit(127);
  }

  // Parent process
  if (!options.quiet) {
    fprintf(stderr, "[injector] Started child process with PID %d\n", static_cast<int>(pid));
  }

  if (!options.detach) {
    // Wait for the child process
    if (!options.quiet) {
      fprintf(stderr, "[injector] Waiting for process to exit...\n");
    }

    waitForChildForwardingSignals(pid, options.quiet);
  }

  return static_cast<qint64>(pid);
}

}  // namespace qtPilot

#endif  // Q_OS_LINUX
