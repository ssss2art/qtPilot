// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

// Windows implementation of probe injection using CreateRemoteThread.
// This file is only compiled on Windows (see CMakeLists.txt).
//
// Injection pattern based on Pattern 4 from 01-RESEARCH.md:
// 1. Create process suspended
// 2. Allocate memory in target for DLL path
// 3. Write DLL path to target memory
// 4. Get address of LoadLibraryW in kernel32
// 5. Create remote thread to call LoadLibraryW with DLL path
// 6. Wait for injection thread to complete
// 7. Resume main thread
// 8. Optionally wait for process to exit

#include "injector.h"

#ifdef Q_OS_WIN

#include "process_inject_windows.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cstdio>

namespace {

/// @brief Build a command line string for CreateProcessW.
/// @param executable The executable path.
/// @param args The command line arguments.
/// @return A properly quoted command line string.
std::wstring buildCommandLine(const QString& executable, const QStringList& args) {
  QString cmdLine;

  // Quote the executable path if it contains spaces
  if (executable.contains(QLatin1Char(' '))) {
    cmdLine = QStringLiteral("\"%1\"").arg(executable);
  } else {
    cmdLine = executable;
  }

  // Append arguments, quoting as needed
  for (const QString& arg : args) {
    cmdLine += QLatin1Char(' ');
    if (arg.contains(QLatin1Char(' ')) || arg.contains(QLatin1Char('"'))) {
      // Quote and escape embedded quotes
      QString escaped = arg;
      escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
      cmdLine += QStringLiteral("\"%1\"").arg(escaped);
    } else {
      cmdLine += arg;
    }
  }

  return cmdLine.toStdWString();
}

}  // namespace

namespace qtPilot {

qint64 launchWithProbe(const LaunchOptions& options) {
  // 1. Set up environment for the target process
  // Pass port to probe via QTPILOT_PORT environment variable

  // Get current environment and add QTPILOT_PORT
  QString portStr = QString::number(options.port);
  if (!SetEnvironmentVariableW(L"QTPILOT_PORT", portStr.toStdWString().c_str())) {
    if (!options.quiet) {
      printWindowsError("Failed to set QTPILOT_PORT", GetLastError());
    }
    // Continue anyway - probe has default port
  }

  // Enable child process injection if requested
  if (options.injectChildren) {
    SetEnvironmentVariableW(L"QTPILOT_INJECT_CHILDREN", L"1");
  }

  // 2. Build command line
  std::wstring cmdLine = buildCommandLine(options.targetExecutable, options.targetArgs);

  if (!options.quiet) {
    fprintf(stderr, "[injector] Command line: %ls\n", cmdLine.c_str());
  }

  // 3. Create target process in suspended state
  STARTUPINFOW si = {};
  si.cb = sizeof(si);

  PROCESS_INFORMATION pi = {};

  // Need a mutable copy of cmdLine for CreateProcessW
  std::vector<wchar_t> cmdLineBuf(cmdLine.begin(), cmdLine.end());
  cmdLineBuf.push_back(L'\0');

  BOOL createResult = CreateProcessW(nullptr,            // Application name (use command line)
                                     cmdLineBuf.data(),  // Command line (mutable)
                                     nullptr,            // Process security attributes
                                     nullptr,            // Thread security attributes
                                     FALSE,              // Inherit handles
                                     CREATE_SUSPENDED,   // Creation flags - start suspended
                                     nullptr,            // Environment (inherit current)
                                     nullptr,            // Current directory
                                     &si,                // Startup info
                                     &pi                 // Process information
  );

  if (!createResult) {
    if (!options.quiet) {
      printWindowsError("CreateProcessW failed", GetLastError());
    }
    return -1;
  }

  // RAII guards for handles
  qtPilot::HandleGuard processHandle(pi.hProcess);
  qtPilot::HandleGuard threadHandle(pi.hThread);
  qint64 processId = static_cast<qint64>(pi.dwProcessId);

  if (!options.quiet) {
    fprintf(stderr, "[injector] Created process %lld (suspended)\n",
            static_cast<long long>(processId));
  }

  // 4. Pre-flight: try loading probe DLL locally with full dependency resolution
  //    to catch missing Qt DLLs before remote injection.
  std::wstring dllPath = options.probePath.toStdWString();
  {
    // Suppress probe initialization in the launcher process
    SetEnvironmentVariableW(L"QTPILOT_ENABLED", L"0");

    HMODULE localCheck = LoadLibraryExW(dllPath.c_str(), nullptr, 0);
    DWORD loadError = localCheck ? 0 : GetLastError();

    if (localCheck) {
      FreeLibrary(localCheck);
    }

    // Clear QTPILOT_ENABLED so the target process doesn't inherit "0"
    SetEnvironmentVariableW(L"QTPILOT_ENABLED", nullptr);

    if (!localCheck) {
      if (!options.quiet) {
        fprintf(stderr, "\n[injector] ERROR: Probe DLL failed pre-flight dependency check.\n");
        fprintf(stderr, "[injector]   Probe: %ls\n", dllPath.c_str());
        printWindowsError("[injector]   Cause", loadError);
        fprintf(stderr, "\n");

        if (loadError == ERROR_MOD_NOT_FOUND) {
          fprintf(stderr, "[injector] This usually means Qt runtime DLLs are not on PATH.\n");
          fprintf(stderr, "[injector] Fix: specify your Qt installation:\n");
          fprintf(stderr,
                  "[injector]   qtPilot-launcher.exe --qt-dir C:\\Qt\\6.8.0\\msvc2022_64 "
                  "your-app.exe\n\n");
        } else if (loadError == ERROR_BAD_EXE_FORMAT) {
          fprintf(stderr,
                  "[injector] Architecture mismatch: the probe DLL does not match "
                  "this process's bitness.\n\n");
        }
      }
      TerminateProcess(processHandle.get(), 1);
      return -1;
    }
  }

  // 5. Inject probe DLL (LoadLibraryW + qtpilotProbeInit via shared utility)
  if (!qtPilot::injectProbeDll(processHandle.get(), pi.dwProcessId, dllPath.c_str(),
                               options.quiet)) {
    TerminateProcess(processHandle.get(), 1);
    return -1;
  }

  // 6. Resume the main thread
  DWORD suspendCount = ResumeThread(threadHandle.get());
  if (suspendCount == static_cast<DWORD>(-1)) {
    if (!options.quiet) {
      printWindowsError("ResumeThread failed", GetLastError());
    }
    TerminateProcess(processHandle.get(), 1);
    return -1;
  }

  if (!options.quiet) {
    fprintf(stderr, "[injector] Resumed main thread (suspend count was %lu)\n", suspendCount);
  }

  // 7. Wait for process if not detaching
  if (!options.detach) {
    if (!options.quiet) {
      fprintf(stderr, "[injector] Waiting for process to exit...\n");
    }
    WaitForSingleObject(processHandle.get(), INFINITE);

    DWORD processExitCode = 0;
    GetExitCodeProcess(processHandle.get(), &processExitCode);

    if (!options.quiet) {
      fprintf(stderr, "[injector] Process exited with code %lu\n", processExitCode);
    }
  }

  return processId;
}

}  // namespace qtPilot

#endif  // Q_OS_WIN
