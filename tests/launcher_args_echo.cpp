// Copyright (c) 2026 qtPilot Contributors
// SPDX-License-Identifier: MIT

// Launch target for test_launcher_args: writes each argument it was given, one per line, to
// args.txt in its working directory. Deliberately not a Qt application -- a QCoreApplication
// would itself remove the arguments Qt reserves (-qmljsdebugger), hiding what the launcher passed.

#include <fstream>

int main(int argc, char* argv[]) {
  std::ofstream out("args.txt", std::ios::binary);
  for (int i = 1; i < argc; ++i)
    out << argv[i] << '\n';
  return out ? 0 : 3;
}
