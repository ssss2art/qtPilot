// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <QObject>
#include <QThread>

namespace qtPilot::test {

/// @brief Keeps an object alive on a worker thread's event loop.
///
/// The object is built on the calling thread and then handed to the worker, so
/// its thread affinity is the worker's -- which is what the probe sees for any
/// object an application creates or moves off the GUI thread. The worker idles
/// until destruction, so a test may look at the object without racing it.
template <typename T = QObject>
class ParkedOnWorker {
 public:
  explicit ParkedOnWorker(const QString& objectName) : m_object(new T()) {
    m_object->setObjectName(objectName);
    m_thread.start();
    m_object->moveToThread(&m_thread);
  }

  ~ParkedOnWorker() {
    // A finishing QThread delivers its pending deferred deletes.
    m_object->deleteLater();
    m_thread.quit();
    m_thread.wait();
  }

  ParkedOnWorker(const ParkedOnWorker&) = delete;
  ParkedOnWorker& operator=(const ParkedOnWorker&) = delete;

  T* object() const { return m_object; }

 private:
  QThread m_thread;
  T* m_object;
};

}  // namespace qtPilot::test
