# Phase 3: Native Mode - Research

**Researched:** 2026-01-31
**Domain:** JSON-RPC API reorganization, object referencing, error handling, convenience methods
**Confidence:** HIGH

## Summary

Phase 3 transforms the existing 21 Phase 2 JSON-RPC methods (currently under the flat `qtpilot.*` namespace) into a polished, agent-friendly Native Mode API. The existing introspection, mutation, interaction, and signal monitoring infrastructure is fully functional -- this phase is about API surface design, not new Qt introspection logic.

The core work involves: (1) reorganizing methods into dotted namespaces like `qt.objects.*`, `qt.properties.*`, etc., (2) implementing three object referencing styles (hierarchical path, numeric shorthand, symbolic names), (3) wrapping all responses in a uniform `{result, meta}` envelope, (4) adding convenience methods (`qt.objects.inspect`, `qt.objects.query`), and (5) standardizing error handling with detailed error codes and self-correcting hints.

The existing `JsonRpcHandler` already supports registering methods by arbitrary string names, so the namespace transition is mechanical -- register new dotted methods that delegate to the same underlying logic. The `ObjectRegistry` already computes hierarchical IDs; the new work is adding numeric shorthand lookup and symbolic name resolution as additional ID resolution layers.

**Primary recommendation:** Create an `ObjectResolver` class that accepts any of the three ID styles and resolves to `QObject*`. Build a `ResponseEnvelope` helper that wraps results uniformly. Register new `qt.*` namespaced methods in a dedicated `NativeModeApi` class. Keep old `qtpilot.*` methods for backward compatibility during transition.

## Standard Stack

### Core (Already in Project)

| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| QJsonDocument/QJsonObject | Qt 6.x | JSON response building | Already used throughout, zero new deps |
| ObjectRegistry | Phase 2 | Object tracking & hierarchical ID | Foundation for all object referencing |
| MetaInspector | Phase 2 | Property/method/signal introspection | Already exposes full QMetaObject |
| SignalMonitor | Phase 2 | Signal subscription system | Already implements subscribe/unsubscribe |
| InputSimulator | Phase 2 | Mouse/keyboard simulation | Already wraps QTest functions |
| Screenshot | Phase 2 | Widget capture to base64 PNG | Already implements captureWidget/Window/Region |
| HitTest | Phase 2 | Coordinate-to-widget mapping | Already implements widgetAt/childAt/geometry |
| JsonRpcHandler | Phase 1 | Method registration & dispatch | Already supports arbitrary method names |

### New Components (To Build)

| Component | Purpose | Complexity |
|-----------|---------|------------|
| ObjectResolver | Resolve any ID style (path/numeric/symbolic) to QObject* | MEDIUM |
| SymbolicNameMap | Load/save/manage symbolic name aliases | MEDIUM |
| ResponseEnvelope | Uniform `{result, meta}` wrapper | LOW |
| NativeModeApi | Register all `qt.*` namespaced methods | MEDIUM-HIGH (volume) |
| ErrorCodes | Application-specific error codes with schema hints | LOW |

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Custom ObjectResolver | Extend ObjectRegistry directly | Resolver is cleaner separation; registry stays focused on tracking |
| JSON file for name map | SQLite | JSON is human-editable, version-controllable; SQLite is overkill |
| Separate NativeModeApi class | Inline in RegisterBuiltinMethods | Separate class is testable, maintainable |
| Uniform envelope wrapper | Raw responses | Envelope lets agents parse uniformly; small overhead |

## Architecture Patterns

### Recommended Project Structure

```
src/
└── probe/
    ├── core/
    │   ├── probe.h/.cpp              # Existing - singleton orchestrator
    │   ├── object_registry.h/.cpp    # Existing - lifecycle tracking
    │   └── object_resolver.h/.cpp    # NEW - Multi-style ID resolution
    ├── introspection/
    │   ├── meta_inspector.h/.cpp     # Existing - QMetaObject wrapper
    │   ├── object_id.h/.cpp          # Existing - Hierarchical ID gen
    │   ├── signal_monitor.h/.cpp     # Existing - Signal subscriptions
    │   └── variant_json.h/.cpp       # Existing - QVariant<->JSON
    ├── interaction/
    │   ├── input_simulator.h/.cpp    # Existing
    │   ├── screenshot.h/.cpp         # Existing
    │   └── hit_test.h/.cpp           # Existing
    ├── api/
    │   ├── native_mode_api.h/.cpp    # NEW - All qt.* method registrations
    │   ├── response_envelope.h/.cpp  # NEW - Uniform response wrapper
    │   ├── error_codes.h             # NEW - Application error code constants
    │   └── symbolic_name_map.h/.cpp  # NEW - Squish-style name aliases
    └── transport/
        ├── websocket_server.h/.cpp   # Existing
        └── jsonrpc_handler.h/.cpp    # Existing - RegisterBuiltinMethods stays
```

### Pattern 1: ObjectResolver - Multi-Style ID Resolution

**What:** A single entry point that accepts any of the three ID styles and resolves to `QObject*`.

**When to use:** Every Native Mode method that takes an `objectId` parameter.

**Example:**
```cpp
// object_resolver.h
namespace qtpilot {

class ObjectResolver {
public:
    /// Resolve any ID style to QObject*.
    /// Tries in order: numeric shorthand, symbolic name, hierarchical path.
    /// Returns nullptr if not found.
    static QObject* resolve(const QString& id);

    /// Register a numeric shorthand for an object.
    /// Returns the assigned numeric ID.
    static int assignNumericId(QObject* obj);

    /// Look up an object by its numeric shorthand.
    static QObject* findByNumericId(int numericId);

    /// Register a symbolic name for an object ID.
    static void registerName(const QString& symbolicName, const QString& objectId);

    /// Unregister a symbolic name.
    static void unregisterName(const QString& symbolicName);

    /// Load symbolic names from a JSON file.
    static void loadNameMap(const QString& filePath);

    /// Save current symbolic names to a JSON file.
    static void saveNameMap(const QString& filePath);

private:
    /// Detect ID style and dispatch.
    /// Numeric IDs: all digits, optionally prefixed with '#'
    /// Symbolic names: start with '@' or found in name map
    /// Everything else: hierarchical path
    static IdStyle detectStyle(const QString& id);
};

}  // namespace qtpilot
```

**Resolution logic:**
```cpp
QObject* ObjectResolver::resolve(const QString& id) {
    if (id.isEmpty()) return nullptr;

    // Style 1: Numeric shorthand (e.g., "#42" or plain "42" if all digits)
    if (id.startsWith('#') || id.toInt() > 0) {
        int numId = id.startsWith('#') ? id.mid(1).toInt() : id.toInt();
        QObject* obj = findByNumericId(numId);
        if (obj) return obj;
    }

    // Style 2: Symbolic name (check name map)
    QString resolved = SymbolicNameMap::instance()->resolve(id);
    if (!resolved.isEmpty()) {
        return ObjectRegistry::instance()->findById(resolved);
    }

    // Style 3: Hierarchical path (default)
    return ObjectRegistry::instance()->findById(id);
}
```

### Pattern 2: Uniform Response Envelope

**What:** All Native Mode responses wrapped in `{result: ..., meta: {...}}`.

**When to use:** Every `qt.*` method response.

**Example:**
```cpp
// response_envelope.h
namespace qtpilot {

class ResponseEnvelope {
public:
    /// Wrap a result with metadata.
    /// @param result The method-specific result (any JSON value)
    /// @param objectId Optional - the objectId this result relates to
    /// @return JSON string: {"result": ..., "meta": {"timestamp": ..., ...}}
    static QString wrap(const QJsonValue& result,
                       const QString& objectId = QString());

    /// Wrap with extra meta fields.
    static QString wrap(const QJsonValue& result,
                       const QJsonObject& extraMeta);
};

}  // namespace qtpilot
```

**Implementation:**
```cpp
QString ResponseEnvelope::wrap(const QJsonValue& result, const QString& objectId) {
    QJsonObject meta;
    meta["timestamp"] = QDateTime::currentMSecsSinceEpoch();
    if (!objectId.isEmpty()) {
        meta["objectId"] = objectId;
    }

    QJsonObject envelope;
    envelope["result"] = result;
    envelope["meta"] = meta;

    return QString::fromUtf8(QJsonDocument(envelope).toJson(QJsonDocument::Compact));
}
```

### Pattern 3: NativeModeApi Registration

**What:** Centralized class that registers all `qt.*` methods on the JsonRpcHandler.

**When to use:** During probe initialization, after WebSocket server creation.

**Example:**
```cpp
// native_mode_api.h
namespace qtpilot {

class NativeModeApi : public QObject {
    Q_OBJECT
public:
    explicit NativeModeApi(JsonRpcHandler* handler, QObject* parent = nullptr);

private:
    void registerObjectMethods();    // qt.objects.*
    void registerPropertyMethods();  // qt.properties.*
    void registerMethodMethods();    // qt.methods.*
    void registerSignalMethods();    // qt.signals.*
    void registerUiMethods();        // qt.ui.*
    void registerNameMapMethods();   // qt.names.*
    void registerSystemMethods();    // qt.ping, qt.version

    JsonRpcHandler* m_handler;
};

}  // namespace qtpilot
```

### Pattern 4: Application Error Codes

**What:** Specific error codes in the -32000 to -32099 range for common failure modes.

**When to use:** All error responses from `qt.*` methods.

**Example:**
```cpp
// error_codes.h
namespace qtpilot {
namespace ErrorCode {

// Object errors (-32001 to -32009)
constexpr int kObjectNotFound = -32001;
constexpr int kObjectStale = -32002;       // Object was deleted since last query
constexpr int kObjectNotWidget = -32003;   // Expected QWidget, got QObject

// Property errors (-32010 to -32019)
constexpr int kPropertyNotFound = -32010;
constexpr int kPropertyReadOnly = -32011;
constexpr int kPropertyTypeMismatch = -32012;

// Method errors (-32020 to -32029)
constexpr int kMethodNotFound = -32020;
constexpr int kMethodInvocationFailed = -32021;
constexpr int kMethodArgumentMismatch = -32022;

// Signal errors (-32030 to -32039)
constexpr int kSignalNotFound = -32030;
constexpr int kSubscriptionNotFound = -32031;

// UI interaction errors (-32040 to -32049)
constexpr int kWidgetNotVisible = -32040;
constexpr int kWidgetNotEnabled = -32041;
constexpr int kScreenCaptureError = -32042;

// Name map errors (-32050 to -32059)
constexpr int kNameNotFound = -32050;
constexpr int kNameAlreadyExists = -32051;
constexpr int kNameMapLoadError = -32052;

}  // namespace ErrorCode
}  // namespace qtpilot
```

### Pattern 5: Symbolic Name Map (Squish-Inspired)

**What:** A JSON file mapping user-defined short names to hierarchical object paths, plus runtime API to add/remove entries.

**When to use:** When agents want stable, short references to commonly-used widgets.

**File format:**
```json
{
  "submitButton": "QMainWindow/centralWidget/QPushButton#submit",
  "nameField": "QMainWindow/centralWidget/QLineEdit",
  "statusBar": "QMainWindow/QStatusBar"
}
```

**Example:**
```cpp
// symbolic_name_map.h
namespace qtpilot {

class SymbolicNameMap : public QObject {
    Q_OBJECT
public:
    static SymbolicNameMap* instance();

    /// Resolve a symbolic name to a hierarchical path.
    /// Returns empty string if not found.
    QString resolve(const QString& symbolicName) const;

    /// Register a name -> path mapping.
    void registerName(const QString& name, const QString& path);

    /// Unregister a mapping.
    void unregisterName(const QString& name);

    /// Get all registered names.
    QJsonObject allNames() const;

    /// Load from JSON file.
    bool loadFromFile(const QString& filePath);

    /// Save to JSON file.
    bool saveToFile(const QString& filePath) const;

private:
    QHash<QString, QString> m_nameMap;
    QString m_filePath;
    mutable QMutex m_mutex;
};

}  // namespace qtpilot
```

### Pattern 6: Structured Error Responses with Schema Hints

**What:** Error responses include expected parameter schema so agents can self-correct.

**When to use:** Validation errors on `qt.*` methods.

**Example error response:**
```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "error": {
    "code": -32602,
    "message": "Invalid params: missing required parameter 'objectId'",
    "data": {
      "method": "qt.properties.get",
      "expected": {
        "objectId": {"type": "string", "required": true, "description": "Object ID (path, numeric #N, or symbolic name)"},
        "name": {"type": "string", "required": true, "description": "Property name"}
      },
      "received": {"name": "text"}
    }
  }
}
```

### Anti-Patterns to Avoid

- **Breaking old method names immediately:** Keep `qtpilot.*` methods registered alongside new `qt.*` methods during transition. Removing them breaks existing tests.
- **Coupling ObjectResolver to JsonRpcHandler:** Resolver should be a standalone utility, not embedded in handler code.
- **Making the envelope optional:** All `qt.*` methods must use the envelope. Inconsistency defeats the purpose.
- **Storing numeric IDs permanently:** Numeric shorthand IDs should be session-scoped (cleared on client disconnect). They are for convenience, not persistence.
- **Auto-resolving stale references:** Per CONTEXT.md decision, stale refs return an error with a hint to re-query. Do NOT silently re-resolve.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| JSON file read/write | Manual file I/O with string parsing | QJsonDocument::fromJson + QFile | Handles encoding, validation, errors |
| Timestamp generation | Manual epoch calculation | QDateTime::currentMSecsSinceEpoch() | Cross-platform, correct timezone handling |
| Thread-safe singleton for NameMap | Double-checked locking | Q_GLOBAL_STATIC | Same pattern used by ObjectRegistry, SignalMonitor |
| Parameter validation | Ad-hoc per-method checks | Shared validation helper function | Extract common pattern: parse params, validate required fields, resolve objectId |
| Method-to-namespace mapping | Manual string manipulation | Lookup table / static registration | Predictable, testable |

**Key insight:** Phase 3 is API surface design, not new introspection logic. All the hard Qt introspection work was done in Phase 2. The risk here is inconsistency and boilerplate, not technical complexity. Use helper functions and code generation patterns to ensure uniform handling across all ~25 methods.

## Common Pitfalls

### Pitfall 1: Boilerplate Explosion

**What goes wrong:** Each of the ~25 methods has nearly identical parameter parsing, objectId resolution, error handling, and response wrapping. Copy-pasting leads to subtle inconsistencies.

**Why it happens:** JSON-RPC handlers are naturally repetitive. Without extraction, each method is 20-30 lines of boilerplate around 2-3 lines of actual logic.

**How to avoid:**
1. Create a shared `resolveObjectParam()` helper that parses params, extracts objectId, resolves it, and throws structured errors
2. Create a `wrapResponse()` helper that applies the envelope uniformly
3. Use the error code constants instead of inline magic numbers
4. Consider a macro or template for the common pattern: parse -> resolve -> call -> wrap

**Warning signs:** Methods that look identical except for the inner call; inconsistent error messages; some methods wrap, others don't.

### Pitfall 2: Numeric ID Collision After Object Deletion

**What goes wrong:** Object gets ID #42, gets deleted, new object gets ID #42. Agent references stale #42, gets wrong object silently.

**Why it happens:** Simple incrementing counter with recycling, or hash-based IDs that collide.

**How to avoid:**
1. Use monotonically increasing counter (never recycle)
2. Clear numeric ID map on client disconnect (session-scoped)
3. Validate that the resolved object still matches expected className if provided
4. Return kObjectStale error if the object behind a numeric ID was destroyed

**Warning signs:** Agents getting unexpected objects, especially after dialogs close or list items change.

### Pitfall 3: Symbolic Name Map Stale After UI Changes

**What goes wrong:** Name map points to "QMainWindow/centralWidget/QPushButton#2" but after a UI rearrangement, QPushButton#2 is now a different button.

**Why it happens:** Hierarchical IDs depend on sibling order. Adding/removing siblings shifts indices.

**How to avoid:**
1. Prefer objectName-based IDs in the name map (stable across rearrangements)
2. Document that symbolic names pointing to index-based paths may break
3. Provide a `qt.names.validate` method that checks all registered names still resolve
4. Per CONTEXT.md: return error with hint to re-query, don't auto-resolve

**Warning signs:** Name map entries silently pointing to wrong widgets after app restart with UI changes.

### Pitfall 4: Envelope Breaking JSON-RPC Response Structure

**What goes wrong:** The uniform envelope `{result: ..., meta: {...}}` gets confused with the JSON-RPC `result` field. Double-wrapping occurs.

**Why it happens:** JSON-RPC already has a `result` field: `{"jsonrpc":"2.0","id":1,"result": ...}`. The envelope becomes nested inside.

**How to avoid:**
1. The envelope IS the JSON-RPC `result` value. So the wire format is: `{"jsonrpc":"2.0","id":1,"result":{"result":...,"meta":{...}}}`
2. This is intentional -- the outer `result` is JSON-RPC protocol, the inner `result` is method-specific
3. Document this clearly for agents
4. Alternative: put `meta` alongside `result` at the JSON-RPC level, but this breaks the spec (only `result` OR `error` allowed)

**Warning signs:** Agents can't find data because they parse one level too shallow or deep.

### Pitfall 5: Missing Error Data Field for Self-Correction

**What goes wrong:** Error responses say "property not found" but don't tell the agent what properties ARE available. Agent can't self-correct.

**Why it happens:** Error handling focuses on the failure, not on providing recovery context.

**How to avoid:**
1. For "object not found": include hint to call `qt.objects.find` or `qt.objects.query`
2. For "property not found": include list of available property names in error `data`
3. For "method not found": include list of available method names in error `data`
4. For parameter validation: include the expected schema in error `data`

**Warning signs:** Agents entering retry loops without making progress; agents unable to discover available features from error messages alone.

### Pitfall 6: qt.ping Implementation Too Simple

**What goes wrong:** `qt.ping` just returns "pong" immediately, not measuring actual event loop health.

**Why it happens:** Ping is trivially implemented as a synchronous return.

**How to avoid:**
1. Post an event to the Qt event loop and measure round-trip time
2. Return timestamp + event loop latency in the ping response
3. This lets agents detect if the app's event loop is frozen (common in long operations)
4. Keep it simple: post QTimer::singleShot(0) and measure wall-clock delta

**Warning signs:** ping returns "pong" but the app is actually frozen; agents don't detect hangs.

## Code Examples

### Complete Method Registration Pattern

```cpp
// Source: Derived from existing jsonrpc_handler.cpp patterns
void NativeModeApi::registerObjectMethods() {
    // qt.objects.find - Find object by name (NAT-01)
    m_handler->RegisterMethod("qt.objects.find", [](const QString& params) -> QString {
        QJsonDocument doc = QJsonDocument::fromJson(params.toUtf8());
        QJsonObject p = doc.object();

        // Validate required param
        if (!p.contains("name") || p["name"].toString().isEmpty()) {
            return createValidationError("qt.objects.find", "name",
                {{"name", QJsonObject{{"type", "string"}, {"required", true}}}});
        }

        QString name = p["name"].toString();
        QString rootId = p["root"].toString();

        QObject* root = rootId.isEmpty() ? nullptr : ObjectResolver::resolve(rootId);
        QObject* found = ObjectRegistry::instance()->findByObjectName(name, root);

        if (!found) {
            return createError(ErrorCode::kObjectNotFound,
                QString("No object with objectName '%1'").arg(name),
                QJsonObject{{"hint", "Use qt.objects.query to search by other criteria"}});
        }

        QString id = ObjectRegistry::instance()->objectId(found);
        return ResponseEnvelope::wrap(
            QJsonObject{{"objectId", id}, {"className", QString::fromLatin1(found->metaObject()->className())}},
            id);
    });
}
```

### ObjectResolver Usage in Methods

```cpp
// Source: Common pattern for all methods taking objectId
// Helper to extract and resolve objectId from params
QObject* resolveObjectParam(const QJsonObject& params, const QString& methodName) {
    QString objectId = params["objectId"].toString();
    if (objectId.isEmpty()) {
        throw JsonRpcException(JsonRpcError::kInvalidParams,
            "Missing required parameter 'objectId'",
            QJsonObject{{"method", methodName}});
    }

    QObject* obj = ObjectResolver::resolve(objectId);
    if (!obj) {
        throw JsonRpcException(ErrorCode::kObjectNotFound,
            QString("Object not found: %1").arg(objectId),
            QJsonObject{{"hint", "Object may have been destroyed. Re-query with qt.objects.find or qt.objects.query"}});
    }

    return obj;
}

// Helper requiring QWidget specifically
QWidget* resolveWidgetParam(const QJsonObject& params, const QString& methodName) {
    QObject* obj = resolveObjectParam(params, methodName);
    QWidget* widget = qobject_cast<QWidget*>(obj);
    if (!widget) {
        throw JsonRpcException(ErrorCode::kObjectNotWidget,
            QString("Object is not a QWidget: %1").arg(params["objectId"].toString()),
            QJsonObject{{"actualClass", QString::fromLatin1(obj->metaObject()->className())}});
    }
    return widget;
}
```

### Symbolic Name Map File Loading

```cpp
// Source: Derived from Squish object map concept + QJsonDocument API
bool SymbolicNameMap::loadFromFile(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Cannot open name map file:" << filePath;
        return false;
    }

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError) {
        qWarning() << "Name map parse error:" << error.errorString();
        return false;
    }

    QMutexLocker lock(&m_mutex);
    m_nameMap.clear();
    m_filePath = filePath;

    QJsonObject map = doc.object();
    for (auto it = map.begin(); it != map.end(); ++it) {
        m_nameMap.insert(it.key(), it.value().toString());
    }

    qInfo() << "Loaded" << m_nameMap.size() << "symbolic names from" << filePath;
    return true;
}
```

### Full API Namespace Mapping

```cpp
// The complete mapping from Phase 2 methods to Phase 3 namespaces:

// === Object Discovery (NAT-01) ===
// qtpilot.findByObjectName  -> qt.objects.find
// qtpilot.findByClassName   -> qt.objects.findByClass
// qtpilot.getObjectTree     -> qt.objects.tree
// qtpilot.getObjectInfo     -> qt.objects.info
// NEW                     -> qt.objects.inspect    (combined props+methods+signals)
// NEW                     -> qt.objects.query      (rich filtering)

// === Properties (NAT-02) ===
// qtpilot.listProperties    -> qt.properties.list
// qtpilot.getProperty       -> qt.properties.get
// qtpilot.setProperty       -> qt.properties.set

// === Methods (NAT-03) ===
// qtpilot.listMethods       -> qt.methods.list
// qtpilot.invokeMethod      -> qt.methods.invoke

// === Signals (NAT-05) ===
// qtpilot.listSignals       -> qt.signals.list
// qtpilot.subscribeSignal   -> qt.signals.subscribe
// qtpilot.unsubscribeSignal -> qt.signals.unsubscribe
// qtpilot.setLifecycleNotifications -> qt.signals.setLifecycle

// === UI Interaction (NAT-04) ===
// qtpilot.click             -> qt.ui.click
// qtpilot.sendKeys          -> qt.ui.sendKeys
// qtpilot.screenshot        -> qt.ui.screenshot
// qtpilot.getGeometry       -> qt.ui.geometry
// qtpilot.hitTest           -> qt.ui.hitTest

// === Name Map (NEW) ===
// NEW -> qt.names.register
// NEW -> qt.names.unregister
// NEW -> qt.names.list
// NEW -> qt.names.validate
// NEW -> qt.names.load
// NEW -> qt.names.save

// === System ===
// ping       -> qt.ping           (enhanced with event loop health)
// getVersion -> qt.version
// getModes   -> qt.modes
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Flat `qtpilot.*` namespace | Dotted `qt.domain.method` | Phase 3 | Better discoverability |
| Raw JSON result | Envelope `{result, meta}` | Phase 3 | Uniform agent parsing |
| Hierarchical path only | Three ID styles (path/numeric/symbolic) | Phase 3 | Agent flexibility |
| Generic std::exception errors | Typed error codes with data/hints | Phase 3 | Agent self-correction |

**Unchanged from Phase 2:**
- All underlying introspection logic (MetaInspector, ObjectRegistry, etc.)
- WebSocket transport and single-client semantics
- Signal monitoring via SignalRelay / dynamic connections
- QTest-based input simulation and QWidget::grab() screenshots

## Open Questions

1. **Backward compatibility period for `qtpilot.*` methods**
   - What we know: New `qt.*` methods will coexist alongside old `qtpilot.*` methods
   - What's unclear: When to remove old methods (Phase 4? never?)
   - Recommendation: Keep both registered indefinitely for now; mark old ones deprecated in `qt.version` response

2. **Numeric ID assignment trigger**
   - What we know: Numeric IDs should be session-scoped
   - What's unclear: Should they be assigned on-demand (when first referenced) or proactively (for all objects)?
   - Recommendation: On-demand only. Assign when an object appears in a response. Include `numericId` in response meta when assigned.

3. **Symbolic name file auto-loading path**
   - What we know: CONTEXT.md says loaded from file at startup
   - What's unclear: What env var or convention names the file? `QTPILOT_NAME_MAP`? Alongside the app executable?
   - Recommendation: Check `QTPILOT_NAME_MAP` env var first, then look for `qtPilot-names.json` in the app's working directory. Both configurable.

4. **qt.objects.query filter complexity**
   - What we know: Should support `{className: "QPushButton", properties: {enabled: true}}`
   - What's unclear: Should it support regex, wildcards, or compound conditions?
   - Recommendation: Start simple -- exact match on className and property values. Add regex/wildcards in a future phase if agents need them.

## Sources

### Primary (HIGH confidence)

- Existing codebase analysis: `src/probe/transport/jsonrpc_handler.cpp` -- all 21 current methods reviewed
- Existing codebase analysis: `src/probe/core/object_registry.h` -- ID generation and lookup mechanisms
- Existing codebase analysis: `src/probe/introspection/meta_inspector.h` -- full introspection API surface
- [JSON-RPC 2.0 Specification](https://www.jsonrpc.org/specification) -- Error code ranges (-32000 to -32099 for implementation-defined)
- [JSON-RPC Best Practices](https://json-rpc.dev/learn/best-practices) -- Dotted namespace convention, named parameters

### Secondary (MEDIUM confidence)

- [Squish Object Map Documentation](https://doc.qt.io/squish/object-map.html) -- Symbolic name architecture and benefits
- [Squish Object Identification](https://doc.qt.io/squish/how-to-identify-and-access-objects.html) -- Multi-style object referencing patterns
- Phase 2 RESEARCH.md -- Foundation patterns for introspection

### Tertiary (LOW confidence)

- Numeric ID performance with large object counts (needs profiling if >10k objects get IDs)
- qt.ping event loop latency measurement accuracy (may need calibration)

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH -- All components already exist in codebase; this is reorganization
- Architecture: HIGH -- Patterns derived directly from existing working code and locked CONTEXT.md decisions
- Pitfalls: HIGH -- Identified from code review of current implementation + API design experience
- Object referencing: MEDIUM -- Three-style resolution is new; numeric and symbolic need testing

**Research date:** 2026-01-31
**Valid until:** 2026-03-31 (60 days -- API design patterns are stable; this is architecture, not library versions)
