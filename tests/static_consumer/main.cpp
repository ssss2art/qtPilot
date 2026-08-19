// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT
//
// Consumer fixture for the STATIC probe. Runs an event loop for one beat and exits.
//
// It must NOT reference any qtPilot symbol. That is the entire point: the probe
// starts from a file-scope Q_COREAPP_STARTUP_FUNCTION registration, and a linker
// pulls a member out of an archive only to resolve a symbol something actually
// references. If this file mentioned the probe, the archive member would be pulled
// in for that reason and the test could never detect the drop it exists to catch.
//
// Success is therefore asserted from OUTSIDE, on the probe's own stderr banner --
// see the static-probe job in .github/workflows/ci.yml.

#include <QCoreApplication>
#include <QTimer>

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  // Probe initialization is queued onto this thread's event queue, so it needs at
  // least one turn of the loop to run.
  QTimer::singleShot(1500, &app, [&app]() { app.quit(); });
  return app.exec();
}
