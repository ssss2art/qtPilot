// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "transport/js_bridge.h"

#include "transport/jsonrpc_handler.h"

#include <emscripten.h>
#include <emscripten/bind.h>
#include <string>

namespace qtPilot {

namespace {

JsBridge* s_current = nullptr;

/// The newest notifications the page keeps when nobody reads them.
constexpr int kNotificationBound = 1000;

std::string call(const std::string& request) {
  if (s_current == nullptr) {
    return R"({"jsonrpc":"2.0","id":null,"error":{"code":-32000,"message":"qtPilot probe is not running"}})";
  }
  return s_current->rpcHandler()->HandleMessage(QString::fromStdString(request)).toStdString();
}

}  // namespace

// clang-format off
EM_JS(void, qtpilot_publish, (), {
  const bridge = globalThis.qtpilot || {};
  bridge.call = request => Module.qtpilotCall(typeof request === 'string' ? request : JSON.stringify(request));
  bridge.notifications = bridge.notifications || [];
  globalThis.qtpilot = bridge;
});

EM_JS(void, qtpilot_notify, (const char* message, int bound), {
  const bridge = globalThis.qtpilot;
  if (!bridge) return;
  const text = UTF8ToString(message);
  bridge.notifications.push(text);
  if (bridge.notifications.length > bound) bridge.notifications.splice(0, bridge.notifications.length - bound);
  if (typeof bridge.onnotification === 'function') {
    try { bridge.onnotification(text); } catch (error) { console.warn('qtpilot.onnotification:', error); }
  }
});

EM_JS(void, qtpilot_unpublish, (), {
  if (globalThis.qtpilot) delete globalThis.qtpilot.call;
});
// clang-format on

EMSCRIPTEN_BINDINGS(qtpilot_js_bridge) {
  emscripten::function("qtpilotCall", &call);
}

JsBridge::JsBridge(QObject* parent) : QObject(parent), m_handler(new JsonRpcHandler(this)) {
  connect(m_handler, &JsonRpcHandler::NotificationReady, this, &JsBridge::sendNotification);
}

JsBridge::~JsBridge() {
  if (s_current == this) {
    qtpilot_unpublish();
    s_current = nullptr;
  }
}

bool JsBridge::install() {
  if (s_current != nullptr && s_current != this) {
    return false;
  }
  s_current = this;
  qtpilot_publish();
  return true;
}

JsonRpcHandler* JsBridge::rpcHandler() const {
  return m_handler;
}

void JsBridge::sendNotification(const QString& message) {
  const QByteArray utf8 = message.toUtf8();
  qtpilot_notify(utf8.constData(), kNotificationBound);
}

JsBridge* JsBridge::current() {
  return s_current;
}

}  // namespace qtPilot
