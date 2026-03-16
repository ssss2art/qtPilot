// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "core/probe.h"  // For QTPILOT_EXPORT

#include <QList>
#include <QMutex>
#include <QString>
#include <QtGlobal>

namespace qtPilot {

/// @brief Captured console message with metadata.
struct ConsoleMessage {
  QtMsgType type;        ///< Message severity (QtDebugMsg, QtWarningMsg, etc.)
  QString message;       ///< The message text
  QString file;          ///< Source file (from QMessageLogContext)
  int line = 0;          ///< Source line number
  QString function;      ///< Source function name
  qint64 timestamp = 0;  ///< Capture timestamp (msecs since epoch)
};

/// @brief Captures Qt debug/warning/critical/fatal messages into a ring buffer.
///
/// Singleton using Q_GLOBAL_STATIC pattern (consistent with ObjectRegistry,
/// SignalMonitor). Installs a Qt message handler via qInstallMessageHandler()
/// and chains to the previous handler so messages are never swallowed.
///
/// Thread-safe: all access to the message buffer is guarded by QMutex.
///
/// Usage:
///   ConsoleMessageCapture::instance()->install();
///   // ... later ...
///   auto msgs = ConsoleMessageCapture::instance()->messages("error", true, 50);
class QTPILOT_EXPORT ConsoleMessageCapture {
 public:
  /// @brief Maximum number of messages retained in the ring buffer.
  static constexpr int MAX_MESSAGES = 1000;

  /// @brief Get the singleton instance.
  static ConsoleMessageCapture* instance();

  /// @brief Install the Qt message handler.
  ///
  /// Calls qInstallMessageHandler() and saves the previous handler
  /// in m_previousHandler for chaining. Safe to call multiple times
  /// (subsequent calls are no-ops).
  void install();

  /// @brief Retrieve captured messages with optional filtering.
  /// @param pattern Regex pattern to match against message text (empty = all).
  /// @param onlyErrors If true, only return QtWarningMsg/QtCriticalMsg/QtFatalMsg.
  /// @param limit Maximum number of messages to return (0 = all matching).
  /// @return List of matching messages, newest first.
  QList<ConsoleMessage> messages(const QString& pattern = QString(), bool onlyErrors = false,
                                 int limit = 0) const;

  /// @brief Clear all captured messages.
  void clear();

  // Public constructor/destructor for Q_GLOBAL_STATIC compatibility
  ConsoleMessageCapture();
  ~ConsoleMessageCapture();

 private:
  /// @brief Static message handler installed via qInstallMessageHandler().
  static void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg);

  QList<ConsoleMessage> m_messages;              ///< Ring buffer of captured messages
  QtMessageHandler m_previousHandler = nullptr;  ///< Chained previous handler
  bool m_installed = false;                      ///< Guard against double install
  mutable QMutex m_mutex;                        ///< Thread-safety guard
};

}  // namespace qtPilot
