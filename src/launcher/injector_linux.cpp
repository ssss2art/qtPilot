// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

// Linux implementation of probe injection using LD_PRELOAD.
// This file is only compiled on Linux (see CMakeLists.txt).
//
// LD_PRELOAD causes the dynamic linker to load our probe library before
// the application's own libraries. The probe's __attribute__((constructor))
// function runs early and sets up the WebSocket server.

#include "injector.h"

#ifndef Q_OS_WIN

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

  // 2. Use QProcess for launching
  // This handles all the complexity of process management
  if (options.detach) {
    // Detached mode: use QProcess::startDetached
    qint64 pid = 0;
    bool success = QProcess::startDetached(options.targetExecutable, options.targetArgs,
                                           QString(),  // Working directory (current)
                                           &pid);

    if (!success) {
      if (!options.quiet) {
        fprintf(stderr, "[injector] Failed to start detached process\n");
      }
      return -1;
    }

    // Note: QProcess::startDetached doesn't apply environment to Qt 5 < 5.15
    // For better compatibility, we use fork/exec below

    // Actually, let's use fork/exec for proper environment handling
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

    int status = 0;
    pid_t result = waitpid(pid, &status, 0);

    if (result < 0) {
      if (!options.quiet) {
        perror("[injector] waitpid failed");
      }
    } else {
      if (WIFEXITED(status)) {
        if (!options.quiet) {
          fprintf(stderr, "[injector] Process exited with code %d\n", WEXITSTATUS(status));
        }
      } else if (WIFSIGNALED(status)) {
        if (!options.quiet) {
          fprintf(stderr, "[injector] Process killed by signal %d\n", WTERMSIG(status));
        }
      }
    }
  }

  return static_cast<qint64>(pid);
}

}  // namespace qtPilot

#endif  // !Q_OS_WIN
