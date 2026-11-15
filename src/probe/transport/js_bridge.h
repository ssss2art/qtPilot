// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "core/qtpilot_export.h"

#include <QObject>
#include <QString>

namespace qtPilot {

class JsonRpcHandler;

/// The probe's transport in a WebAssembly build, where a page cannot listen on a port.
///
/// Installs `globalThis.qtpilot` in the page:
///   - `qtpilot.call(json)` hands one JSON-RPC request to the handler and returns the reply string;
///   - `qtpilot.notifications` holds the newest notifications (oldest dropped past a bound), and
///     `qtpilot.onnotification`, if set to a function, is called with each one.
///
/// A client reaches it through whatever runs script in the page: Playwright's page.evaluate, the
/// DevTools console, or a WebMCP tool. Calls run on the browser's main thread, which is Qt's GUI
/// thread in a Qt WebAssembly build, so handlers run where they would over a socket.
class QTPILOT_EXPORT JsBridge : public QObject {
  Q_OBJECT

 public:
  explicit JsBridge(QObject* parent = nullptr);
  ~JsBridge() override;

  /// Publishes `globalThis.qtpilot`. Only one bridge may be installed.
  bool install();

  JsonRpcHandler* rpcHandler() const;

  /// Queues @p message (a JSON-RPC notification) for the page.
  void sendNotification(const QString& message);

  /// The installed bridge, or null: what `qtpilot.call` dispatches to.
  static JsBridge* current();

 private:
  JsonRpcHandler* m_handler;
};

}  // namespace qtPilot
