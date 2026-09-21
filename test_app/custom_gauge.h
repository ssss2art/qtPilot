// Copyright (c) 2024-2026 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <QColor>
#include <QWidget>

/// @brief Custom QWidget representing a status gauge.
/// Tests probe introspection of custom properties, signals, and Q_INVOKABLE methods.
class CustomGaugeWidget : public QWidget {
  Q_OBJECT

  Q_PROPERTY(int value READ value WRITE setValue NOTIFY valueChanged)
  Q_PROPERTY(int maximum READ maximum WRITE setMaximum)
  Q_PROPERTY(QColor gaugeColor READ gaugeColor WRITE setGaugeColor NOTIFY gaugeColorChanged)

 public:
  explicit CustomGaugeWidget(QWidget* parent = nullptr);

  int value() const { return value_; }
  void setValue(int val);

  int maximum() const { return max_; }
  void setMaximum(int max);

  QColor gaugeColor() const { return color_; }
  void setGaugeColor(const QColor& color);

  Q_INVOKABLE void reset();
  Q_INVOKABLE void increment(int amount = 1);

 signals:
  void valueChanged(int newValue);
  void gaugeColorChanged(const QColor& newColor);
  void thresholdReached();

 protected:
  void paintEvent(QPaintEvent* event) override;

 private:
  int value_ = 25;
  int max_ = 100;
  QColor color_ = {0, 150, 255};
};
