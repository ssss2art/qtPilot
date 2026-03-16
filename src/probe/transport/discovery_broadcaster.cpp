// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "transport/discovery_broadcaster.h"

#include <QCoreApplication>
#include <QHostInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QUdpSocket>

namespace qtPilot {

static constexpr quint16 kDefaultDiscoveryPort = 9221;
static constexpr int kBroadcastIntervalMs = 5000;

DiscoveryBroadcaster::DiscoveryBroadcaster(quint16 wsPort, const QString& mode, QObject* parent)
    : QObject(parent),
      socket_(nullptr),
      timer_(nullptr),
      discoveryPort_(kDefaultDiscoveryPort),
      wsPort_(wsPort),
      mode_(mode),
      running_(false) {
  // Read discovery port from environment
  QByteArray portEnv = qgetenv("QTPILOT_DISCOVERY_PORT");
  if (!portEnv.isEmpty()) {
    bool ok = false;
    int envPort = portEnv.toInt(&ok);
    if (ok && envPort > 0 && envPort <= 65535) {
      discoveryPort_ = static_cast<quint16>(envPort);
    }
  }
}

DiscoveryBroadcaster::~DiscoveryBroadcaster() {
  stop();
}

bool DiscoveryBroadcaster::start() {
  if (running_) {
    return true;
  }

  socket_ = new QUdpSocket(this);

  timer_ = new QTimer(this);
  timer_->setInterval(kBroadcastIntervalMs);
  connect(timer_, &QTimer::timeout, this, &DiscoveryBroadcaster::sendAnnounce);

  uptime_.start();
  running_ = true;

  // Send initial announce immediately
  sendAnnounce();
  timer_->start();

  fprintf(stderr, "[qtPilot] Discovery broadcaster started on UDP port %u\n",
          static_cast<unsigned>(discoveryPort_));
  return true;
}

void DiscoveryBroadcaster::stop() {
  if (!running_) {
    return;
  }

  running_ = false;

  if (timer_) {
    timer_->stop();
  }

  // Send goodbye before closing
  sendGoodbye();

  if (socket_) {
    socket_->close();
  }

  delete timer_;
  timer_ = nullptr;
  delete socket_;
  socket_ = nullptr;

  fprintf(stderr, "[qtPilot] Discovery broadcaster stopped\n");
}

bool DiscoveryBroadcaster::isRunning() const {
  return running_;
}

void DiscoveryBroadcaster::sendAnnounce() {
  if (!socket_ || !running_) {
    return;
  }
  QByteArray payload = buildAnnouncePayload();
  socket_->writeDatagram(payload, QHostAddress::Broadcast, discoveryPort_);
}

void DiscoveryBroadcaster::sendGoodbye() {
  if (!socket_) {
    return;
  }
  QByteArray payload = buildGoodbyePayload();
  socket_->writeDatagram(payload, QHostAddress::Broadcast, discoveryPort_);
}

QByteArray DiscoveryBroadcaster::buildAnnouncePayload() const {
  QJsonObject obj;
  obj["type"] = QStringLiteral("announce");
  obj["protocol"] = QStringLiteral("qtPilot-discovery");
  obj["version"] = 1;
  obj["appName"] = QCoreApplication::applicationName();
  obj["pid"] = static_cast<qint64>(QCoreApplication::applicationPid());
  obj["qtVersion"] = QString::fromLatin1(qVersion());
  obj["wsPort"] = static_cast<int>(wsPort_);
  obj["hostname"] = QHostInfo::localHostName();
  obj["mode"] = mode_;
  obj["uptime"] = uptime_.elapsed() / 1000.0;
  return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

QByteArray DiscoveryBroadcaster::buildGoodbyePayload() const {
  QJsonObject obj;
  obj["type"] = QStringLiteral("goodbye");
  obj["protocol"] = QStringLiteral("qtPilot-discovery");
  obj["version"] = 1;
  obj["pid"] = static_cast<qint64>(QCoreApplication::applicationPid());
  obj["wsPort"] = static_cast<int>(wsPort_);
  obj["hostname"] = QHostInfo::localHostName();
  return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

}  // namespace qtPilot
