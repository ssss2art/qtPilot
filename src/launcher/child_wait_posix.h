// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <sys/types.h>

namespace qtPilot {

/// @brief Wait for a launched child, passing termination signals on to it.
///
/// A caller that stops a foreground launcher knows only the launcher's pid. If
/// the launcher dies alone, the app is orphaned and keeps its probe port. So
/// while this waits, SIGTERM, SIGINT, SIGHUP and SIGQUIT sent to the launcher
/// are forwarded to the child, and the launcher exits once the child has.
///
/// @return the child's wait status, or -1 if waitpid failed.
int waitForChildForwardingSignals(pid_t child, bool quiet);

}  // namespace qtPilot
