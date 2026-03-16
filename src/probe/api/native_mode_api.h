// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "transport/jsonrpc_handler.h"  // For QTPILOT_EXPORT, JsonRpcHandler

#include <QObject>

namespace qtPilot {

/// @brief Native Mode API surface registering all qt.* namespaced JSON-RPC methods.
///
/// This is the core deliverable of Phase 3 - the polished, agent-friendly API
/// surface that agents use for all Qt introspection in Native Mode.
///
/// Registers ~30 methods across 7 domains:
///   - qt.objects.*  - Object discovery and inspection
///   - qt.properties.* - Property access
///   - qt.methods.*  - Method invocation
///   - qt.signals.*  - Signal monitoring
///   - qt.ui.*       - UI interaction (click, keys, screenshot, geometry, hit test)
///   - qt.names.*    - Symbolic name map management
///   - qt.ping/qt.version/qt.modes - System methods
///
/// All methods use ObjectResolver for ID resolution, ResponseEnvelope for
/// response wrapping, and ErrorCode constants for structured errors.
///
/// Old qtpilot.* methods remain registered in JsonRpcHandler for backward
/// compatibility.
class QTPILOT_EXPORT NativeModeApi : public QObject {
  Q_OBJECT

 public:
  /// @brief Construct and register all qt.* methods on the given handler.
  /// @param handler The JSON-RPC handler to register methods on.
  /// @param parent Parent QObject.
  explicit NativeModeApi(JsonRpcHandler* handler, QObject* parent = nullptr);

 private:
  void registerObjectMethods();    ///< qt.objects.*
  void registerPropertyMethods();  ///< qt.properties.*
  void registerMethodMethods();    ///< qt.methods.*
  void registerSignalMethods();    ///< qt.signals.*
  void registerUiMethods();        ///< qt.ui.*
  void registerNameMapMethods();   ///< qt.names.*
  void registerSystemMethods();    ///< qt.ping, qt.version, qt.modes
  void registerEventMethods();     ///< qt.events.*
  void registerQmlMethods();       ///< qt.qml.*
  void registerModelMethods();     ///< qt.models.*

  JsonRpcHandler* m_handler;
};

}  // namespace qtPilot
