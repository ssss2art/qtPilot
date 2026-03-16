// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

// Hooks CreateProcessW via Microsoft Detours so that every child process
// automatically gets the qtPilot probe DLL injected.
//
// How it works:
// 1. Hooked_CreateProcessW forces CREATE_SUSPENDED in dwCreationFlags.
// 2. Calls the real CreateProcessW.
// 3. On success, calls injectProbeDll() from the shared library.
// 4. If the caller did NOT request CREATE_SUSPENDED, resumes the main thread.
//
// Environment inheritance handles recursion: children inherit QTPILOT_PORT=0
// and QTPILOT_INJECT_CHILDREN=1, so the probe in the child also hooks
// CreateProcessW for grandchildren.

#ifdef _WIN32

#include "child_injector_windows.h"

#include "process_inject_windows.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cstdio>
#include <detours.h>

// Declared in probe_init_windows.cpp — returns the absolute path to this DLL.
extern "C" __declspec(dllimport) const wchar_t* qtpilotGetProbeDllPath();

namespace {

// Pointer to the real CreateProcessW.  Detours will patch this.
static decltype(&CreateProcessW) Real_CreateProcessW = CreateProcessW;

static bool g_hookInstalled = false;

BOOL WINAPI Hooked_CreateProcessW(LPCWSTR lpApplicationName, LPWSTR lpCommandLine,
                                  LPSECURITY_ATTRIBUTES lpProcessAttributes,
                                  LPSECURITY_ATTRIBUTES lpThreadAttributes, BOOL bInheritHandles,
                                  DWORD dwCreationFlags, LPVOID lpEnvironment,
                                  LPCWSTR lpCurrentDirectory, LPSTARTUPINFOW lpStartupInfo,
                                  LPPROCESS_INFORMATION lpProcessInformation) {
  // Remember whether the caller originally requested suspended
  bool callerWantsSuspended = (dwCreationFlags & CREATE_SUSPENDED) != 0;

  // Force CREATE_SUSPENDED so we can inject before the child runs
  dwCreationFlags |= CREATE_SUSPENDED;

  BOOL result = Real_CreateProcessW(
      lpApplicationName, lpCommandLine, lpProcessAttributes, lpThreadAttributes, bInheritHandles,
      dwCreationFlags, lpEnvironment, lpCurrentDirectory, lpStartupInfo, lpProcessInformation);

  if (!result) {
    return FALSE;
  }

  // Inject the probe DLL into the child
  const wchar_t* dllPath = qtpilotGetProbeDllPath();
  if (dllPath && dllPath[0] != L'\0') {
    qtPilot::injectProbeDll(lpProcessInformation->hProcess, lpProcessInformation->dwProcessId,
                          dllPath, /*quiet=*/true);
  }

  // If the caller did NOT request suspended, resume the main thread now
  if (!callerWantsSuspended) {
    ResumeThread(lpProcessInformation->hThread);
  }

  return TRUE;
}

}  // namespace

namespace qtPilot {

void installChildProcessHook() {
  if (g_hookInstalled) {
    return;
  }

  DetourTransactionBegin();
  DetourUpdateThread(GetCurrentThread());
  DetourAttach(reinterpret_cast<PVOID*>(&Real_CreateProcessW), Hooked_CreateProcessW);
  LONG error = DetourTransactionCommit();

  if (error == NO_ERROR) {
    g_hookInstalled = true;
    fprintf(stderr, "[qtPilot] Child process injection hook installed\n");
  } else {
    fprintf(stderr, "[qtPilot] Failed to install child process hook (Detours error %ld)\n", error);
  }
}

void uninstallChildProcessHook() {
  if (!g_hookInstalled) {
    return;
  }

  DetourTransactionBegin();
  DetourUpdateThread(GetCurrentThread());
  DetourDetach(reinterpret_cast<PVOID*>(&Real_CreateProcessW), Hooked_CreateProcessW);
  LONG error = DetourTransactionCommit();

  if (error == NO_ERROR) {
    g_hookInstalled = false;
    fprintf(stderr, "[qtPilot] Child process injection hook removed\n");
  } else {
    fprintf(stderr, "[qtPilot] Failed to remove child process hook (Detours error %ld)\n", error);
  }
}

}  // namespace qtPilot

#endif  // _WIN32
