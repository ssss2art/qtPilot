// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <sys/types.h>
#include <sys/wait.h>

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

/// @brief Convert a waitpid() status to a launcher exit code.
/// @return WEXITSTATUS(status) on normal exit, 128 + WTERMSIG(status) on signal termination,
///         or 1 otherwise.
inline int exitCodeFromWaitStatus(int status) {
  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  if (WIFSIGNALED(status)) {
    return 128 + WTERMSIG(status);
  }
  return 1;
}

}  // namespace qtPilot
