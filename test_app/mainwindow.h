// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <QMainWindow>

namespace Ui {
class MainWindow;
}

/// @brief Main window for the qtPilot test application.
///
/// This window provides a comprehensive set of Qt widgets for testing
/// the qtPilot introspection and automation capabilities.
class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow() override;

 private slots:
  void OnSubmitClicked();
  void OnClearClicked();
  void OnSliderChanged(int value);
  void OnSpawnChildClicked();

 private:
  Ui::MainWindow* ui_;
};
