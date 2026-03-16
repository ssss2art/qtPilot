// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

namespace qtPilot {

/// @brief Initialize the qtPilot probe.
///
/// This function is called automatically when the library is loaded into
/// a Qt application. On Linux, it's triggered via __attribute__((constructor))
/// when loaded via LD_PRELOAD. On Windows, it's called from DllMain.
///
/// The function checks the QTPILOT_ENABLED environment variable (defaults to "1")
/// and initializes the probe using configuration from environment variables.
void InitializeProbe();

/// @brief Shutdown the qtPilot probe.
///
/// This function is called automatically when the library is unloaded.
/// It cleanly shuts down the WebSocket server and releases resources.
void ShutdownProbe();

}  // namespace qtPilot
