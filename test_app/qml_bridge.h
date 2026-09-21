// Copyright (c) 2024-2026 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <QObject>
#include <QString>

/// @brief Bridge object exposed to the QML context for hybrid QWidget/QML testing.
class QmlTestBridge : public QObject {
  Q_OBJECT

  Q_PROPERTY(int clickCount READ clickCount WRITE setClickCount NOTIFY clickCountChanged)
  Q_PROPERTY(QString statusText READ statusText WRITE setStatusText NOTIFY statusTextChanged)

 public:
  explicit QmlTestBridge(QObject* parent = nullptr);

  int clickCount() const { return clickCount_; }
  void setClickCount(int count);

  QString statusText() const { return statusText_; }
  void setStatusText(const QString& text);

  Q_INVOKABLE QString computeHash(const QString& input);
  Q_INVOKABLE void increment();

 signals:
  void clickCountChanged(int newCount);
  void statusTextChanged(const QString& newText);
  void actionReceived(const QString& action);

 private:
  int clickCount_ = 0;
  QString statusText_ = QStringLiteral("QML Initialized");
};
