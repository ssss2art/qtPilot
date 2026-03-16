// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "transport/jsonrpc_handler.h"  // For QTPILOT_EXPORT, JsonRpcHandler

#include <QObject>

namespace qtPilot {

/// @brief Computer Use Mode API surface registering all cu.* namespaced JSON-RPC methods.
///
/// This is the core deliverable of Phase 4 - the Chrome-compatible computer use API
/// surface that provides coordinate-based screen interaction methods.
///
/// Registers 13 methods across 5 domains:
///   - cu.screenshot       - Screen/window capture
///   - cu.click, cu.rightClick, cu.middleClick, cu.doubleClick - Click actions
///   - cu.mouseMove, cu.drag, cu.mouseDown, cu.mouseUp - Mouse primitives
///   - cu.type, cu.key     - Keyboard input
///   - cu.scroll            - Scroll wheel
///   - cu.cursorPosition    - Cursor query
///
/// All methods use coordinate-based targeting (no objectId needed).
/// Coordinates are window-relative by default, or screen-absolute with screenAbsolute=true.
/// All responses wrapped with ResponseEnvelope::wrap().
class QTPILOT_EXPORT ComputerUseModeApi : public QObject {
  Q_OBJECT

 public:
  /// @brief Construct and register all cu.* methods on the given handler.
  /// @param handler The JSON-RPC handler to register methods on.
  /// @param parent Parent QObject.
  explicit ComputerUseModeApi(JsonRpcHandler* handler, QObject* parent = nullptr);

 private:
  void registerScreenshotMethods();  ///< cu.screenshot
  void registerMouseMethods();       ///< cu.click, cu.rightClick, cu.middleClick, cu.doubleClick,
                                     ///< cu.mouseMove, cu.drag, cu.mouseDown, cu.mouseUp
  void registerKeyboardMethods();    ///< cu.type, cu.key
  void registerScrollMethod();       ///< cu.scroll
  void registerQueryMethods();       ///< cu.cursorPosition

  JsonRpcHandler* m_handler;
};

}  // namespace qtPilot
