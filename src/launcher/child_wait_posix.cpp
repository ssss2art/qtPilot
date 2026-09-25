// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "child_wait_posix.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <sys/wait.h>

namespace qtPilot {

namespace {

volatile sig_atomic_t g_child = 0;

constexpr int kForwardedSignals[] = {SIGTERM, SIGINT, SIGHUP, SIGQUIT};

void forwardToChild(int sig) {
  // kill() is async-signal-safe; nothing else may run here.
  if (g_child > 0) {
    kill(static_cast<pid_t>(g_child), sig);
  }
}

}  // namespace

int waitForChildForwardingSignals(pid_t child, bool quiet) {
  g_child = child;

  struct sigaction forward{};
  forward.sa_handler = forwardToChild;
  sigemptyset(&forward.sa_mask);
  // No SA_RESTART: waitpid returns EINTR, and the loop below waits again.
  forward.sa_flags = 0;
  for (int sig : kForwardedSignals) {
    sigaction(sig, &forward, nullptr);
  }

  int status = 0;
  pid_t result = 0;
  do {
    result = waitpid(child, &status, 0);
  } while (result < 0 && errno == EINTR);

  g_child = 0;
  if (result < 0) {
    if (!quiet) {
      perror("[injector] waitpid failed");
    }
    return -1;
  }
  if (!quiet) {
    if (WIFEXITED(status)) {
      fprintf(stderr, "[injector] Process exited with code %d\n", WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
      fprintf(stderr, "[injector] Process killed by signal %d\n", WTERMSIG(status));
    }
  }
  return status;
}

}  // namespace qtPilot
