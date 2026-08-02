// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "introspection/event_capture.h"

#include "compat/compat_gui.h"
#include "core/object_registry.h"

#include <utility>

#include <QCoreApplication>
#include <QDebug>
#include <QEvent>
#include <QFocusEvent>
#include <QGlobalStatic>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QMutexLocker>
#include <QResizeEvent>
#include <QThread>
#include <QTimer>
#include <QWidget>

#ifdef QTPILOT_HAS_QML
#include <QQuickItem>
#include <QQuickWindow>
#endif

namespace qtPilot {

Q_GLOBAL_STATIC(EventCapture, s_eventCaptureInstance)

EventCapture* EventCapture::instance() {
  EventCapture* capture = s_eventCaptureInstance();
  QCoreApplication* app = QCoreApplication::instance();
  if (app && capture->thread() != app->thread() && capture->thread() == QThread::currentThread()) {
    // Q_GLOBAL_STATIC constructs in the first caller's thread. Event filters
    // must share affinity with the watched object, so move a first-use worker
    // construction to the application thread before it can be installed.
    capture->moveToThread(app->thread());
  }
  return capture;
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
  QCoreApplication* app = QCoreApplication::instance();
  if (!app) {
    qWarning() << "[qtPilot] EventCapture: Cannot start -- no QCoreApplication";
    return;
  }

  if (QThread::currentThread() != app->thread()) {
    const bool invoked =
        QMetaObject::invokeMethod(app, [this]() { startCapture(); }, Qt::BlockingQueuedConnection);
    if (!invoked) {
      qWarning() << "[qtPilot] EventCapture: Cannot marshal start to application thread";
    }
    return;
  }

  QMutexLocker lock(&m_mutex);
  if (m_capturing) {
    return;
  }

  if (thread() != app->thread()) {
    qWarning() << "[qtPilot] EventCapture: Event filter has wrong thread affinity";
    return;
  }

  app->installEventFilter(this);
  m_capturing = true;
  qDebug() << "[qtPilot] EventCapture started";
}

void EventCapture::stopCapture() {
  QCoreApplication* app = QCoreApplication::instance();
  if (app && QThread::currentThread() != app->thread()) {
    const bool invoked =
        QMetaObject::invokeMethod(app, [this]() { stopCapture(); }, Qt::BlockingQueuedConnection);
    if (!invoked) {
      qWarning() << "[qtPilot] EventCapture: Cannot marshal stop to application thread";
    }
    return;
  }

  QMutexLocker lock(&m_mutex);
  if (!m_capturing) {
    return;
  }

  if (app) {
    app->removeEventFilter(this);
  }

  m_capturing = false;
#ifdef QTPILOT_HAS_QML
  const QList<PendingQuickWindowEvent> pending = std::move(m_pendingQuickWindowEvents);
  m_pendingQuickWindowEvents.clear();
#endif
  lock.unlock();

#ifdef QTPILOT_HAS_QML
  for (const PendingQuickWindowEvent& event : pending) {
    Q_EMIT eventCaptured(event.notification);
  }
#endif
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

  // Capture events on visual objects only. A pure Qt Quick app has no QWidget
  // anywhere, so restricting to QWidget captured nothing at all there: input is
  // delivered to the QQuickWindow and routed to QQuickItems.
#ifdef QTPILOT_HAS_QML
  bool deferQuickWindowInput = false;
  bool cancelDeferredQuickWindowInput = false;
#endif
  if (qobject_cast<QWidget*>(watched) == nullptr) {
#ifdef QTPILOT_HAS_QML
    // Qt Quick normally dispatches input first to QQuickWindow and then to the
    // target QQuickItem. Pointer handlers can grab a sequence, though, leaving
    // some events (commonly the release) on the window only. Defer window input
    // by one event-loop turn: an item duplicate cancels it, while a window-only
    // event is still reported instead of being silently lost.
    //
    // The widget path never had this problem: its window-level receiver is a
    // QWidgetWindow, which is a QWindow and so was already filtered out.
    const bool isItem = qobject_cast<QQuickItem*>(watched) != nullptr;
    const bool isQuickWindow = qobject_cast<QQuickWindow*>(watched) != nullptr;
    bool accept = isItem;
    if (isQuickWindow) {
      switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseButtonDblClick:
        case QEvent::KeyPress:
        case QEvent::KeyRelease:
          accept = true;
          deferQuickWindowInput = true;
          break;
        case QEvent::Show:
        case QEvent::Hide:
        case QEvent::Close:
        case QEvent::Resize:
          accept = true;
          break;
        default:
          break;
      }
    }
    cancelDeferredQuickWindowInput = isItem;
    if (!accept) {
      return false;
    }
#else
    return false;
#endif
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
#ifdef QTPILOT_HAS_QML
    quint64 timestamp = 0;
    switch (event->type()) {
      case QEvent::MouseButtonPress:
      case QEvent::MouseButtonRelease:
      case QEvent::MouseButtonDblClick:
      case QEvent::KeyPress:
      case QEvent::KeyRelease:
        timestamp = static_cast<QInputEvent*>(event)->timestamp();
        break;
      default:
        break;
    }
    if (deferQuickWindowInput) {
      deferQuickWindowEvent(event->type(), timestamp, notification);
      return false;
    }
    if (cancelDeferredQuickWindowInput) {
      cancelDeferredQuickWindowEvent(event->type(), timestamp);
    }
#endif
    Q_EMIT eventCaptured(notification);
  }

  // Never consume the event -- we are observe-only
  return false;
}

#ifdef QTPILOT_HAS_QML

void EventCapture::deferQuickWindowEvent(int type, quint64 timestamp,
                                         const QJsonObject& notification) {
  const quint64 token = m_nextQuickWindowEventToken++;
  m_pendingQuickWindowEvents.append(PendingQuickWindowEvent{token, type, timestamp, notification});
  QTimer::singleShot(0, this, [this, token]() { emitDeferredQuickWindowEvent(token); });
}

void EventCapture::cancelDeferredQuickWindowEvent(int type, quint64 timestamp) {
  for (auto it = m_pendingQuickWindowEvents.begin(); it != m_pendingQuickWindowEvents.end(); ++it) {
    if (it->type == type && it->timestamp == timestamp) {
      m_pendingQuickWindowEvents.erase(it);
      return;
    }
  }
}

void EventCapture::emitDeferredQuickWindowEvent(quint64 token) {
  for (auto it = m_pendingQuickWindowEvents.begin(); it != m_pendingQuickWindowEvents.end(); ++it) {
    if (it->token == token) {
      const QJsonObject notification = it->notification;
      m_pendingQuickWindowEvents.erase(it);
      Q_EMIT eventCaptured(notification);
      return;
    }
  }
}

#endif

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
