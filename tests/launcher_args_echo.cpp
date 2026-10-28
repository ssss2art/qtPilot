// Copyright (c) 2026 qtPilot Contributors
// SPDX-License-Identifier: MIT

// Launch target for test_launcher_args: writes each argument it was given, one per line in UTF-8,
// to args.txt in its working directory. Deliberately not a Qt application -- a QCoreApplication
// would itself remove the arguments Qt reserves (-qmljsdebugger), hiding what the launcher passed.

#include <fstream>

#ifdef _WIN32
#include <string>
#include <windows.h>

// The wide entry point, so arguments outside the ANSI code page arrive intact.
int wmain(int argc, wchar_t* argv[]) {
  std::ofstream out("args.txt", std::ios::binary);
  for (int i = 1; i < argc; ++i) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<size_t>(size > 0 ? size - 1 : 0), '\0');
    WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, utf8.data(), size, nullptr, nullptr);
    out << utf8 << '\n';
  }
  return out ? 0 : 3;
}
#else
int main(int argc, char* argv[]) {
  std::ofstream out("args.txt", std::ios::binary);
  for (int i = 1; i < argc; ++i)
    out << argv[i] << '\n';
  return out ? 0 : 3;
}
#endif
