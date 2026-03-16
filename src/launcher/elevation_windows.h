// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#ifdef _WIN32

#include <QString>
#include <QStringList>

namespace qtPilot {

/// @brief Check whether the current process is running with elevated (admin) privileges.
/// @return true if the process token has TokenElevation set.
bool isProcessElevated();

/// @brief Re-launch the current executable with administrator privileges via UAC.
///
/// Uses ShellExecuteEx with the "runas" verb. Replaces --run-as-admin with --elevated
/// in the argument list so the elevated instance knows it's already elevated.
/// Blocks until the elevated process exits.
///
/// @param executable Path to the current executable.
/// @param args Command-line arguments (without argv[0]).
/// @return Exit code from the elevated process, or 1 on failure.
int relaunchElevated(const QString& executable, const QStringList& args);

}  // namespace qtPilot

#endif  // _WIN32
