// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "mainwindow.h"

#include <QCoreApplication>
#include <QProcess>
#include <QPushButton>

#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), ui_(new Ui::MainWindow) {
  ui_->setupUi(this);

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
