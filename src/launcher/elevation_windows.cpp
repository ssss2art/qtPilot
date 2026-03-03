// Copyright (c) 2024 QtMCP Contributors
// SPDX-License-Identifier: MIT

// Windows-only elevation support for the launcher.
// Enables self-elevation via UAC when --run-as-admin is passed.

#include "elevation_windows.h"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <shellapi.h>

#include <cstdio>

namespace qtmcp {

bool isProcessElevated() {
  BOOL elevated = FALSE;
  HANDLE token = nullptr;
  if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
    TOKEN_ELEVATION elev = {};
    DWORD size = 0;
    if (GetTokenInformation(token, TokenElevation, &elev, sizeof(elev), &size)) {
      elevated = elev.TokenIsElevated;
    }
    CloseHandle(token);
  }
  return elevated != FALSE;
}

int relaunchElevated(const QString& executable, const QStringList& args) {
  // Build argument string: replace --run-as-admin with --elevated
  QStringList newArgs = args;
  newArgs.removeAll(QStringLiteral("--run-as-admin"));
  newArgs.prepend(QStringLiteral("--elevated"));

  // Quote arguments that contain spaces
  QStringList quotedArgs;
  for (const QString& arg : newArgs) {
    if (arg.contains(QLatin1Char(' ')) || arg.contains(QLatin1Char('"'))) {
      QString escaped = arg;
      escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
      quotedArgs.append(QStringLiteral("\"%1\"").arg(escaped));
    } else {
      quotedArgs.append(arg);
    }
  }

  // ShellExecuteEx with "runas" creates a new process with a clean environment.
  // We must propagate critical environment variables (PATH, QT_PLUGIN_PATH, QTMCP_*)
  // by wrapping the launch in cmd.exe /c "set VAR=val && ... && launcher.exe args".
  QString cmdLine;

  // Collect environment variables to forward
  QStringList envSetCmds;
  const char* envVarsToForward[] = {"PATH", "QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH",
                                     "QTMCP_PORT", "QTMCP_MODE", "QTMCP_INJECT_CHILDREN",
                                     "QTMCP_ENABLED", "QTMCP_DISCOVERY_PORT"};
  for (const char* varName : envVarsToForward) {
    QString val = QString::fromLocal8Bit(qgetenv(varName));
    if (!val.isEmpty()) {
      // Escape any special cmd characters in the value
      envSetCmds.append(QStringLiteral("set \"%1=%2\"").arg(
          QString::fromLatin1(varName), val));
    }
  }

  // Build: cmd.exe /c "set VAR=val && set VAR2=val2 && "launcher.exe" --elevated args"
  QString launcherCmd;
  if (executable.contains(QLatin1Char(' '))) {
    launcherCmd = QStringLiteral("\"%1\" %2").arg(executable, quotedArgs.join(QLatin1Char(' ')));
  } else {
    launcherCmd = QStringLiteral("%1 %2").arg(executable, quotedArgs.join(QLatin1Char(' ')));
  }

  if (envSetCmds.isEmpty()) {
    cmdLine = QStringLiteral("/c %1").arg(launcherCmd);
  } else {
    cmdLine = QStringLiteral("/c \"%1 && %2\"").arg(
        envSetCmds.join(QStringLiteral(" && ")), launcherCmd);
  }

  // Elevate cmd.exe which will set env vars then run the launcher
  wchar_t cmdExePath[MAX_PATH];
  GetSystemDirectoryW(cmdExePath, MAX_PATH);
  wcscat_s(cmdExePath, MAX_PATH, L"\\cmd.exe");

  std::wstring paramsW = cmdLine.toStdWString();

  SHELLEXECUTEINFOW sei = {};
  sei.cbSize = sizeof(sei);
  sei.lpVerb = L"runas";
  sei.lpFile = cmdExePath;
  sei.lpParameters = paramsW.c_str();
  sei.nShow = SW_SHOWNORMAL;
  sei.fMask = SEE_MASK_NOCLOSEPROCESS;

  if (!ShellExecuteExW(&sei)) {
    DWORD err = GetLastError();
    if (err == ERROR_CANCELLED) {
      fprintf(stderr, "Error: UAC elevation was cancelled by user\n");
    } else {
      fprintf(stderr, "Error: ShellExecuteEx failed (error %lu)\n", err);
    }
    return 1;
  }

  // Wait for elevated instance to finish
  WaitForSingleObject(sei.hProcess, INFINITE);
  DWORD exitCode = 0;
  GetExitCodeProcess(sei.hProcess, &exitCode);
  CloseHandle(sei.hProcess);
  return static_cast<int>(exitCode);
}

}  // namespace qtmcp

#endif  // _WIN32
