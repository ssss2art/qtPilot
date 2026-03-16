// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "childwindow.h"

#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

ChildWindow::ChildWindow(QWidget* parent) : QWidget(parent) {
  setWindowTitle(QStringLiteral("qtPilot Child Process"));
  setObjectName(QStringLiteral("ChildWindow"));
  resize(400, 200);

  auto* layout = new QVBoxLayout(this);

  auto* heading = new QLabel(QStringLiteral("Child Process Window"), this);
  heading->setObjectName(QStringLiteral("headingLabel"));
  layout->addWidget(heading);

  auto* form = new QFormLayout;

  auto* firstNameEdit = new QLineEdit(this);
  firstNameEdit->setObjectName(QStringLiteral("firstNameEdit"));
  firstNameEdit->setPlaceholderText(QStringLiteral("First name"));
  form->addRow(QStringLiteral("First Name:"), firstNameEdit);

  auto* lastNameEdit = new QLineEdit(this);
  lastNameEdit->setObjectName(QStringLiteral("lastNameEdit"));
  lastNameEdit->setPlaceholderText(QStringLiteral("Last name"));
  form->addRow(QStringLiteral("Last Name:"), lastNameEdit);

  layout->addLayout(form);
  layout->addStretch();
}
