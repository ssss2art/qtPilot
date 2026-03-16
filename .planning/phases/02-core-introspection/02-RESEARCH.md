# Phase 2: Core Introspection - Research

**Researched:** 2026-01-30
**Domain:** Qt Meta-Object System introspection, signal monitoring, and UI interaction
**Confidence:** HIGH

## Summary

Phase 2 implements the core introspection capabilities of the qtPilot probe. This involves three interconnected subsystems: (1) an Object Registry that tracks all QObjects via Qt's private hook mechanism, (2) a Meta Inspector that uses QMetaObject to expose properties, methods, and signals, and (3) a UI Interaction layer that leverages QTest functions for input simulation and QWidget::grab() for screenshots.

The Qt Meta-Object system provides comprehensive introspection via `QMetaObject`, `QMetaProperty`, and `QMetaMethod` classes. These are public, stable APIs that allow enumeration and access to properties, methods (including signals and slots), and dynamic method invocation. Object lifecycle tracking uses Qt's private `qtHookData` hooks (AddQObject/RemoveQObject), which are the same mechanism used by KDAB's GammaRay.

Signal monitoring can be implemented either via the private `qt_register_signal_spy_callbacks` API (low-level, receives all signals) or by dynamically connecting to specific signals via `QMetaObject::connect`. The private API is more powerful but undocumented; dynamic connections are public API but require per-object setup.

**Primary recommendation:** Implement Object Registry using qtHookData hooks with mutex protection, Meta Inspector using public QMetaObject APIs, and UI interaction using QTest module functions. Reserve private signal spy callbacks for Phase 2 "push notifications" feature.

## Standard Stack

### Core (Qt Built-in)

| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| QMetaObject | Qt 5.15+/6.x | Introspection API | Core Qt API, stable across versions |
| QMetaProperty | Qt 5.15+/6.x | Property access | Public API for property read/write |
| QMetaMethod | Qt 5.15+/6.x | Method/signal/slot info | Public API for method invocation |
| QTest | Qt 5.15+/6.x | UI input simulation | Built-in mouse/keyboard simulation |
| QWidget::grab() | Qt 5.15+/6.x | Screenshot capture | Public API, works for widget subtrees |
| QScreen::grabWindow() | Qt 5.15+/6.x | Window/screen capture | Public API, captures window frames |

### Private APIs Required

| Header | Version | Purpose | Risk Level |
|--------|---------|---------|------------|
| `<private/qhooks_p.h>` | Qt 5.x/6.x | Object lifecycle hooks | MEDIUM - stable but undocumented |
| `<private/qobject_p.h>` | Qt 5.x/6.x | Signal spy callbacks | MEDIUM - stable but undocumented |

### Supporting

| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| QPointer | Qt 5.15+/6.x | Safe object references | Always when storing QObject* |
| QMutex/QRecursiveMutex | Qt 5.15+/6.x | Thread-safe registry | Protecting shared data structures |
| QVariant | Qt 5.15+/6.x | Type-erased values | Property values, method arguments |

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| qtHookData | QEvent filter | Misses objects without event loop participation |
| Private signal spy | QMetaObject::connect | Must explicitly connect per-object/signal |
| QTest functions | QApplication::postEvent | QTest is higher-level, handles timing |
| QWidget::grab() | QScreen::grabWindow() | grab() is widget-local; grabWindow() captures window frame |

**CMake:**
```cmake
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Test)
target_link_libraries(qtPilot-probe PRIVATE
    Qt6::Core
    Qt6::CorePrivate  # For qhooks_p.h
    Qt6::Gui
    Qt6::Widgets
    Qt6::Test
)
```

Note: `Qt6::CorePrivate` is required to access `<private/qhooks_p.h>`.

## Architecture Patterns

### Recommended Project Structure

```
src/
└── probe/
    ├── core/
    │   ├── probe.h/.cpp           # Existing - singleton orchestrator
    │   ├── probe_init_*.cpp       # Existing - platform entry points
    │   └── object_registry.h/.cpp # NEW - Object lifecycle tracking
    ├── introspection/
    │   ├── meta_inspector.h/.cpp  # NEW - QMetaObject wrapper
    │   ├── object_id.h/.cpp       # NEW - Hierarchical ID generation
    │   └── signal_monitor.h/.cpp  # NEW - Signal subscription system
    ├── interaction/
    │   ├── input_simulator.h/.cpp # NEW - Mouse/keyboard simulation
    │   ├── screenshot.h/.cpp      # NEW - Widget/window capture
    │   └── hit_test.h/.cpp        # NEW - Coordinate-to-widget mapping
    └── transport/
        ├── websocket_server.h/.cpp    # Existing
        └── jsonrpc_handler.h/.cpp     # Existing - add new methods
```

### Pattern 1: Object Registry with Hook Chaining

**What:** Track all QObjects via qtHookData hooks, preserving existing hooks for tool coexistence.

**When to use:** Always - this is the foundation for all introspection.

**Why:** qtHookData is called for EVERY QObject creation/destruction, ensuring complete coverage.

**Example:**
```cpp
// object_registry.h
#include <QObject>
#include <QHash>
#include <QPointer>
#include <QMutex>
#include <private/qhooks_p.h>

namespace qtpilot {

class ObjectRegistry : public QObject {
    Q_OBJECT
public:
    static ObjectRegistry* instance();

    // Lookup methods
    QObject* findById(const QString& id);
    QObject* findByObjectName(const QString& name);
    QList<QObject*> findAllByClassName(const QString& className);
    QList<QObject*> allObjects();

    // ID generation
    QString objectId(QObject* obj);

signals:
    void objectAdded(QObject* obj);
    void objectRemoved(QObject* obj);

private:
    friend void qtpilotAddObjectHook(QObject*);
    friend void qtpilotRemoveObjectHook(QObject*);

    void registerObject(QObject* obj);
    void unregisterObject(QObject* obj);

    QHash<QObject*, QString> m_objectToId;
    QHash<QString, QPointer<QObject>> m_idToObject;
    mutable QRecursiveMutex m_mutex;  // Must be recursive for nested operations
};

// Hook installation - called once at probe startup
void installObjectHooks();
void uninstallObjectHooks();

}  // namespace qtpilot
```

```cpp
// object_registry.cpp
#include "object_registry.h"

namespace {
    // Store previous callbacks for daisy-chaining
    QHooks::AddQObjectCallback g_previousAddCallback = nullptr;
    QHooks::RemoveQObjectCallback g_previousRemoveCallback = nullptr;
}

extern "C" Q_DECL_EXPORT void qtpilotAddObjectHook(QObject* obj) {
    qtpilot::ObjectRegistry::instance()->registerObject(obj);

    // Chain to previous callback (e.g., GammaRay)
    if (g_previousAddCallback)
        g_previousAddCallback(obj);
}

extern "C" Q_DECL_EXPORT void qtpilotRemoveObjectHook(QObject* obj) {
    qtpilot::ObjectRegistry::instance()->unregisterObject(obj);

    if (g_previousRemoveCallback)
        g_previousRemoveCallback(obj);
}

void qtpilot::installObjectHooks() {
    // Verify hook version compatibility
    Q_ASSERT(qtHookData[QHooks::HookDataVersion] >= 1);

    // Save existing callbacks
    g_previousAddCallback = reinterpret_cast<QHooks::AddQObjectCallback>(
        qtHookData[QHooks::AddQObject]);
    g_previousRemoveCallback = reinterpret_cast<QHooks::RemoveQObjectCallback>(
        qtHookData[QHooks::RemoveQObject]);

    // Install our callbacks
    qtHookData[QHooks::AddQObject] = reinterpret_cast<quintptr>(&qtpilotAddObjectHook);
    qtHookData[QHooks::RemoveQObject] = reinterpret_cast<quintptr>(&qtpilotRemoveObjectHook);
}

void qtpilot::ObjectRegistry::registerObject(QObject* obj) {
    QMutexLocker lock(&m_mutex);

    QString id = generateId(obj);
    m_objectToId.insert(obj, id);
    m_idToObject.insert(id, obj);

    // Emit on main thread
    QMetaObject::invokeMethod(this, [this, obj]() {
        emit objectAdded(obj);
    }, Qt::QueuedConnection);
}
```

### Pattern 2: Hierarchical Object ID Generation

**What:** Generate stable, human-readable IDs based on object hierarchy per CONTEXT.md decisions.

**When to use:** When assigning IDs to tracked objects.

**Why:** IDs must be stable (when possible), debuggable, and reflect Qt's object tree.

**Example:**
```cpp
// Per CONTEXT.md: objectNames preferred, fall back to text, then className
// Multiple unnamed: use tree position suffix (QPushButton#1, QPushButton#2)
QString ObjectRegistry::generateId(QObject* obj) {
    QStringList segments;
    QObject* current = obj;

    while (current) {
        QString segment;

        // Priority 1: objectName
        if (!current->objectName().isEmpty()) {
            segment = current->objectName();
        }
        // Priority 2: text property (for buttons, labels, etc.)
        else if (current->property("text").isValid() &&
                 !current->property("text").toString().isEmpty()) {
            QString text = current->property("text").toString();
            // Sanitize: take first 20 chars, replace non-alnum
            text = text.left(20).simplified();
            text.replace(QRegularExpression("[^a-zA-Z0-9]"), "_");
            segment = QStringLiteral("text_%1").arg(text);
        }
        // Priority 3: className with sibling index
        else {
            QString className = current->metaObject()->className();
            int siblingIndex = 0;

            if (QObject* parent = current->parent()) {
                for (QObject* sibling : parent->children()) {
                    if (sibling == current) break;
                    if (sibling->metaObject()->className() == current->metaObject()->className())
                        siblingIndex++;
                }
            }

            segment = (siblingIndex > 0)
                ? QStringLiteral("%1#%2").arg(className).arg(siblingIndex + 1)
                : className;
        }

        segments.prepend(segment);
        current = current->parent();
    }

    return segments.join(QStringLiteral("/"));
}
```

### Pattern 3: Meta Inspector for Property/Method Access

**What:** Wrapper around QMetaObject providing JSON-friendly introspection.

**When to use:** Implementing OBJ-04 through OBJ-10 requirements.

**Example:**
```cpp
// meta_inspector.h
class MetaInspector {
public:
    // Object info (OBJ-04)
    static QJsonObject objectInfo(QObject* obj);

    // Properties (OBJ-05, OBJ-06, OBJ-07)
    static QJsonArray listProperties(QObject* obj);
    static QJsonValue getProperty(QObject* obj, const QString& name);
    static bool setProperty(QObject* obj, const QString& name, const QJsonValue& value);

    // Methods (OBJ-08, OBJ-09)
    static QJsonArray listMethods(QObject* obj);
    static QJsonValue invokeMethod(QObject* obj, const QString& name,
                                   const QJsonArray& args);

    // Signals (OBJ-10)
    static QJsonArray listSignals(QObject* obj);
};

// Implementation
QJsonArray MetaInspector::listProperties(QObject* obj) {
    QJsonArray result;
    const QMetaObject* meta = obj->metaObject();

    for (int i = meta->propertyOffset(); i < meta->propertyCount(); ++i) {
        QMetaProperty prop = meta->property(i);
        QJsonObject propInfo;
        propInfo["name"] = QString::fromLatin1(prop.name());
        propInfo["type"] = QString::fromLatin1(prop.typeName());
        propInfo["readable"] = prop.isReadable();
        propInfo["writable"] = prop.isWritable();
        propInfo["value"] = variantToJson(prop.read(obj));
        result.append(propInfo);
    }

    return result;
}

QJsonValue MetaInspector::invokeMethod(QObject* obj, const QString& name,
                                        const QJsonArray& args) {
    const QMetaObject* meta = obj->metaObject();
    int methodIndex = meta->indexOfMethod(name.toLatin1().constData());

    if (methodIndex < 0) {
        // Try with simplified signature
        for (int i = 0; i < meta->methodCount(); ++i) {
            QMetaMethod method = meta->method(i);
            if (QString::fromLatin1(method.name()) == name) {
                methodIndex = i;
                break;
            }
        }
    }

    if (methodIndex < 0)
        throw std::runtime_error("Method not found: " + name.toStdString());

    QMetaMethod method = meta->method(methodIndex);

    // Build argument list - Qt's meta system handles type coercion
    QVariantList varArgs;
    for (const QJsonValue& arg : args) {
        varArgs.append(jsonToVariant(arg));
    }

    QVariant returnValue;
    bool ok = method.invoke(obj, Qt::AutoConnection,
                            Q_RETURN_ARG(QVariant, returnValue),
                            // ... args
                            );

    if (!ok)
        throw std::runtime_error("Failed to invoke method: " + name.toStdString());

    return variantToJson(returnValue);
}
```

### Pattern 4: Signal Subscription with Dynamic Connections

**What:** Subscribe to signals by dynamically connecting to a lambda that pushes notifications.

**When to use:** Implementing SIG-01 through SIG-03 requirements.

**Why:** Public API approach that doesn't require private headers for signal monitoring.

**Example:**
```cpp
// signal_monitor.h
class SignalMonitor : public QObject {
    Q_OBJECT
public:
    // Subscribe to a signal on an object
    QString subscribe(QObject* obj, const QString& signalName);

    // Unsubscribe
    void unsubscribe(const QString& subscriptionId);

signals:
    // Emitted when a subscribed signal fires
    void signalEmitted(const QString& subscriptionId,
                       const QString& objectId,
                       const QString& signalName,
                       const QJsonArray& arguments);

private:
    struct Subscription {
        QPointer<QObject> object;
        QString signalName;
        QMetaObject::Connection connection;
    };

    QHash<QString, Subscription> m_subscriptions;
    int m_nextId = 1;
};

// Implementation using QMetaObject::connect
QString SignalMonitor::subscribe(QObject* obj, const QString& signalName) {
    const QMetaObject* meta = obj->metaObject();
    int signalIndex = meta->indexOfSignal(signalName.toLatin1().constData());

    if (signalIndex < 0) {
        // Try finding by name without arguments
        for (int i = 0; i < meta->methodCount(); ++i) {
            QMetaMethod method = meta->method(i);
            if (method.methodType() == QMetaMethod::Signal &&
                QString::fromLatin1(method.name()) == signalName) {
                signalIndex = i;
                break;
            }
        }
    }

    if (signalIndex < 0)
        throw std::runtime_error("Signal not found: " + signalName.toStdString());

    QString subId = QStringLiteral("sub_%1").arg(m_nextId++);
    QString objId = ObjectRegistry::instance()->objectId(obj);

    QMetaMethod signal = meta->method(signalIndex);

    // Use a signal spy approach - connect and capture args
    auto conn = QObject::connect(obj, signal, this, [=](/* variadic capture */) {
        QJsonArray args;
        // Note: Capturing signal arguments requires specialized handling
        // For simplicity, this example emits without args
        emit signalEmitted(subId, objId, signalName, args);
    });

    m_subscriptions.insert(subId, {obj, signalName, conn});
    return subId;
}
```

### Pattern 5: UI Interaction via QTest Functions

**What:** Use QTest module functions for input simulation (mouse clicks, keyboard input).

**When to use:** Implementing UI-01 and UI-02 requirements.

**Why:** QTest functions are designed for this; they handle timing and event queuing correctly.

**Example:**
```cpp
// input_simulator.h
#include <QTest>

class InputSimulator {
public:
    // Mouse operations (UI-01)
    static void mouseClick(QWidget* widget, Qt::MouseButton button,
                          const QPoint& pos = QPoint(),
                          Qt::KeyboardModifiers modifiers = Qt::NoModifier);

    // Keyboard operations (UI-02)
    static void sendText(QWidget* widget, const QString& text);
    static void sendKeySequence(QWidget* widget, const QString& sequence);
};

// Implementation
void InputSimulator::mouseClick(QWidget* widget, Qt::MouseButton button,
                                const QPoint& pos, Qt::KeyboardModifiers modifiers) {
    // Per CONTEXT.md: coordinates are widget-local
    QPoint clickPos = pos.isNull() ? widget->rect().center() : pos;

    // QTest handles timing, event synthesis, and delivery
    QTest::mouseClick(widget, button, modifiers, clickPos);
}

void InputSimulator::sendText(QWidget* widget, const QString& text) {
    // Ensure widget has focus
    widget->setFocus();
    QApplication::processEvents();

    // Send each character
    for (QChar ch : text) {
        QTest::keyClick(widget, ch.toLatin1());
    }
}

void InputSimulator::sendKeySequence(QWidget* widget, const QString& sequence) {
    // Parse sequence like "Ctrl+Shift+A" or accept explicit modifiers
    QKeySequence keySeq(sequence);

    if (keySeq.isEmpty())
        throw std::runtime_error("Invalid key sequence: " + sequence.toStdString());

    // QKeySequence can have multiple keys; send the first
    int key = keySeq[0].key();
    Qt::KeyboardModifiers mods = keySeq[0].keyboardModifiers();

    QTest::keyClick(widget, static_cast<Qt::Key>(key), mods);
}
```

### Pattern 6: Screenshot Capture

**What:** Capture widget or window screenshots as base64-encoded PNG.

**When to use:** Implementing UI-03 requirement.

**Example:**
```cpp
// screenshot.h
class Screenshot {
public:
    // Capture widget (UI-03)
    static QByteArray captureWidget(QWidget* widget);

    // Capture window including frame
    static QByteArray captureWindow(QWidget* window);

    // Capture arbitrary region
    static QByteArray captureRegion(QWidget* widget, const QRect& region);
};

// Implementation
QByteArray Screenshot::captureWidget(QWidget* widget) {
    // QWidget::grab() captures the widget and its children
    QPixmap pixmap = widget->grab();

    // Convert to PNG and base64 encode
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    pixmap.save(&buffer, "PNG");

    return bytes.toBase64();
}

QByteArray Screenshot::captureWindow(QWidget* window) {
    // grabWindow() captures the window including frame/decorations
    QScreen* screen = window->screen();
    if (!screen)
        throw std::runtime_error("Cannot determine screen for window");

    QPixmap pixmap = screen->grabWindow(window->winId());

    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    pixmap.save(&buffer, "PNG");

    return bytes.toBase64();
}
```

### Pattern 7: Coordinate-to-Widget Hit Testing

**What:** Map coordinates to the widget at that position.

**When to use:** Implementing UI-05 requirement.

**Example:**
```cpp
// hit_test.h
class HitTest {
public:
    // Find widget at global coordinates
    static QWidget* widgetAt(const QPoint& globalPos);

    // Find widget at coordinates relative to a parent widget
    static QWidget* childAt(QWidget* parent, const QPoint& localPos);

    // Get widget geometry (UI-04)
    static QJsonObject widgetGeometry(QWidget* widget);
};

// Implementation
QWidget* HitTest::childAt(QWidget* parent, const QPoint& localPos) {
    // QWidget::childAt() finds the visible child at the position
    QWidget* child = parent->childAt(localPos);
    return child ? child : parent;
}

QJsonObject HitTest::widgetGeometry(QWidget* widget) {
    QJsonObject result;

    // Local geometry (relative to parent)
    QRect local = widget->geometry();
    result["local"] = QJsonObject{
        {"x", local.x()},
        {"y", local.y()},
        {"width", local.width()},
        {"height", local.height()}
    };

    // Global geometry (screen coordinates)
    QPoint globalTopLeft = widget->mapToGlobal(QPoint(0, 0));
    result["global"] = QJsonObject{
        {"x", globalTopLeft.x()},
        {"y", globalTopLeft.y()},
        {"width", widget->width()},
        {"height", widget->height()}
    };

    return result;
}
```

### Anti-Patterns to Avoid

- **Storing raw QObject pointers:** Always use QPointer. Objects can be deleted at any time.
- **Unprotected registry access:** Hook callbacks come from ANY thread. Always use mutex.
- **Blocking during hook callbacks:** Keep hook handlers minimal; queue heavy work.
- **Using QSignalSpy for production:** It uses DirectConnection, causing thread issues.
- **Synchronous method invocation from worker threads:** Use Qt::QueuedConnection.
- **Hardcoded object paths:** Use flexible lookup methods, not brittle hierarchical paths.

## Don't Hand-Roll

Problems that look simple but have existing solutions:

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Object lifecycle tracking | Event filters | qtHookData hooks | Event filters miss objects without event loop |
| Input simulation | QCoreApplication::postEvent | QTest::mouseClick/keyClick | QTest handles timing, coordinates, focus |
| Screenshot capture | Manual painting | QWidget::grab() | grab() handles child widgets, transformations |
| Property type conversion | Manual JSON/QVariant mapping | QMetaType::convert | Qt handles registered type conversions |
| Thread-safe singleton | Double-checked locking | Q_GLOBAL_STATIC | Platform-safe, avoids TLS issues |
| Object ID uniqueness | UUIDs | Hierarchical paths | Human-readable, debuggable, stable when named |

**Key insight:** Qt's meta-object system handles most introspection needs via public APIs. The only private API required is qtHookData for object lifecycle - everything else (properties, methods, signals) uses public QMetaObject APIs.

## Common Pitfalls

### Pitfall 1: Thread Safety in qtHookData Callbacks

**What goes wrong:** AddQObject/RemoveQObject are called from whatever thread creates/destroys the object. Without protection, registry corruption occurs.

**Why it happens:** Qt doesn't synchronize these callbacks. Multiple threads creating objects simultaneously race on shared data.

**How to avoid:**
1. Use QRecursiveMutex (not QMutex) - callbacks may nest
2. Keep critical section minimal - just add/remove from hash
3. Queue signals to main thread via QMetaObject::invokeMethod
4. Test with aggressive multi-threaded object creation

**Warning signs:** Crashes in hash operations, missing objects, duplicate IDs.

### Pitfall 2: Object Deletion During Introspection

**What goes wrong:** Object gets deleted while reading its properties or invoking methods. Crash or undefined behavior.

**Why it happens:** QObject lifetime is controlled by the application, not the probe. User could close a dialog mid-introspection.

**How to avoid:**
1. Always use QPointer when storing object references
2. Check QPointer validity before EVERY access
3. Per CONTEXT.md: return JSON-RPC error with specific code if object not found
4. Consider holding registry mutex during entire operation

**Warning signs:** Intermittent crashes, "pure virtual method called" errors.

### Pitfall 3: QVariant to JSON Type Mapping

**What goes wrong:** Some Qt types (QPoint, QSize, custom types) don't have obvious JSON representations. Conversion fails or produces useless output.

**Why it happens:** QJsonValue only supports: bool, double, string, array, object, null. QVariant supports hundreds of types.

**How to avoid:**
1. Build explicit conversion for common Qt types (QPoint, QSize, QRect, QColor)
2. For unknown types, use QVariant::toString() as fallback
3. Include type name in response so client knows what it got
4. Per CONTEXT.md: let Qt's meta-object system attempt coercion for setProperty

**Warning signs:** Empty values, "null" for non-null properties, type errors on write.

### Pitfall 4: Method Invocation Argument Marshaling

**What goes wrong:** JSON arguments don't convert to Qt types expected by the method. Invocation fails or crashes.

**Why it happens:** QMetaMethod::invoke requires exact type matching. JSON number becomes double, but method expects int.

**How to avoid:**
1. Use QMetaType::convert to coerce types before invoke
2. Read parameter types from QMetaMethod::parameterTypes()
3. Provide helpful error message listing expected vs. actual types
4. Per CONTEXT.md: error only if conversion fails, let Qt try first

**Warning signs:** "Unable to handle unregistered datatype" errors, wrong values in slots.

### Pitfall 5: Signal Argument Capture

**What goes wrong:** Subscribing to a signal captures no arguments, or wrong arguments. Client gets useless notifications.

**Why it happens:** Dynamic connection to signals requires knowing argument types at compile time for proper capture. Generic lambda can't receive signal arguments.

**How to avoid:**
1. Use the private qt_register_signal_spy_callbacks for full argument capture
2. Or create per-signal-signature handler classes
3. At minimum, capture argument count and types even if values unavailable
4. Document which signals support full argument capture

**Warning signs:** Empty argument arrays, crash on signal with non-QVariant args.

### Pitfall 6: High-DPI Screenshot Issues

**What goes wrong:** Screenshots have wrong size or resolution on high-DPI displays. Coordinates don't match visual positions.

**Why it happens:** Qt uses device-independent pixels. Physical pixels differ by devicePixelRatio. grab() returns device pixels, but sizes are in logical pixels.

**How to avoid:**
1. Account for devicePixelRatio in coordinate mapping
2. Return both logical and physical dimensions in geometry responses
3. Use widget->devicePixelRatioF() for accurate scaling
4. Test on high-DPI displays (Windows scaling, macOS Retina)

**Warning signs:** Screenshots too small/large, clicks miss targets on scaled displays.

## Code Examples

### JSON-RPC Method Registration for Introspection

```cpp
// Source: JsonRpcHandler integration pattern
void Probe::registerIntrospectionMethods() {
    auto* handler = m_server->jsonRpcHandler();

    // OBJ-01: Find by objectName
    handler->RegisterMethod("findByObjectName", [](const QString& params) {
        QJsonDocument doc = QJsonDocument::fromJson(params.toUtf8());
        QString name = doc.object()["name"].toString();
        QString root = doc.object()["root"].toString();  // Optional

        QObject* rootObj = root.isEmpty() ? nullptr
            : ObjectRegistry::instance()->findById(root);
        QObject* found = ObjectRegistry::instance()->findByObjectName(name, rootObj);

        if (!found)
            throw std::runtime_error("Object not found: " + name.toStdString());

        QString id = ObjectRegistry::instance()->objectId(found);
        return QJsonDocument(QJsonObject{{"id", id}}).toJson();
    });

    // OBJ-05: List properties
    handler->RegisterMethod("listProperties", [](const QString& params) {
        QJsonDocument doc = QJsonDocument::fromJson(params.toUtf8());
        QString id = doc.object()["id"].toString();

        QObject* obj = ObjectRegistry::instance()->findById(id);
        if (!obj)
            throw std::runtime_error("Object not found: " + id.toStdString());

        QJsonArray props = MetaInspector::listProperties(obj);
        return QJsonDocument(props).toJson();
    });

    // OBJ-07: Set property
    handler->RegisterMethod("setProperty", [](const QString& params) {
        QJsonDocument doc = QJsonDocument::fromJson(params.toUtf8());
        QString id = doc.object()["id"].toString();
        QString name = doc.object()["name"].toString();
        QJsonValue value = doc.object()["value"];

        QObject* obj = ObjectRegistry::instance()->findById(id);
        if (!obj)
            throw std::runtime_error("Object not found");

        if (!MetaInspector::setProperty(obj, name, value))
            throw std::runtime_error("Property is read-only: " + name.toStdString());

        return QJsonDocument(QJsonObject{{"success", true}}).toJson();
    });
}
```

### Object Tree Serialization

```cpp
// Source: OBJ-03 requirement, OBJ-11 hierarchical IDs
QJsonObject serializeObjectTree(QObject* root, int maxDepth = -1, int currentDepth = 0) {
    if (!root || (maxDepth >= 0 && currentDepth > maxDepth))
        return QJsonObject();

    QJsonObject obj;
    obj["id"] = ObjectRegistry::instance()->objectId(root);
    obj["className"] = QString::fromLatin1(root->metaObject()->className());
    obj["objectName"] = root->objectName();

    // Add geometry if it's a widget
    if (auto* widget = qobject_cast<QWidget*>(root)) {
        obj["visible"] = widget->isVisible();
        obj["geometry"] = HitTest::widgetGeometry(widget);
    }

    // Recursively add children
    QJsonArray children;
    for (QObject* child : root->children()) {
        QJsonObject childObj = serializeObjectTree(child, maxDepth, currentDepth + 1);
        if (!childObj.isEmpty())
            children.append(childObj);
    }

    if (!children.isEmpty())
        obj["children"] = children;

    return obj;
}
```

### Type-Safe Signal Subscription Using Private API

```cpp
// Source: GammaRay pattern, <private/qobject_p.h>
// Note: This uses Qt's private API for full signal argument capture
#include <private/qobject_p.h>

namespace {
    QSignalSpyCallbackSet g_prevCallbacks;
    SignalMonitor* g_monitor = nullptr;
}

void signalBeginCallback(QObject* caller, int signalIndex, void** argv) {
    // Forward to previous callbacks (daisy-chain)
    if (g_prevCallbacks.signal_begin_callback)
        g_prevCallbacks.signal_begin_callback(caller, signalIndex, argv);

    if (g_monitor)
        g_monitor->onSignalEmit(caller, signalIndex, argv);
}

void SignalMonitor::installSignalHooks() {
    // Save existing callbacks
    g_prevCallbacks = qt_signal_spy_callback_set;
    g_monitor = this;

    // Install our callbacks
    QSignalSpyCallbackSet callbacks = g_prevCallbacks;
    callbacks.signal_begin_callback = signalBeginCallback;
    qt_register_signal_spy_callbacks(callbacks);
}

void SignalMonitor::onSignalEmit(QObject* caller, int signalIndex, void** argv) {
    // Check if this object/signal is subscribed
    QString objId = ObjectRegistry::instance()->objectId(caller);

    for (auto it = m_subscriptions.begin(); it != m_subscriptions.end(); ++it) {
        if (it->object == caller && it->signalIndex == signalIndex) {
            // Capture arguments
            QJsonArray args = captureSignalArguments(caller, signalIndex, argv);

            // Emit notification (will be picked up by transport layer)
            emit signalEmitted(it.key(), objId, it->signalName, args);
        }
    }
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| QMetaMethod with QGenericArgument | Template-based invoke (Qt 6.5) | Qt 6.5 | Simpler argument passing |
| QMetaProperty::read returns QVariant | Same, but QMetaType improved | Qt 6.0 | Better type info |
| Manual JSON conversion | QJsonValue::fromVariant improved | Qt 5.15 | More types supported |
| Static QMetaObject access | Same | N/A | Stable across versions |

**Deprecated/outdated:**
- `QObject::connect(SIGNAL(), SLOT())` string-based syntax: Use pointer-to-member syntax
- `qt_metacall` direct calls: Use QMetaMethod::invoke instead
- `QGenericArgument` for invoke: Use template overloads in Qt 6.5+

## Open Questions

1. **Full signal argument capture in Qt 6**
   - What we know: `qt_register_signal_spy_callbacks` exists in Qt 6, same as Qt 5
   - What's unclear: Exact behavior differences between Qt 5.15 and Qt 6.x
   - Recommendation: Test on both versions; fallback to no-args notifications if needed

2. **QML object integration**
   - What we know: QML items are QObjects, appear in registry
   - What's unclear: How to access QML-specific properties (context properties, bindings)
   - Recommendation: Defer to Phase 6 (QML Support); basic QObject introspection works

3. **Object discovery after late hook installation**
   - What we know: Objects created before hooks installed won't trigger callback
   - What's unclear: Best way to enumerate existing objects
   - Recommendation: Walk QApplication::topLevelWidgets() tree at startup

4. **Performance with large object counts**
   - What we know: Registry uses QHash (O(1) lookup)
   - What's unclear: Memory overhead at 100k+ objects
   - Recommendation: Profile during integration testing; consider weak references if needed

## Sources

### Primary (HIGH confidence)

- [Qt QMetaObject Documentation](https://doc.qt.io/qt-6/qmetaobject.html) - Property/method introspection
- [Qt QMetaProperty Documentation](https://doc.qt.io/qt-6/qmetaproperty.html) - Property read/write
- [Qt QMetaMethod Documentation](https://doc.qt.io/qt-6/qmetamethod.html) - Method invocation
- [Qt QTest Documentation](https://doc.qt.io/qt-6/qtest.html) - Input simulation functions
- [Qt Screenshot Example](https://doc.qt.io/qt-6/qtwidgets-desktop-screenshot-example.html) - QWidget::grab(), QScreen::grabWindow()
- [Qt QWidget::childAt() Documentation](https://doc.qt.io/qt-6/qwidget.html) - Hit testing
- [GammaRay hooks.cpp](https://github.com/KDAB/GammaRay/blob/master/probe/hooks.cpp) - qtHookData installation pattern
- [GammaRay probe.cpp](https://github.com/KDAB/GammaRay/blob/master/core/probe.cpp) - Object registry pattern

### Secondary (MEDIUM confidence)

- [Qt Forum: qt_register_signal_spy_callbacks](https://www.qtcentre.org/threads/16484-Is-it-OK-to-use-qt_register_signal_spy_callbacks) - Signal spy private API usage
- [How Qt Signals Work (Woboq)](https://woboq.com/blog/how-qt-signals-slots-work.html) - Signal internals
- [Qt Widget Coordinate Mapping (RuneBook)](https://runebook.dev/en/articles/qt/qwidget/mapTo) - Coordinate transformation

### Tertiary (LOW confidence)

- qt_register_signal_spy_callbacks exact signature in Qt 6 (needs source verification)
- Performance characteristics of QHash with 100k+ objects (needs profiling)
- High-DPI behavior differences between platforms (needs testing)

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH - Using documented Qt public APIs
- Object Registry: HIGH - GammaRay pattern well-established, qtHookData stable since Qt 5.0
- Meta Inspector: HIGH - QMetaObject/Property/Method are stable public APIs
- Signal Monitoring: MEDIUM - Private API involved for full argument capture
- UI Interaction: HIGH - QTest module is public, stable API
- Screenshot: HIGH - QWidget::grab() is public, documented API

**Research date:** 2026-01-30
**Valid until:** 2026-03-30 (60 days - Qt meta-object system is extremely stable)
