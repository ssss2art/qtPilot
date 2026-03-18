// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

// macOS implementation of probe injection using DYLD_INSERT_LIBRARIES.
// This file is only compiled on macOS (see CMakeLists.txt).
//
// DYLD_INSERT_LIBRARIES causes the dynamic linker to load our probe library
// before the application's own libraries. The probe's __attribute__((constructor))
// function runs early and sets up the WebSocket server.
//
// Note: System Integrity Protection (SIP) strips DYLD_INSERT_LIBRARIES for
// binaries in /usr/, /System/, and other protected paths. Only user-built
// or non-protected executables can be injected this way.

#include "injector.h"

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)

#include <cstdio>
#include <cstdlib>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>

namespace {

/// @brief Warn if the target executable is in a SIP-protected path.
/// DYLD_INSERT_LIBRARIES is stripped for such binaries.
void warnIfSipProtected(const QString& targetPath, bool quiet) {
  if (quiet)
    return;

  static const char* sipPaths[] = {"/usr/", "/System/", "/bin/", "/sbin/", nullptr};
  QByteArray pathBytes = targetPath.toUtf8();
  const char* path = pathBytes.constData();

  for (const char** p = sipPaths; *p != nullptr; ++p) {
    if (strncmp(path, *p, strlen(*p)) == 0) {
      fprintf(stderr,
              "[injector] WARNING: Target '%s' is in a SIP-protected path.\n"
              "[injector]   DYLD_INSERT_LIBRARIES will be stripped by macOS.\n"
              "[injector]   Probe injection will NOT work for this binary.\n"
              "[injector]   Use a non-SIP-protected executable instead.\n",
              qPrintable(targetPath));
      return;
    }
  }
}

}  // namespace

namespace qtPilot {

qint64 launchWithProbe(const LaunchOptions& options) {
  // Warn about SIP restrictions
  warnIfSipProtected(options.targetExecutable, options.quiet);

  // 1. Set up environment for the child process
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

  // Set QTPILOT_PORT for the probe
  env.insert(QStringLiteral("QTPILOT_PORT"), QString::number(options.port));

  // Get absolute path to probe library
  QString absProbe = QFileInfo(options.probePath).absoluteFilePath();

  // Prepend to DYLD_INSERT_LIBRARIES (preserve existing entries)
  QString existingInsert = env.value(QStringLiteral("DYLD_INSERT_LIBRARIES"));
  if (existingInsert.isEmpty()) {
    env.insert(QStringLiteral("DYLD_INSERT_LIBRARIES"), absProbe);
  } else {
    // Prepend our library, colon-separated
    env.insert(QStringLiteral("DYLD_INSERT_LIBRARIES"),
               absProbe + QLatin1Char(':') + existingInsert);
  }

  if (!options.quiet) {
    fprintf(stderr, "[injector] DYLD_INSERT_LIBRARIES: %s\n",
            qPrintable(env.value(QStringLiteral("DYLD_INSERT_LIBRARIES"))));
    fprintf(stderr, "[injector] QTPILOT_PORT: %s\n",
            qPrintable(env.value(QStringLiteral("QTPILOT_PORT"))));
  }

  // 2. Fork the process
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

    // Set DYLD_INSERT_LIBRARIES
    const char* existingInsert = getenv("DYLD_INSERT_LIBRARIES");
    if (existingInsert && existingInsert[0] != '\0') {
      QString newInsert = absProbe + QLatin1Char(':') + QString::fromLocal8Bit(existingInsert);
      setenv("DYLD_INSERT_LIBRARIES", qPrintable(newInsert), 1);
    } else {
      setenv("DYLD_INSERT_LIBRARIES", qPrintable(absProbe), 1);
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

#endif  // Q_OS_MACOS || Q_OS_MAC
