// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

// Shared Windows injection implementation.
// Extracted from src/launcher/injector_windows.cpp so the probe's
// child-process hook can reuse the same logic.

#include "process_inject_windows.h"

#ifdef _WIN32

// clang-format off
#include <Windows.h>   // must precede Psapi.h
#include <Psapi.h>
// clang-format on
#include <cstdio>
#include <string>
#include <vector>

namespace qtPilot {

void printWindowsError(const char* prefix, DWORD errorCode) {
  wchar_t* msgBuf = nullptr;
  FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      reinterpret_cast<LPWSTR>(&msgBuf), 0, nullptr);

  if (msgBuf) {
    fprintf(stderr, "[injector] %s: %ls (error %lu)\n", prefix, msgBuf, errorCode);
    LocalFree(msgBuf);
  } else {
    fprintf(stderr, "[injector] %s: error %lu\n", prefix, errorCode);
  }
}

HMODULE findRemoteModule(HANDLE process, const wchar_t* moduleName) {
  HMODULE modules[1024];
  DWORD bytesNeeded = 0;

  if (!EnumProcessModules(process, modules, sizeof(modules), &bytesNeeded)) {
    return nullptr;
  }

  DWORD moduleCount = bytesNeeded / sizeof(HMODULE);
  for (DWORD i = 0; i < moduleCount; ++i) {
    wchar_t name[MAX_PATH];
    if (GetModuleBaseNameW(process, modules[i], name, MAX_PATH)) {
      if (_wcsicmp(name, moduleName) == 0) {
        return modules[i];
      }
    }
  }
  return nullptr;
}

bool injectProbeDll(HANDLE hProcess, DWORD processId, const wchar_t* dllPath, bool quiet) {
  // --- Phase 1: Load the DLL via LoadLibraryW ---

  std::wstring dllPathStr(dllPath);
  size_t dllPathSize = (dllPathStr.size() + 1) * sizeof(wchar_t);

  // Allocate memory in target process for DLL path
  void* remoteMem =
      VirtualAllocEx(hProcess, nullptr, dllPathSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  if (!remoteMem) {
    if (!quiet) {
      printWindowsError("VirtualAllocEx failed", GetLastError());
    }
    return false;
  }

  if (!quiet) {
    fprintf(stderr, "[injector] Allocated %zu bytes in PID %lu at %p\n", dllPathSize,
            static_cast<unsigned long>(processId), remoteMem);
  }

  // Write DLL path to target process memory
  SIZE_T bytesWritten = 0;
  BOOL writeResult =
      WriteProcessMemory(hProcess, remoteMem, dllPathStr.c_str(), dllPathSize, &bytesWritten);
  if (!writeResult || bytesWritten != dllPathSize) {
    if (!quiet) {
      printWindowsError("WriteProcessMemory failed", GetLastError());
    }
    VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
    return false;
  }

  if (!quiet) {
    fprintf(stderr, "[injector] Wrote DLL path: %ls\n", dllPath);
  }

  // Get address of LoadLibraryW from kernel32.dll
  HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
  if (!kernel32) {
    if (!quiet) {
      printWindowsError("GetModuleHandleW(kernel32) failed", GetLastError());
    }
    VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
    return false;
  }

  auto loadLibraryW =
      reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(kernel32, "LoadLibraryW"));
  if (!loadLibraryW) {
    if (!quiet) {
      printWindowsError("GetProcAddress(LoadLibraryW) failed", GetLastError());
    }
    VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
    return false;
  }

  if (!quiet) {
    fprintf(stderr, "[injector] LoadLibraryW at %p\n", reinterpret_cast<void*>(loadLibraryW));
  }

  // Create remote thread to call LoadLibraryW with DLL path
  HandleGuard remoteThread(
      CreateRemoteThread(hProcess, nullptr, 0, loadLibraryW, remoteMem, 0, nullptr));
  if (!remoteThread.valid()) {
    if (!quiet) {
      printWindowsError("CreateRemoteThread failed", GetLastError());
    }
    VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
    return false;
  }

  if (!quiet) {
    fprintf(stderr, "[injector] Created remote thread for DLL injection\n");
  }

  // Wait for injection thread to complete
  DWORD waitResult = WaitForSingleObject(remoteThread.get(), 10000);
  if (waitResult != WAIT_OBJECT_0) {
    if (!quiet) {
      if (waitResult == WAIT_TIMEOUT) {
        fprintf(stderr, "[injector] Injection thread timed out\n");
      } else {
        printWindowsError("WaitForSingleObject failed", GetLastError());
      }
    }
    VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
    return false;
  }

  // Check if LoadLibrary succeeded
  DWORD exitCode = 0;
  if (GetExitCodeThread(remoteThread.get(), &exitCode) && exitCode == 0) {
    if (!quiet) {
      fprintf(stderr,
              "[injector] Warning: LoadLibraryW returned NULL (DLL load may have failed)\n");
    }
  }

  if (!quiet) {
    fprintf(stderr, "[injector] DLL injection completed\n");
  }

  // Free the remote memory (no longer needed)
  VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);

  // --- Phase 2: Call exported qtpilotProbeInit in the remote process ---

  // Extract just the DLL filename from the full path
  const wchar_t* lastSlash = wcsrchr(dllPath, L'\\');
  const wchar_t* lastFwdSlash = wcsrchr(dllPath, L'/');
  const wchar_t* probeDllName = dllPath;
  if (lastSlash && lastFwdSlash) {
    probeDllName = (lastSlash > lastFwdSlash ? lastSlash : lastFwdSlash) + 1;
  } else if (lastSlash) {
    probeDllName = lastSlash + 1;
  } else if (lastFwdSlash) {
    probeDllName = lastFwdSlash + 1;
  }

  HMODULE remoteProbeBase = findRemoteModule(hProcess, probeDllName);
  if (!remoteProbeBase) {
    if (!quiet) {
      fprintf(stderr, "[injector] Warning: Could not find %ls in remote process modules\n",
              probeDllName);
    }
    // Fall through — Q_COREAPP_STARTUP_FUNCTION may still work
    return true;
  }

  // Load the probe DLL locally WITHOUT running DllMain/constructors
  HMODULE localProbe = LoadLibraryExW(dllPath, nullptr, DONT_RESOLVE_DLL_REFERENCES);
  if (!localProbe) {
    if (!quiet) {
      printWindowsError("LoadLibraryExW (local, no-init) failed", GetLastError());
    }
    return true;  // DLL is loaded remotely, init may still work
  }

  // Get local address of the exported init function
  auto localFunc =
      reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(localProbe, "qtpilotProbeInit"));

  if (!localFunc) {
    if (!quiet) {
      fprintf(stderr, "[injector] Warning: qtpilotProbeInit not found in probe DLL exports\n");
    }
    FreeLibrary(localProbe);
    return true;
  }

  // Calculate the remote address via offset from base
  auto offset =
      reinterpret_cast<const char*>(localFunc) - reinterpret_cast<const char*>(localProbe);
  auto remoteFunc = reinterpret_cast<LPTHREAD_START_ROUTINE>(
      reinterpret_cast<const char*>(remoteProbeBase) + offset);

  if (!quiet) {
    fprintf(stderr, "[injector] Calling qtpilotProbeInit at remote address %p\n",
            reinterpret_cast<void*>(remoteFunc));
  }

  // Call qtpilotProbeInit via CreateRemoteThread
  HandleGuard initThread(CreateRemoteThread(hProcess, nullptr, 0, remoteFunc, nullptr, 0, nullptr));

  if (!initThread.valid()) {
    if (!quiet) {
      printWindowsError("CreateRemoteThread (init) failed", GetLastError());
    }
    FreeLibrary(localProbe);
    return true;  // DLL loaded, just init failed
  }

  DWORD initWait = WaitForSingleObject(initThread.get(), 10000);
  if (initWait != WAIT_OBJECT_0) {
    if (!quiet) {
      fprintf(stderr, "[injector] Warning: qtpilotProbeInit thread did not complete in time\n");
    }
  } else if (!quiet) {
    fprintf(stderr, "[injector] qtpilotProbeInit completed successfully\n");
  }

  FreeLibrary(localProbe);
  return true;
}

}  // namespace qtPilot

#endif  // _WIN32
