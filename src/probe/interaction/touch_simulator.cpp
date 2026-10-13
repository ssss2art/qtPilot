// Copyright (c) 2026 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "interaction/touch_simulator.h"

#include <QMap>
#include <QMetaObject>
#include <QPointer>
#include <QWindow>

#ifdef QTPILOT_HAS_TOUCH
#include <QEventPoint>
#include <QPointingDevice>
#include <QtGui/qtestsupport_gui.h>
#endif

namespace qtPilot {

namespace {

/// The fingers that are down, by id, in window coordinates.
struct HeldTouch {
  QPointer<QWindow> window;
  QMap<int, QPointF> fingers;
};

HeldTouch& held() {
  static HeldTouch s_held;
  // A window destroyed with fingers on it takes them with it.
  if (!s_held.window) {
    s_held.fingers.clear();
  }
  return s_held;
}

#ifdef QTPILOT_HAS_TOUCH
/// The touchscreen every frame comes from, registered once with the window system.
const QPointingDevice* touchscreen() {
  static QPointingDevice* s_device = QTest::createTouchDevice(QInputDevice::DeviceType::TouchScreen,
                                                              QInputDevice::Capability::Position);
  return s_device;
}

QEventPoint::State toEventPointState(const QString& state) {
  if (state == QStringLiteral("pressed")) {
    return QEventPoint::State::Pressed;
  }
  if (state == QStringLiteral("moved")) {
    return QEventPoint::State::Updated;
  }
  if (state == QStringLiteral("released")) {
    return QEventPoint::State::Released;
  }
  return QEventPoint::State::Stationary;
}
#endif

}  // namespace

bool TouchSimulator::isHeld() {
  return !held().fingers.isEmpty();
}

QWindow* TouchSimulator::heldWindow() {
  return isHeld() ? held().window.data() : nullptr;
}

QList<TouchReport> TouchSimulator::frame(QWindow* window, const QList<TouchFinger>& fingers) {
  HeldTouch& state = held();
  QMap<int, QPointF> next;
  for (const TouchFinger& finger : fingers) {
    next.insert(finger.id, finger.windowPosition);
  }

  QMap<int, TouchReport> reports;
  for (auto it = next.cbegin(); it != next.cend(); ++it) {
    const auto previous = state.fingers.constFind(it.key());
    QString what = QStringLiteral("pressed");
    if (previous != state.fingers.cend()) {
      what = *previous == it.value() ? QStringLiteral("stationary") : QStringLiteral("moved");
    }
    reports.insert(it.key(),
                   TouchReport{.id = it.key(), .state = what, .windowPosition = it.value()});
  }
  for (auto it = state.fingers.cbegin(); it != state.fingers.cend(); ++it) {
    if (!next.contains(it.key())) {
      // A lifted finger leaves the screen where it last was.
      reports.insert(it.key(), TouchReport{.id = it.key(),
                                           .state = QStringLiteral("released"),
                                           .windowPosition = it.value()});
    }
  }

  state.fingers = next;
  state.window = next.isEmpty() ? nullptr : window;

#ifdef QTPILOT_HAS_TOUCH
  QList<QEventPoint> points;
  points.reserve(reports.size());
  for (const TouchReport& report : reports) {
    points.append(QEventPoint(report.id, toEventPointState(report.state), report.windowPosition,
                              window->mapToGlobal(report.windowPosition)));
  }
  const QPointingDevice* device = touchscreen();
  QPointer<QWindow> target(window);
  QMetaObject::invokeMethod(
      window,
      [target, device, points]() {
        if (target) {
          qt_handleTouchEvent(target, device, points);
        }
      },
      Qt::QueuedConnection);
#endif

  return reports.values();
}

}  // namespace qtPilot
