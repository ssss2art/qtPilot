// Copyright (c) 2024-2026 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "mainwindow.h"

#include <QCoreApplication>
#include <QDialog>
#include <QGraphicsEllipseItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QStandardItem>
#include <QTableView>
#include <QVBoxLayout>

#if defined(QTPILOT_HAS_QUICKWIDGETS)
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWidget>
#include <QUrl>
#include "qml_bridge.h"
#endif

#include "complex_table_model.h"
#include "custom_gauge.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), ui_(new Ui::MainWindow) {
  ui_->setupUi(this);

  // Populate the tree model for the Tree tab.
  // 3-level hierarchy + a synthetic 1200-row parent to exercise pagination.
  treeModel_ = new QStandardItemModel(this);
  treeModel_->setHorizontalHeaderLabels({"Name", "Type", "Count"});

  auto addRow = [](QStandardItem* parent, const QString& name, const QString& type,
                   const QString& count) {
    QList<QStandardItem*> row{new QStandardItem(name), new QStandardItem(type),
                              new QStandardItem(count)};
    parent->appendRow(row);
    return row.front();
  };
  auto addTopRow = [this](const QString& name, const QString& type, const QString& count) {
    QList<QStandardItem*> row{new QStandardItem(name), new QStandardItem(type),
                              new QStandardItem(count)};
    treeModel_->appendRow(row);
    return row.front();
  };

  QStandardItem* mfgA = addTopRow("ManufacturerA", "Manufacturer", "");
  QStandardItem* modelX = addRow(mfgA, "DeviceModelX direct", "Device", "0");
  addRow(modelX, "Profile 8ch", "Profile", "0");
  addRow(modelX, "Profile 12ch", "Profile", "0");
  addRow(mfgA, "DeviceModelY", "Device", "0");

  QStandardItem* mfgB = addTopRow("ManufacturerB", "Manufacturer", "");
  QStandardItem* modelZ = addRow(mfgB, "DeviceModelZ", "Device", "0");
  addRow(modelZ, "Profile 8ch", "Profile", "0");
  addRow(modelZ, "Profile 12ch", "Profile", "0");

  // Synthetic 1200-row child set under a dedicated parent to exercise pagination.
  QStandardItem* bulk = addTopRow("BulkManufacturer", "Manufacturer", "");
  for (int i = 0; i < 1200; ++i) {
    addRow(bulk, QStringLiteral("Device %1").arg(i, 4, 10, QChar('0')),
           "Device", QString::number(i));
  }

  ui_->treeView->setModel(treeModel_);
  ui_->treeView->setObjectName(QStringLiteral("treeView"));

  // Set explicit object names for form fields
  ui_->nameEdit->setObjectName(QStringLiteral("nameEdit"));
  ui_->emailEdit->setObjectName(QStringLiteral("emailEdit"));
  ui_->messageEdit->setObjectName(QStringLiteral("messageEdit"));

  // Add custom gauge to Form tab
  customGauge_ = new CustomGaugeWidget(this);
  customGauge_->setObjectName(QStringLiteral("customGauge"));
  ui_->inputFormLayout->addRow(new QLabel(QStringLiteral("Custom Gauge:"), this), customGauge_);

  // Populate Table tab with ComplexTableModel
  complexTableModel_ = new ComplexTableModel(this);
  complexTableModel_->setObjectName(QStringLiteral("complexTableModel"));
  complexTableView_ = new QTableView(this);
  complexTableView_->setObjectName(QStringLiteral("complexTableView"));
  complexTableView_->setModel(complexTableModel_);
  ui_->tableLayout->addWidget(complexTableView_);

  // Canvas tab with QGraphicsView and QGraphicsScene
  graphicsScene_ = new QGraphicsScene(this);
  graphicsScene_->setObjectName(QStringLiteral("graphicsScene"));
  graphicsScene_->setSceneRect(0, 0, 400, 300);

  auto* rectItem = graphicsScene_->addRect(20, 20, 100, 60, QPen(Qt::black), QBrush(Qt::cyan));
  rectItem->setFlag(QGraphicsItem::ItemIsSelectable);
  rectItem->setFlag(QGraphicsItem::ItemIsMovable);

  auto* ellipseItem = graphicsScene_->addEllipse(150, 40, 80, 80, QPen(Qt::darkBlue), QBrush(Qt::yellow));
  ellipseItem->setFlag(QGraphicsItem::ItemIsSelectable);
  ellipseItem->setFlag(QGraphicsItem::ItemIsMovable);

  auto* textItem = graphicsScene_->addSimpleText(QStringLiteral("qtPilot Canvas Item"));
  textItem->setPos(50, 150);

  graphicsView_ = new QGraphicsView(graphicsScene_, this);
  graphicsView_->setObjectName(QStringLiteral("graphicsView"));

  auto* canvasTab = new QWidget();
  canvasTab->setObjectName(QStringLiteral("canvasTab"));
  auto* canvasLayout = new QVBoxLayout(canvasTab);
  canvasLayout->addWidget(graphicsView_);
  ui_->tabWidget->addTab(canvasTab, QStringLiteral("Canvas"));

  // Lifecycle tab for object storm and modal testing
  auto* lifecycleTab = new QWidget();
  lifecycleTab->setObjectName(QStringLiteral("lifecycleTab"));
  auto* lifecycleLayout = new QVBoxLayout(lifecycleTab);

  auto* stormBtnLayout = new QHBoxLayout();
  auto* spawnStormBtn = new QPushButton(QStringLiteral("Spawn 100 Objects"), lifecycleTab);
  spawnStormBtn->setObjectName(QStringLiteral("spawnStormButton"));
  auto* clearStormBtn = new QPushButton(QStringLiteral("Clear Objects"), lifecycleTab);
  clearStormBtn->setObjectName(QStringLiteral("clearStormButton"));
  auto* openModalBtn = new QPushButton(QStringLiteral("Open Modal Dialog"), lifecycleTab);
  openModalBtn->setObjectName(QStringLiteral("openModalButton"));

  stormBtnLayout->addWidget(spawnStormBtn);
  stormBtnLayout->addWidget(clearStormBtn);
  stormBtnLayout->addWidget(openModalBtn);
  lifecycleLayout->addLayout(stormBtnLayout);

  stormContainer_ = new QWidget(lifecycleTab);
  stormContainer_->setObjectName(QStringLiteral("stormContainer"));
  new QVBoxLayout(stormContainer_);
  lifecycleLayout->addWidget(stormContainer_);

  connect(spawnStormBtn, &QPushButton::clicked, this, &MainWindow::OnSpawnStormClicked);
  connect(clearStormBtn, &QPushButton::clicked, this, &MainWindow::OnClearStormClicked);
  connect(openModalBtn, &QPushButton::clicked, this, &MainWindow::OnOpenModalClicked);

  ui_->tabWidget->addTab(lifecycleTab, QStringLiteral("Lifecycle"));

#if defined(QTPILOT_HAS_QUICKWIDGETS)
  quickWidget_ = new QQuickWidget(this);
  quickWidget_->setObjectName(QStringLiteral("quickWidget"));
  quickWidget_->setResizeMode(QQuickWidget::SizeRootObjectToView);

  qmlBridge_ = new QmlTestBridge(this);
  quickWidget_->engine()->rootContext()->setContextProperty(QStringLiteral("qmlBridge"), qmlBridge_);
  quickWidget_->setSource(QUrl(QStringLiteral("qrc:/qml/EmbeddedScene.qml")));

  auto* qmlTab = new QWidget();
  qmlTab->setObjectName(QStringLiteral("qmlTab"));
  auto* qmlLayout = new QVBoxLayout(qmlTab);
  qmlLayout->addWidget(quickWidget_);
  ui_->tabWidget->addTab(qmlTab, QStringLiteral("QML"));
#endif

  // Connect signals
  connect(ui_->submitButton, &QPushButton::clicked, this, &MainWindow::OnSubmitClicked);
  connect(ui_->clearButton, &QPushButton::clicked, this, &MainWindow::OnClearClicked);
  connect(ui_->slider, &QSlider::valueChanged, this, &MainWindow::OnSliderChanged);
  connect(ui_->slider, &QSlider::valueChanged, customGauge_, &CustomGaugeWidget::setValue);

  // Add "Spawn Child Process" button next to Submit/Clear
  auto* spawnButton = new QPushButton(QStringLiteral("Spawn Child Process"), this);
  spawnButton->setObjectName(QStringLiteral("spawnChildButton"));
  ui_->buttonLayout->insertWidget(2, spawnButton);
  connect(spawnButton, &QPushButton::clicked, this, &MainWindow::OnSpawnChildClicked);

  // Initialize status bar
  statusBar()->showMessage("Ready");
}

MainWindow::~MainWindow() { delete ui_; }

void MainWindow::OnSubmitClicked() {
  QString name = ui_->nameEdit->text();
  QString email = ui_->emailEdit->text();
  QString message = ui_->messageEdit->toPlainText();

  QString result = QString("Name: %1\nEmail: %2\nMessage: %3").arg(name, email, message);
  ui_->resultText->setPlainText(result);

  statusBar()->showMessage("Form submitted", 3000);
}

void MainWindow::OnClearClicked() {
  ui_->nameEdit->clear();
  ui_->emailEdit->clear();
  ui_->messageEdit->clear();
  ui_->resultText->clear();
  ui_->slider->setValue(50);
  ui_->checkBox->setChecked(false);
  ui_->comboBox->setCurrentIndex(0);
  if (customGauge_) {
    customGauge_->setValue(50);
  }

  statusBar()->showMessage("Form cleared", 3000);
}

void MainWindow::OnSliderChanged(int value) {
  ui_->sliderValueLabel->setText(QString::number(value));
}

void MainWindow::OnSpawnChildClicked() {
  QString exe = QCoreApplication::applicationFilePath();
  QStringList args = {QStringLiteral("--child")};

  qint64 pid = 0;
  bool ok = QProcess::startDetached(exe, args, QString(), &pid);

  if (ok) {
    statusBar()->showMessage(QString("Spawned child process (PID %1)").arg(pid), 5000);
  } else {
    statusBar()->showMessage("Failed to spawn child process", 5000);
  }
}

void MainWindow::OnSpawnStormClicked() {
  QLayout* layout = stormContainer_->layout();
  for (int i = 0; i < 100; ++i) {
    auto* label = new QLabel(QStringLiteral("Storm Node %1").arg(i), stormContainer_);
    label->setObjectName(QStringLiteral("stormLabel_%1").arg(i));
    layout->addWidget(label);
  }
  statusBar()->showMessage(QStringLiteral("Spawned 100 objects"), 2000);
}

void MainWindow::OnClearStormClicked() {
  QLayout* layout = stormContainer_->layout();
  QLayoutItem* child;
  while ((child = layout->takeAt(0)) != nullptr) {
    if (child->widget()) {
      child->widget()->deleteLater();
    }
    delete child;
  }
  statusBar()->showMessage(QStringLiteral("Cleared storm objects"), 2000);
}

void MainWindow::OnOpenModalClicked() {
  auto* dialog = new QDialog(this);
  dialog->setObjectName(QStringLiteral("testModalDialog"));
  dialog->setWindowTitle(QStringLiteral("Modal Dialog"));
  auto* layout = new QVBoxLayout(dialog);
  auto* label = new QLabel(QStringLiteral("Active modal dialog content"), dialog);
  label->setObjectName(QStringLiteral("modalLabel"));
  auto* closeBtn = new QPushButton(QStringLiteral("Close Modal"), dialog);
  closeBtn->setObjectName(QStringLiteral("modalCloseButton"));
  connect(closeBtn, &QPushButton::clicked, dialog, &QDialog::accept);
  layout->addWidget(label);
  layout->addWidget(closeBtn);
  dialog->open();
}

