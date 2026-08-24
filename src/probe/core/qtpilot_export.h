// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

// The single definition of QTPILOT_EXPORT.
//
// This macro used to be copy-pasted into probe.h, transport/jsonrpc_handler.h and
// transport/websocket_server.h. Three copies is one too many in the obvious way --
// teaching the macro about static builds meant the copies disagreed, and any
// translation unit including two of them failed with -Werror=macro-redefined.
//
// Three configurations, and all three are reachable:
//
//   - QTPILOT_PROBE_STATIC_BUILD: the probe is a static archive linked into an app
//     (the only delivery route on Android and iOS). Defined PUBLIC by the build, so
//     the probe AND its consumers agree. Nothing is imported or exported: the code
//     ends up inside the consumer's own binary. Without this arm a Windows static
//     build compiled and then failed to link, because the consumer arm below asks
//     the linker for __imp_-prefixed symbols that an archive of object code does
//     not contain.
//   - QTPILOT_PROBE_LIBRARY: we are building the shared probe -- export.
//   - neither: we are a consumer of the shared probe -- import.
#if defined(QTPILOT_PROBE_STATIC_BUILD)
#define QTPILOT_EXPORT
#elif defined(QTPILOT_PROBE_LIBRARY)
#if defined(_WIN32)
#define QTPILOT_EXPORT __declspec(dllexport)
#else
#define QTPILOT_EXPORT __attribute__((visibility("default")))
#endif
#else
#if defined(_WIN32)
#define QTPILOT_EXPORT __declspec(dllimport)
#else
#define QTPILOT_EXPORT
#endif
#endif
