// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "introspection/event_capture.h"

#include "compat/compat_gui.h"
#include "core/object_registry.h"

#include <QCoreApplication>
#include <QDebug>
#include <QEvent>
#include <QFocusEvent>
#include <QGlobalStatic>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QMutexLocker>
#include <QResizeEvent>
#include <QWidget>

namespace qtPilot {

Q_GLOBAL_STATIC(EventCapture, s_eventCaptureInstance)

EventCapture* EventCapture::instance() {
  return s_eventCaptureInstance();
}

EventCapture::EventCapture() : QObject(nullptr) {
  // Populate the set of event types we care about
  m_capturedTypes.insert(QEvent::MouseButtonPress);
  m_capturedTypes.insert(QEvent::MouseButtonRelease);
  m_capturedTypes.insert(QEvent::MouseButtonDblClick);
  m_capturedTypes.insert(QEvent::KeyPress);
  m_capturedTypes.insert(QEvent::KeyRelease);
  m_capturedTypes.insert(QEvent::FocusIn);
  m_capturedTypes.insert(QEvent::FocusOut);
  // Window lifecycle
  m_capturedTypes.insert(QEvent::Show);
  m_capturedTypes.insert(QEvent::Hide);
  m_capturedTypes.insert(QEvent::Close);
  m_capturedTypes.insert(QEvent::Resize);

  qDebug() << "[qtPilot] EventCapture created";
}

EventCapture::~EventCapture() {
  stopCapture();
  qDebug() << "[qtPilot] EventCapture destroyed";
}

void EventCapture::startCapture() {
  QMutexLocker lock(&m_mutex);
  if (m_capturing) {
    return;
  }

  if (!QCoreApplication::instance()) {
    qWarning() << "[qtPilot] EventCapture: Cannot start -- no QCoreApplication";
    return;
  }

  QCoreApplication::instance()->installEventFilter(this);
  m_capturing = true;
  qDebug() << "[qtPilot] EventCapture started";
}

void EventCapture::stopCapture() {
  QMutexLocker lock(&m_mutex);
  if (!m_capturing) {
    return;
  }

  if (QCoreApplication::instance()) {
    QCoreApplication::instance()->removeEventFilter(this);
  }

  m_capturing = false;
  qDebug() << "[qtPilot] EventCapture stopped";
}

bool EventCapture::isCapturing() const {
  QMutexLocker lock(&m_mutex);
  return m_capturing;
}

bool EventCapture::eventFilter(QObject* watched, QEvent* event) {
  // Quick reject: not capturing or event type not in our set
  if (!m_capturing || !m_capturedTypes.contains(event->type())) {
    return false;
  }

  // Only capture events on QWidget-derived objects
  QWidget* widget = qobject_cast<QWidget*>(watched);
  if (!widget) {
    return false;
  }

  QJsonObject notification;

  switch (event->type()) {
    case QEvent::MouseButtonPress:
      notification = buildMouseNotification(watched, event, QStringLiteral("MouseButtonPress"));
      break;
    case QEvent::MouseButtonRelease:
      notification = buildMouseNotification(watched, event, QStringLiteral("MouseButtonRelease"));
      break;
    case QEvent::MouseButtonDblClick:
      notification = buildMouseNotification(watched, event, QStringLiteral("MouseButtonDblClick"));
      break;
    case QEvent::KeyPress:
      notification = buildKeyNotification(watched, event, QStringLiteral("KeyPress"));
      break;
    case QEvent::KeyRelease:
      notification = buildKeyNotification(watched, event, QStringLiteral("KeyRelease"));
      break;
    case QEvent::FocusIn:
      notification = buildFocusNotification(watched, event, QStringLiteral("FocusIn"));
      break;
    case QEvent::FocusOut:
      notification = buildFocusNotification(watched, event, QStringLiteral("FocusOut"));
      break;
    case QEvent::Show:
      notification = buildWindowNotification(watched, event, QStringLiteral("Show"));
      break;
    case QEvent::Hide:
      notification = buildWindowNotification(watched, event, QStringLiteral("Hide"));
      break;
    case QEvent::Close:
      notification = buildWindowNotification(watched, event, QStringLiteral("Close"));
      break;
    case QEvent::Resize:
      notification = buildWindowNotification(watched, event, QStringLiteral("Resize"));
      break;
    default:
      return false;
  }

  if (!notification.isEmpty()) {
    Q_EMIT eventCaptured(notification);
  }

  // Never consume the event -- we are observe-only
  return false;
}

static QString mouseButtonName(Qt::MouseButton button) {
  switch (button) {
    case Qt::LeftButton:
      return QStringLiteral("left");
    case Qt::RightButton:
      return QStringLiteral("right");
    case Qt::MiddleButton:
      return QStringLiteral("middle");
    default:
      return QStringLiteral("other");
  }
}

static QString modifiersToString(Qt::KeyboardModifiers mods) {
  QStringList parts;
  if (mods & Qt::ShiftModifier)
    parts.append(QStringLiteral("Shift"));
  if (mods & Qt::ControlModifier)
    parts.append(QStringLiteral("Ctrl"));
  if (mods & Qt::AltModifier)
    parts.append(QStringLiteral("Alt"));
  if (mods & Qt::MetaModifier)
    parts.append(QStringLiteral("Meta"));
  return parts.join(QStringLiteral("+"));
}

static QString focusReasonName(Qt::FocusReason reason) {
  switch (reason) {
    case Qt::MouseFocusReason:
      return QStringLiteral("mouse");
    case Qt::TabFocusReason:
      return QStringLiteral("tab");
    case Qt::BacktabFocusReason:
      return QStringLiteral("backtab");
    case Qt::ActiveWindowFocusReason:
      return QStringLiteral("activeWindow");
    case Qt::PopupFocusReason:
      return QStringLiteral("popup");
    case Qt::ShortcutFocusReason:
      return QStringLiteral("shortcut");
    case Qt::MenuBarFocusReason:
      return QStringLiteral("menuBar");
    case Qt::OtherFocusReason:
    default:
      return QStringLiteral("other");
  }
}

QJsonObject EventCapture::buildMouseNotification(QObject* widget, QEvent* event,
                                                 const QString& typeName) {
  auto* me = static_cast<QMouseEvent*>(event);

  QJsonObject notification;
  notification[QStringLiteral("type")] = typeName;
  notification[QStringLiteral("objectId")] = ObjectRegistry::instance()->objectId(widget);
  notification[QStringLiteral("objectName")] = widget->objectName();
  notification[QStringLiteral("className")] =
      QString::fromLatin1(widget->metaObject()->className());
  notification[QStringLiteral("button")] = mouseButtonName(me->button());

  QJsonObject pos;
  QPoint localPoint = compat::mousePos(me);
  pos[QStringLiteral("x")] = localPoint.x();
  pos[QStringLiteral("y")] = localPoint.y();
  notification[QStringLiteral("pos")] = pos;

  QJsonObject globalPos;
  QPoint globalPoint = compat::mouseGlobalPos(me);
  globalPos[QStringLiteral("x")] = globalPoint.x();
  globalPos[QStringLiteral("y")] = globalPoint.y();
  notification[QStringLiteral("globalPos")] = globalPos;

  return notification;
}

QJsonObject EventCapture::buildKeyNotification(QObject* widget, QEvent* event,
                                               const QString& typeName) {
  auto* ke = static_cast<QKeyEvent*>(event);

  QJsonObject notification;
  notification[QStringLiteral("type")] = typeName;
  notification[QStringLiteral("objectId")] = ObjectRegistry::instance()->objectId(widget);
  notification[QStringLiteral("objectName")] = widget->objectName();
  notification[QStringLiteral("className")] =
      QString::fromLatin1(widget->metaObject()->className());
  notification[QStringLiteral("key")] = ke->key();
  notification[QStringLiteral("text")] = ke->text();
  notification[QStringLiteral("modifiers")] = modifiersToString(ke->modifiers());

  return notification;
}

QJsonObject EventCapture::buildFocusNotification(QObject* widget, QEvent* event,
                                                 const QString& typeName) {
  auto* fe = static_cast<QFocusEvent*>(event);

  QJsonObject notification;
  notification[QStringLiteral("type")] = typeName;
  notification[QStringLiteral("objectId")] = ObjectRegistry::instance()->objectId(widget);
  notification[QStringLiteral("objectName")] = widget->objectName();
  notification[QStringLiteral("className")] =
      QString::fromLatin1(widget->metaObject()->className());
  notification[QStringLiteral("reason")] = focusReasonName(fe->reason());

  return notification;
}

QJsonObject EventCapture::buildWindowNotification(QObject* widget, QEvent* event,
                                                  const QString& typeName) {
  QJsonObject notification;
  notification[QStringLiteral("type")] = typeName;
  notification[QStringLiteral("objectId")] = ObjectRegistry::instance()->objectId(widget);
  notification[QStringLiteral("objectName")] = widget->objectName();
  notification[QStringLiteral("className")] =
      QString::fromLatin1(widget->metaObject()->className());

  if (event->type() == QEvent::Resize) {
    auto* re = static_cast<QResizeEvent*>(event);
    QJsonObject size;
    size[QStringLiteral("w")] = re->size().width();
    size[QStringLiteral("h")] = re->size().height();
    notification[QStringLiteral("size")] = size;
  }

  return notification;
}

}  // namespace qtPilot
