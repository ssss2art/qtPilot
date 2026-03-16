// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "introspection/qml_inspector.h"

#ifdef QTPILOT_HAS_QML

#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>

namespace qtPilot {

QString stripQmlPrefix(const QString& className) {
  if (className.startsWith(QLatin1String("QQuick"))) {
    return className.mid(6);
  }
  return className;
}

bool isQmlItem(QObject* obj) {
  return qobject_cast<QQuickItem*>(obj) != nullptr;
}

QmlItemInfo inspectQmlItem(QObject* obj) {
  QmlItemInfo info;

  if (!obj) {
    return info;
  }

  auto* quickItem = qobject_cast<QQuickItem*>(obj);
  if (!quickItem) {
    return info;
  }

  info.isQmlItem = true;
  info.shortTypeName = stripQmlPrefix(QString::fromLatin1(obj->metaObject()->className()));

  QQmlContext* context = QQmlEngine::contextForObject(obj);
  if (context) {
    info.qmlId = context->nameForObject(obj);

    QUrl baseUrl = context->baseUrl();
    if (baseUrl.isValid()) {
      info.qmlFile = baseUrl.toString();
    }
  }

  return info;
}

}  // namespace qtPilot

#else  // !QTPILOT_HAS_QML

// Stub implementations are inline in the header.
// This translation unit exists so the build system always has a .cpp to compile.

#endif  // QTPILOT_HAS_QML
