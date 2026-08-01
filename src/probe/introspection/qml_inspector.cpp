// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "introspection/qml_inspector.h"

#ifdef QTPILOT_HAS_QML

#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>

namespace qtPilot {

namespace {

/// @brief RAII guard: sets a bool flag for its lifetime and clears it on scope
/// exit, including when an exception propagates out of the guarded call.
struct ScopedFlag {
  bool& flag;
  explicit ScopedFlag(bool& f) : flag(f) { flag = true; }
  ~ScopedFlag() { flag = false; }
  ScopedFlag(const ScopedFlag&) = delete;
  ScopedFlag& operator=(const ScopedFlag&) = delete;
};

}  // namespace

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

  // Re-entrancy guard. This runs from the AddQObject hook, during QObject
  // construction. QQmlContext::nameForObject() resolves the object's `id:` by
  // reading properties on the context's objects, and reading a lazy/computed
  // property can CONSTRUCT a QObject (e.g. QQuickTextArea::cursorSelection builds
  // a QQuickTextSelection). That construction fires the AddQObject hook again,
  // which calls back into here — recursing until the stack overflows. If we're
  // already inside a nameForObject() lookup, skip it: the object still registers
  // with its type name, and the deferred refreshObjectId() (queued after
  // construction settles) fills in the qmlId without recursing.
  //
  // thread_local, not a global flag: the recursion is strictly synchronous on
  // one thread, so guarding per-thread avoids needlessly skipping lookups for
  // QML objects legitimately built on another thread.
  static thread_local bool inNameLookup = false;
  if (inNameLookup) {
    return info;
  }

  QQmlContext* context = QQmlEngine::contextForObject(obj);
  if (context) {
    {
      // RAII so the flag is cleared even if nameForObject() throws.
      ScopedFlag guard(inNameLookup);
      info.qmlId = context->nameForObject(obj);
    }

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
