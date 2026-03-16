// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

// Shared Windows injection utilities used by both the launcher and the probe's
// child-process hook.  Pure Win32 — no Qt dependency.

#pragma once

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace qtPilot {

/// @brief RAII wrapper for Windows HANDLE to ensure cleanup.
class HandleGuard {
 public:
  explicit HandleGuard(HANDLE h = nullptr) : m_handle(h) {}
  ~HandleGuard() {
    if (m_handle && m_handle != INVALID_HANDLE_VALUE) {
      CloseHandle(m_handle);
    }
  }
  HandleGuard(const HandleGuard&) = delete;
  HandleGuard& operator=(const HandleGuard&) = delete;
  HandleGuard(HandleGuard&& other) noexcept : m_handle(other.m_handle) { other.m_handle = nullptr; }
  HandleGuard& operator=(HandleGuard&& other) noexcept {
    if (this != &other) {
      if (m_handle && m_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(m_handle);
      }
      m_handle = other.m_handle;
      other.m_handle = nullptr;
    }
    return *this;
  }

  HANDLE get() const { return m_handle; }
  HANDLE release() {
    HANDLE h = m_handle;
    m_handle = nullptr;
    return h;
  }
  bool valid() const { return m_handle && m_handle != INVALID_HANDLE_VALUE; }

 private:
  HANDLE m_handle;
};

/// @brief Print a Windows error message to stderr.
void printWindowsError(const char* prefix, DWORD errorCode);

/// @brief Find a loaded module in a remote process by filename.
/// @return The module base address in the remote process, or nullptr if not found.
HMODULE findRemoteModule(HANDLE process, const wchar_t* moduleName);

/// @brief Inject the probe DLL into a (typically suspended) process.
///
/// Performs the full sequence: VirtualAllocEx -> WriteProcessMemory ->
/// CreateRemoteThread(LoadLibraryW) -> wait -> findRemoteModule ->
/// calculate qtpilotProbeInit offset -> CreateRemoteThread(init) -> wait -> cleanup.
///
/// @param hProcess Handle to the target process (must have full access rights).
/// @param processId The target's PID (used only for diagnostics).
/// @param dllPath   Absolute path to the probe DLL on disk.
/// @param quiet     Suppress diagnostic messages when true.
/// @return true on success, false on failure.
bool injectProbeDll(HANDLE hProcess, DWORD processId, const wchar_t* dllPath, bool quiet = true);

}  // namespace qtPilot

#endif  // _WIN32
