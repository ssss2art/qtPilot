// Copyright (c) 2026 qtPilot Contributors
// SPDX-License-Identifier: MIT

// Launch target for test_launcher_exit: optionally exits with a specified code (default 0). The
// test needs something for the launcher to inject into; a binary of our own, never an
// operating-system tool (see AGENTS.md).

#include <charconv>
#include <string_view>

int main(int argc, char* argv[]) {
  int exitCode = 0;
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::string_view(argv[i]) == "--exit-code") {
      const std::string_view value(argv[i + 1]);
      std::from_chars(value.data(), value.data() + value.size(), exitCode);
      break;
    }
  }
  return exitCode;
}
