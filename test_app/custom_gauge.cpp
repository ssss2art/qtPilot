// Copyright (c) 2024-2026 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "custom_gauge.h"

#include <algorithm>
#include <QPaintEvent>
#include <QPainter>

CustomGaugeWidget::CustomGaugeWidget(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("customGauge"));
  setMinimumSize(120, 30);
}

void CustomGaugeWidget::setValue(int val) {
  val = std::clamp(val, 0, max_);
  if (value_ == val)
    return;
  value_ = val;
  emit valueChanged(value_);
  if (value_ >= max_) {
    emit thresholdReached();
  }
  update();
}

void CustomGaugeWidget::setMaximum(int max) {
  if (max <= 0 || max_ == max)
    return;
  max_ = max;
  if (value_ > max_) {
    setValue(max_);
  }
  update();
}

void CustomGaugeWidget::setGaugeColor(const QColor& color) {
  if (color_ == color)
    return;
  color_ = color;
  emit gaugeColorChanged(color_);
  update();
}

void CustomGaugeWidget::reset() {
  setValue(0);
}

void CustomGaugeWidget::increment(int amount) {
  setValue(value_ + amount);
}

void CustomGaugeWidget::paintEvent(QPaintEvent* /*event*/) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);

  // Background track
  QRect r = rect().adjusted(2, 2, -2, -2);
  painter.fillRect(r, QColor(220, 220, 220));

  // Progress fill
  if (max_ > 0) {
    int fillWidth = static_cast<int>((static_cast<double>(value_) / max_) * r.width());
    QRect fillRect(r.x(), r.y(), fillWidth, r.height());
    painter.fillRect(fillRect, color_);
  }

  // Border
  painter.setPen(QColor(100, 100, 100));
  painter.drawRect(r);

  // Text
  painter.setPen(Qt::black);
  QString label = QStringLiteral("%1 / %2").arg(value_).arg(max_);
  painter.drawText(r, Qt::AlignCenter, label);
}
