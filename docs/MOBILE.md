# Mobile Support (Android and iOS)

qtPilot reaches a mobile app by a different route than a desktop one. Everything
downstream of the probe — the WebSocket protocol, the Python MCP server, the tool
surface — is identical; only the delivery of the probe changes.

## Two delivery modes

| | Desktop (Windows, Linux, macOS) | Mobile (Android, iOS) |
|---|---|---|
| Delivery | **Injected** into an already-built process | **Linked** into a development build |
| Probe artifact | Shared library (`.dll` / `.so` / `.dylib`) | Static library (`.a`) |
| Started by | Launcher + loader (`DYLD_INSERT_LIBRARIES`, `LD_PRELOAD`, Detours) | `Q_COREAPP_STARTUP_FUNCTION` in the linked probe |
| Needs app source? | No | Yes — you must add the probe to the app's link line |
| `qtPilot-launcher` | Yes | Not built; injection does not exist on mobile |

Neither mobile platform lets you insert a library into a third-party app, so the
injected route has no mobile equivalent. Build-time linking is the only way in,
which is why mobile support means *your own development build*, not any app on
the device.

## Building the probe

Configure with Qt's mobile toolchain wrapper (`qt-cmake` from the Android or iOS
Qt kit). `ANDROID` or `IOS` being set is what switches qtPilot into mobile mode.

```bash
# Android
/path/to/Qt/6.11.1/android_arm64_v8a/bin/qt-cmake -B build-android -S . \
    -DCMAKE_BUILD_TYPE=Release

# iOS (device)
/path/to/Qt/6.11.1/ios/bin/qt-cmake -B build-ios -S . -G Xcode
cmake --build build-ios --config Debug -- -sdk iphoneos
```

Mobile mode changes three things automatically, so you do not need to pass them:

- `QTPILOT_PROBE_STATIC` defaults to `ON` — the probe becomes a static library.
  A static archive avoids embedding and code-signing an extra dynamic library
  inside the app bundle, and needs no runtime search path.
- `QTPILOT_BUILD_TESTS` and `QTPILOT_BUILD_TEST_APP` are forced `OFF` — neither
  the harness app nor the host-side unit tests deploy to a device.
- `src/launcher` is not built at all. It injects into a running process, which
  mobile has no mechanism for.

The result is a static archive, versioned by Qt like every other probe build:

```
build-android/lib/libqtPilot-probe-qt6.11.a        # Release
build-ios/lib/Debug/libqtPilot-probe-qt6.11d.a     # Debug — note the `d` suffix,
                                                   # and Xcode's per-config subdir
```

## Linking it into your app

Add the probe to your app's link line behind a build flag that is **off by
default**. The probe opens an unauthenticated WebSocket server (see
[Security](#security) below), so it must not be reachable in a shipped build.

```cmake
option(MYAPP_WITH_QTPILOT "Link the qtPilot probe (development builds only)" OFF)

if(MYAPP_WITH_QTPILOT)
    find_package(qtPilot REQUIRED)      # point CMAKE_PREFIX_PATH at the install
    qtPilot_inject_probe(myapp)         # or: target_link_libraries(myapp PRIVATE qtPilot::Probe)
endif()
```

That is all that is needed. The imported target carries what a static probe
requires: whole-archive linking, the Qt components the archive references, and the
`QTPILOT_PROBE_STATIC_BUILD` definition that keeps `QTPILOT_EXPORT` from expanding
to `dllimport`.

**If you link the archive by raw path instead**, you must force-load it yourself:

```cmake
target_link_libraries(myapp PRIVATE
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,${MYAPP_QTPILOT_LIB}>")   # CMake 3.24+
```

or `-force_load <path>` on Apple platforms, or
`-Wl,--whole-archive <path> -Wl,--no-whole-archive` with the Android NDK. This is
not an optimization. A linker pulls a member out of an archive only to resolve a
symbol something references, and nothing in your app names the probe's
`Q_COREAPP_STARTUP_FUNCTION` registration -- so a plain link drops that member,
the app builds and runs cleanly, and the probe simply never starts. The one
alternative is to reference the probe explicitly, which anchors the member:

```cpp
namespace qtPilot { void ensureInitialized(); }
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    qtPilot::ensureInitialized();   // safe to call any number of times
    ...
}
```

Requires qtPilot with static probe support (PR #30 / v0.3.2 or later). Earlier
probes segfault on launch when linked -- they initialized from inside
`QCoreApplicationPrivate::init()`, before an event dispatcher existed to register
a socket with -- and could not see `Repeater` or `ListView` delegates at all.

## Connecting from the host

The probe listens on **all interfaces** port 9222 by default (override the port
with `QTPILOT_PORT`) and broadcasts UDP discovery packets, so a device on the
same network is reachable directly:

```bash
qtpilot serve --mode native --ws-url ws://<device-ip>:9222
```

Over USB, forward the port instead — more reliable on a locked-down or
Wi-Fi-less network, and it works regardless of the bind setting, because
`adb forward` and `iproxy` both terminate on the device's own loopback:

```bash
adb forward tcp:9222 tcp:9222        # Android
iproxy 9222 9222                     # iOS (usbmuxd)
qtpilot serve --mode native --ws-url ws://localhost:9222
```

If you are working exclusively over USB, `QTPILOT_BIND_ADDRESS=loopback` in the
app's launch environment keeps the device from accepting connections over the
air and from announcing itself. See
[Network exposure](GETTING-STARTED.md#network-exposure) — the probe invokes
arbitrary slots and does not authenticate clients.

`iproxy` is known to drop a device while `devicectl` still lists it; the symptom
is `Connection reset by peer` against a healthy device. Replug, or connect to
the device IP directly.

## What does not work on a device

- **No launcher, no injection, no child-process injection.** All three require
  inserting a library into a process you did not build.
- **No environment-variable control in practice.** `QTPILOT_ENABLED=0`,
  `QTPILOT_PORT` and friends are read at startup, but a device app launch gives
  you nowhere to set them. The compile-time flag on your link line is the real
  on/off switch.
- **Parentless C++ `QObject`s are not in the registry.** Context objects exposed
  to QML (`engine.rootContext()->setContextProperty(...)`) commonly have no
  parent and are not `QQuickItem`s, so they get no ID path and their invokables
  cannot be called. Reach their state through a QML item that binds to them.
- **Synthetic input does not always reach QML input handlers.** On iOS, taps
  delivered to an AR/overlay scene have been observed producing no handler
  signal, with hit testing returning an intermediate `QQuickShaderEffectSource`
  from a blur effect. Assert on what the app *transmitted* rather than on what a
  synthetic tap appeared to do.
- **A scene-graph screenshot omits native underlays.** Camera and map views
  rendered beneath the QML scene come back blank.

QML delegates are on the object tree: items created by a `Repeater` or `ListView`
delegate have a visual parent but no `QObject` parent, and the probe walks the
visual parent as a fallback so they appear with resolvable IDs. Sibling delegates
that would otherwise generate the same path get a `#N` suffix
(`.../strip/Rectangle#3`), which resolves by path like any other segment.

Naming delegates is still worth doing, for legibility rather than correctness. A
`#N` suffix is positional, so it follows the visual child order and shifts if rows
are restacked; an index-derived `objectName: "row" + index` is stable and reads
better in a tree dump. Note that a QML `id:` and a constant `objectName` are
per-declaration, not per-instance -- every row of `delegate: Rectangle { id: row }`
produces the segment `row`, and the probe disambiguates them positionally.

## Security

On a device the probe binds every interface. Anyone who can reach port 9222 can
read and write properties, invoke methods and synthesize input in your app —
there is no authentication. Keep it behind a default-off build flag, never
enable it in a build you distribute, and prefer USB port forwarding to exposing
the port on a shared network.

## Continuous integration

CI cross-compiles the probe for Android and iOS on every run (the `mobile` job),
and separately builds a static probe, links a real consumer against the installed
package, and asserts the probe actually initializes (the `static-probe` job). That
second job is the one that catches a dropped archive member -- the failure mode
where everything builds and the probe is simply absent.

Neither job runs on a device: the mobile jobs are compile-only, and on-device
behaviour is still verified by hand.

## See also

- [docs/BUILDING.md](BUILDING.md) — build options and artifact layout
- [docs/MACOS.md](MACOS.md) — the injected macOS path, code signing, notarization
- [docs/TROUBLESHOOTING.md](TROUBLESHOOTING.md) — including "linked probe never starts"
