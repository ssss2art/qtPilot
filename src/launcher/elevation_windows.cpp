// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

// Windows-only elevation support for the launcher.
// Enables self-elevation via UAC when --run-as-admin is passed.

#include "elevation_windows.h"

#include "windows_command_line.h"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cstdio>
#include <shellapi.h>

namespace qtPilot {

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
  // ShellExecuteEx with "runas" creates a new process with a clean environment.
  // We must propagate critical environment variables (PATH, QT_PLUGIN_PATH, QTPILOT_*)
  // by wrapping the launch in cmd.exe /c "set VAR=val && ... && launcher.exe args".
  QList<QPair<QString, QString>> envVars;
  for (const char* varName : kElevatedEnvVarsToForward) {
    const QString val = QString::fromLocal8Bit(qgetenv(varName));
    if (!val.isEmpty()) {
      envVars.append({QString::fromLatin1(varName), val});
    }
  }

  const QString cmdLine = buildElevatedCmdParameters(executable, args, envVars);

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

}  // namespace qtPilot

#endif  // _WIN32
