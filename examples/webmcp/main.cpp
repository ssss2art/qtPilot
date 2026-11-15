// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStringListModel>

int main(int argc, char* argv[]) {
  QGuiApplication app(argc, argv);
  QStringListModel model({QStringLiteral("First item"), QStringLiteral("Second item")}, &app);
  model.setObjectName(QStringLiteral("demoModel"));
  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("demoModel"), &model);
  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
      []() { QCoreApplication::exit(1); }, Qt::QueuedConnection);
  engine.loadFromModule("qtPilotWebMcpExample", "Main");
  return app.exec();
}
