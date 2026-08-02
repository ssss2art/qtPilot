// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT
//
// A pure Qt Quick app (QGuiApplication + QQmlApplicationEngine, no QtWidgets)
// whose views are backed by real QAbstractItemModel instances. Exists so the
// qt.models.* probe surface can be exercised against QML -- the widget test app
// cannot cover it, and a QML app built on ListElement/JS arrays has no Qt model
// for the probe to find.
//
// See docs/qml-a11y-evaluation.md.

#include "models.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

int main(int argc, char* argv[]) {
  QGuiApplication app(argc, argv);
  app.setApplicationName(QStringLiteral("qtPilot QML Model Test App"));

  // objectName is what makes each model addressable by a stable id through
  // qt.objects.search / qt.models.list, rather than a synthesised path.
  TaskListModel taskModel;
  taskModel.setObjectName(QStringLiteral("taskModel"));

  FileTreeModel treeModel;
  treeModel.setObjectName(QStringLiteral("treeModel"));

  LazyLogModel lazyModel;
  lazyModel.setObjectName(QStringLiteral("lazyModel"));

  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("taskModel"), &taskModel);
  engine.rootContext()->setContextProperty(QStringLiteral("treeModel"), &treeModel);
  engine.rootContext()->setContextProperty(QStringLiteral("lazyModel"), &lazyModel);

  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
      []() { QCoreApplication::exit(1); }, Qt::QueuedConnection);

  engine.loadFromModule("qtPilotQmlTestApp", "Main");
  if (engine.rootObjects().isEmpty()) {
    return 1;
  }

  return app.exec();
}
