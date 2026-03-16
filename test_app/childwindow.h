// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <QLineEdit>
#include <QWidget>

/// @brief Simple child window used to test child-process probe injection.
///
/// Launched when the test app is started with --child.  Contains a couple of
/// text boxes so the probe has something to introspect.
class ChildWindow : public QWidget {
  Q_OBJECT

 public:
  explicit ChildWindow(QWidget* parent = nullptr);
};
