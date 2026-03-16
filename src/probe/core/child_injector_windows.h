// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

// Detours-based CreateProcessW hook that automatically injects the probe DLL
// into child processes.  Enabled when QTPILOT_INJECT_CHILDREN=1 is set.

#pragma once

#ifdef _WIN32

namespace qtPilot {

/// Install the CreateProcessW hook via Microsoft Detours.
/// Must be called from the main thread (or at least while no other threads are
/// calling CreateProcessW).
void installChildProcessHook();

/// Remove the CreateProcessW hook.
void uninstallChildProcessHook();

}  // namespace qtPilot

#endif  // _WIN32
