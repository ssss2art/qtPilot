// Copyright (c) 2024-2026 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "qml_bridge.h"

#include <QCryptographicHash>

QmlTestBridge::QmlTestBridge(QObject* parent) : QObject(parent) {
  setObjectName(QStringLiteral("qmlBridge"));
}

void QmlTestBridge::setClickCount(int count) {
  if (clickCount_ == count) return;
  clickCount_ = count;
  emit clickCountChanged(clickCount_);
}

void QmlTestBridge::setStatusText(const QString& text) {
  if (statusText_ == text) return;
  statusText_ = text;
  emit statusTextChanged(statusText_);
}

QString QmlTestBridge::computeHash(const QString& input) {
  QByteArray hash = QCryptographicHash::hash(input.toUtf8(), QCryptographicHash::Sha256);
  return QString::fromLatin1(hash.toHex());
}

void QmlTestBridge::increment() {
  setClickCount(clickCount_ + 1);
  setStatusText(QStringLiteral("Clicked %1 times").arg(clickCount_));
  emit actionReceived(QStringLiteral("increment"));
}
