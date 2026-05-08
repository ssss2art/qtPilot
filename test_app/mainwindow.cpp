// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "mainwindow.h"

#include <QCoreApplication>
#include <QProcess>
#include <QPushButton>
#include <QStandardItem>

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

  QStandardItem* etc = addTopRow("ETC", "Manufacturer", "");
  QStandardItem* fos4 = addRow(etc, "fos4 Fresnel Lustr X8 direct", "Fixture", "0");
  addRow(fos4, "Mode 8ch", "Mode", "0");
  addRow(fos4, "Mode 12ch", "Mode", "0");
  addRow(etc, "ColorSource PAR", "Fixture", "0");

  QStandardItem* martin = addTopRow("Martin", "Manufacturer", "");
  QStandardItem* aura = addRow(martin, "MAC Aura XB", "Fixture", "0");
  addRow(aura, "Mode 8ch", "Mode", "0");
  addRow(aura, "Mode 12ch", "Mode", "0");

  // Synthetic 1200-row child set under a dedicated parent to exercise pagination.
  QStandardItem* bulk = addTopRow("BulkManufacturer", "Manufacturer", "");
  for (int i = 0; i < 1200; ++i) {
    addRow(bulk, QStringLiteral("Fixture %1").arg(i, 4, 10, QChar('0')),
           "Fixture", QString::number(i));
  }

  ui_->treeView->setModel(treeModel_);
  ui_->treeView->setObjectName(QStringLiteral("treeView"));

  // Set explicit object names for form fields
  ui_->nameEdit->setObjectName(QStringLiteral("nameEdit"));
  ui_->emailEdit->setObjectName(QStringLiteral("emailEdit"));
  ui_->messageEdit->setObjectName(QStringLiteral("messageEdit"));

  // Connect signals
  connect(ui_->submitButton, &QPushButton::clicked, this, &MainWindow::OnSubmitClicked);
  connect(ui_->clearButton, &QPushButton::clicked, this, &MainWindow::OnClearClicked);
  connect(ui_->slider, &QSlider::valueChanged, this, &MainWindow::OnSliderChanged);

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
