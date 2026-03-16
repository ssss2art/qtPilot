// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "console_message_capture.h"

#include <QDateTime>
#include <QGlobalStatic>
#include <QMutexLocker>
#include <QRegularExpression>

namespace qtPilot {

Q_GLOBAL_STATIC(ConsoleMessageCapture, s_instance)

ConsoleMessageCapture* ConsoleMessageCapture::instance() {
  return s_instance();
}

ConsoleMessageCapture::ConsoleMessageCapture() = default;
ConsoleMessageCapture::~ConsoleMessageCapture() = default;

void ConsoleMessageCapture::install() {
  QMutexLocker locker(&m_mutex);
  if (m_installed)
    return;

  m_previousHandler = qInstallMessageHandler(messageHandler);
  m_installed = true;
}

void ConsoleMessageCapture::messageHandler(QtMsgType type, const QMessageLogContext& context,
                                           const QString& msg) {
  // Guard against calls during static destruction
  if (s_instance.isDestroyed())
    return;

  // Record the message into the ring buffer
  ConsoleMessage cm;
  cm.type = type;
  cm.message = msg;
  cm.file = context.file ? QString::fromUtf8(context.file) : QString();
  cm.line = context.line;
  cm.function = context.function ? QString::fromUtf8(context.function) : QString();
  cm.timestamp = QDateTime::currentMSecsSinceEpoch();

  auto* self = instance();
  {
    QMutexLocker locker(&self->m_mutex);
    self->m_messages.append(cm);
    // Enforce ring buffer size limit
    while (self->m_messages.size() > MAX_MESSAGES)
      self->m_messages.removeFirst();
  }

  // Chain to previous handler (never swallow messages)
  if (self->m_previousHandler)
    self->m_previousHandler(type, context, msg);
}

QList<ConsoleMessage> ConsoleMessageCapture::messages(const QString& pattern, bool onlyErrors,
                                                      int limit) const {
  QMutexLocker locker(&m_mutex);

  QRegularExpression regex;
  bool hasPattern = !pattern.isEmpty();
  if (hasPattern) {
    regex.setPattern(pattern);
    if (!regex.isValid())
      hasPattern = false;  // Invalid regex: treat as no filter
  }

  QList<ConsoleMessage> result;

  // Iterate in reverse order (newest first)
  for (int i = m_messages.size() - 1; i >= 0; --i) {
    const auto& cm = m_messages.at(i);

    // Filter by error level
    if (onlyErrors) {
      if (cm.type != QtWarningMsg && cm.type != QtCriticalMsg && cm.type != QtFatalMsg)
        continue;
    }

    // Filter by pattern
    if (hasPattern) {
      if (!regex.match(cm.message).hasMatch())
        continue;
    }

    result.append(cm);

    // Enforce limit
    if (limit > 0 && result.size() >= limit)
      break;
  }

  return result;
}

void ConsoleMessageCapture::clear() {
  QMutexLocker locker(&m_mutex);
  m_messages.clear();
}

}  // namespace qtPilot
