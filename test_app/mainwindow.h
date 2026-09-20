// Copyright (c) 2024-2026 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <QMainWindow>
#include <QStandardItemModel>

class CustomGaugeWidget;
class ComplexTableModel;
class QTableView;
class QGraphicsScene;
class QGraphicsView;
#if defined(QTPILOT_HAS_QUICKWIDGETS)
class QQuickWidget;
class QmlTestBridge;
#endif

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
  void OnSpawnStormClicked();
  void OnClearStormClicked();
  void OnOpenModalClicked();

 private:
  Ui::MainWindow* ui_;
  QStandardItemModel* treeModel_ = nullptr;
  CustomGaugeWidget* customGauge_ = nullptr;
  ComplexTableModel* complexTableModel_ = nullptr;
  QTableView* complexTableView_ = nullptr;
  QGraphicsScene* graphicsScene_ = nullptr;
  QGraphicsView* graphicsView_ = nullptr;
  QWidget* stormContainer_ = nullptr;
#if defined(QTPILOT_HAS_QUICKWIDGETS)
  QQuickWidget* quickWidget_ = nullptr;
  QmlTestBridge* qmlBridge_ = nullptr;
#endif
};
