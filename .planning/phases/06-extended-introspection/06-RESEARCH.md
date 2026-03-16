# Phase 6: Extended Introspection - Research

**Researched:** 2026-02-01
**Domain:** Qt QML introspection and QAbstractItemModel navigation via public C++ APIs
**Confidence:** MEDIUM (some areas HIGH, QML type name stripping is LOW)

## Summary

Phase 6 extends the probe's introspection to cover two domains: (1) QML/Qt Quick items with their IDs, properties, and QML-specific metadata; and (2) QAbstractItemModel hierarchies with data retrieval. Both domains are well-served by Qt's public APIs. The CONTEXT.md decision to use "public API only" is achievable for all requirements except binding expressions and context properties, which were already declared out of scope.

The core approach is:
- **QML detection**: `qobject_cast<QQuickItem*>(obj)` detects visual QML items; `qmlContext(obj)` detects any QML-engine-created object.
- **QML id retrieval**: `QQmlContext::nameForObject(obj)` returns the QML `id` string — fully public API.
- **QML type name stripping**: Strip `QQuick` prefix from `metaObject()->className()` for anonymous items (e.g., `QQuickRectangle` -> `Rectangle`). No public API maps C++ class name to QML type name without private headers.
- **Model navigation**: `QAbstractItemModel` provides `rowCount()`, `columnCount()`, `data()`, `index()`, `parent()`, and `roleNames()` — all public and sufficient for full hierarchy traversal.

**Primary recommendation:** Add `Qt6::Qml` and `Qt6::Quick` as new library dependencies. Create two new source files: `qml_inspector.cpp` for QML metadata extraction and `model_navigator.cpp` for model data access. Extend `NativeModeApi` with new `qt.qml.*` and `qt.models.*` method groups. Modify `object_id.cpp` to use QML-aware ID segment generation.

## Standard Stack

### Core (new dependencies needed)

| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| Qt6::Qml | 6.x | QQmlContext, qmlContext(), qmlEngine() | Required for QML id retrieval and context access |
| Qt6::Quick | 6.x | QQuickItem, QQuickWindow | Required for QML item type detection and child traversal |

### Supporting (optional, for QQuickWidget boundary crossing)

| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| Qt6::QuickWidgets | 6.x | QQuickWidget::rootObject() | Only if target app embeds QML in widgets via QQuickWidget |

### Existing (already linked)

| Library | Purpose for Phase 6 |
|---------|---------------------|
| Qt6::Core | QAbstractItemModel, QMetaObject, QVariant |
| Qt6::Widgets | QAbstractItemView::model() for view-to-model resolution |
| Qt6::CorePrivate | Already used for qhooks_p.h (no new private deps needed) |

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Public QML API | QQmlPrivate/qqmlmetatype_p.h | Would give proper QML type names but breaks between Qt minor versions; CONTEXT.md forbids private headers |
| QQuickItem::childItems() | QObject::children() | childItems() is the visual tree; QObject::children() is the ownership tree. Both may be needed — childItems() for visual hierarchy, children() for non-visual QML objects |

**Installation (CMake changes needed):**
```cmake
# Add to root CMakeLists.txt find_package:
find_package(Qt6 COMPONENTS Qml Quick QUIET)

# Add to src/probe/CMakeLists.txt target_link_libraries:
if(Qt6Qml_FOUND AND Qt6Quick_FOUND)
    target_link_libraries(qtPilot_probe PUBLIC Qt6::Qml Qt6::Quick)
    target_compile_definitions(qtPilot_probe PUBLIC QTPILOT_HAS_QML)
endif()
```

## Architecture Patterns

### Recommended Project Structure

New/modified files:

```
src/probe/
├── introspection/
│   ├── object_id.cpp          # MODIFY: QML-aware ID segment generation
│   ├── qml_inspector.h        # NEW: QML metadata extraction
│   ├── qml_inspector.cpp      # NEW: QML metadata extraction
│   ├── model_navigator.h      # NEW: QAbstractItemModel navigation
│   └── model_navigator.cpp    # NEW: QAbstractItemModel navigation
├── api/
│   ├── native_mode_api.h      # MODIFY: Add registerQmlMethods(), registerModelMethods()
│   ├── native_mode_api.cpp    # MODIFY: New qt.qml.* and qt.models.* methods
│   └── error_codes.h          # MODIFY: Add QML and Model error codes
└── core/
    └── object_registry.cpp    # MINOR: Possibly modify findAllByClassName for inheritance
```

### Pattern 1: QML-Aware ID Segment Generation

**What:** Modify `generateIdSegment()` in `object_id.cpp` to detect QML items and use QML id or short type name.
**When to use:** Every time an object ID is generated for a QQuickItem subclass.

```cpp
// Source: Qt6 public API — QQmlContext::nameForObject, qmlContext()
#ifdef QTPILOT_HAS_QML
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#endif

QString generateIdSegment(QObject* obj) {
    if (!obj) return QString();

    // Priority 0 (NEW): QML id — replaces className per CONTEXT.md decision
#ifdef QTPILOT_HAS_QML
    if (qobject_cast<QQuickItem*>(obj)) {
        QQmlContext* ctx = qmlContext(obj);
        if (ctx) {
            QString qmlId = ctx->nameForObject(obj);
            if (!qmlId.isEmpty()) {
                return qmlId;  // Use QML id directly
            }
        }
        // No QML id — use short type name (strip QQuick prefix)
        QString className = QString::fromLatin1(obj->metaObject()->className());
        QString shortName = stripQmlPrefix(className);
        int sibIdx = getSiblingIndex(obj);
        if (sibIdx > 0)
            return shortName + QLatin1Char('#') + QString::number(sibIdx);
        return shortName;
    }
#endif

    // Existing logic for non-QML objects...
    // Priority 1: objectName
    // Priority 2: text property
    // Priority 3: ClassName#N
}
```

### Pattern 2: QML Type Name Stripping

**What:** Convert C++ class names like `QQuickRectangle` to QML-friendly names like `Rectangle`.
**When to use:** When a QML item has no `id` and needs a human-readable segment name.

```cpp
// Heuristic approach — no public API for C++ -> QML type name mapping
QString stripQmlPrefix(const QString& className) {
    // Built-in Qt Quick types: QQuickRectangle -> Rectangle
    if (className.startsWith(QLatin1String("QQuick"))) {
        return className.mid(6);  // len("QQuick") == 6
    }
    // QML-registered custom types may use other prefixes
    // Fall back to full class name
    return className;
}
```

**Confidence: MEDIUM.** This heuristic works for all built-in Qt Quick types (`QQuickRectangle`, `QQuickText`, `QQuickItem`, `QQuickListView`, etc.). Custom C++ types registered to QML may not follow this prefix convention, but their C++ class name is still useful.

### Pattern 3: Model Data Retrieval with Smart Pagination

**What:** Fetch model data with automatic pagination for large models.
**When to use:** When the agent calls `qt.models.data` on a model.

```cpp
// Source: Qt6 QAbstractItemModel public API
QJsonObject fetchModelData(QAbstractItemModel* model,
                           const QModelIndex& parent,
                           int offset, int limit,
                           const QList<int>& roles) {
    int totalRows = model->rowCount(parent);
    int totalCols = model->columnCount(parent);

    // Smart pagination: small models return all data
    if (limit <= 0 && totalRows <= 100) {
        offset = 0;
        limit = totalRows;
    } else if (limit <= 0) {
        limit = 100;  // Default page size
    }

    int endRow = qMin(offset + limit, totalRows);
    QJsonArray rows;
    for (int r = offset; r < endRow; ++r) {
        QJsonObject rowObj;
        for (int c = 0; c < totalCols; ++c) {
            QModelIndex idx = model->index(r, c, parent);
            QJsonObject cellData;
            for (int role : roles) {
                QVariant val = model->data(idx, role);
                cellData[roleIdToName(model, role)] = variantToJson(val);
            }
            rowObj[QString::number(c)] = cellData;
        }
        rows.append(rowObj);
    }

    QJsonObject result;
    result["rows"] = rows;
    result["totalRows"] = totalRows;
    result["totalColumns"] = totalCols;
    result["offset"] = offset;
    result["limit"] = limit;
    result["hasMore"] = (endRow < totalRows);
    return result;
}
```

### Pattern 4: View-to-Model Resolution

**What:** If agent passes a view's objectId, automatically resolve its model.
**When to use:** When `qt.models.*` methods receive an objectId.

```cpp
// Source: Qt6 QAbstractItemView::model() public API
QAbstractItemModel* resolveModel(QObject* obj) {
    // Direct model
    if (auto* model = qobject_cast<QAbstractItemModel*>(obj))
        return model;

    // Widget view -> model
    if (auto* view = qobject_cast<QAbstractItemView*>(obj))
        return view->model();

    // QML view types don't have a common base with model() in public API
    // Try property "model" via QObject::property()
    QVariant modelProp = obj->property("model");
    if (modelProp.isValid()) {
        QObject* modelObj = modelProp.value<QObject*>();
        if (auto* model = qobject_cast<QAbstractItemModel*>(modelObj))
            return model;
    }

    return nullptr;
}
```

### Pattern 5: QML Metadata in Tree Serialization

**What:** Add QML-specific fields to `serializeObjectInfo()` output.
**When to use:** When serializing any QQuickItem in the object tree.

```cpp
// Extend serializeObjectInfo() in object_id.cpp
#ifdef QTPILOT_HAS_QML
if (auto* quickItem = qobject_cast<QQuickItem*>(obj)) {
    result["isQmlItem"] = true;

    QQmlContext* ctx = qmlContext(obj);
    if (ctx) {
        QString qmlId = ctx->nameForObject(obj);
        if (!qmlId.isEmpty()) {
            result["qmlId"] = qmlId;
        }
        QUrl baseUrl = ctx->baseUrl();
        if (baseUrl.isValid()) {
            result["qmlFile"] = baseUrl.toLocalFile().isEmpty()
                ? baseUrl.toString() : baseUrl.toLocalFile();
        }
    }
}
#endif
```

### Anti-Patterns to Avoid

- **Using private Qt QML headers (qqmldata_p.h, qqmlmetatype_p.h, qqmltype_p.h):** These break between Qt minor versions. The CONTEXT.md explicitly forbids this. All requirements can be met with public API.
- **Separate QML tree endpoint:** CONTEXT.md decided on a unified tree. QML items appear naturally in `QObject::children()`. Do NOT create a separate `qt.qml.tree` method.
- **Iterating all tracked objects to find models:** Use the existing `ObjectRegistry::findAllByClassName()` but note it uses exact class name matching. Models subclass `QAbstractItemModel` — you need inheritance-aware search. Use `qobject_cast<QAbstractItemModel*>(obj)` on `allObjects()` instead.
- **Caching model data:** Models are mutable (rows can be added/removed). Never cache model data — always fetch fresh on each request.
- **Blocking on model operations:** Some models (e.g., SQL-backed) may have slow data() calls. The existing architecture uses synchronous handlers, so keep this consistent but document the risk.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| QML id lookup | String scanning of QML source files | `QQmlContext::nameForObject(obj)` | Qt manages the id -> object mapping internally |
| QML context detection | Checking class name prefixes | `qmlContext(obj) != nullptr` | Public API, handles all QML engine scenarios |
| QQuickItem detection | Checking `metaObject()->className()` startsWith "QQuick" | `qobject_cast<QQuickItem*>(obj)` | Type-safe, works with subclasses |
| Model role name lookup | Hardcoded role ID tables | `model->roleNames()` | Returns the actual `QHash<int, QByteArray>` mapping at runtime |
| View-to-model resolution | Custom lookup table of view types | `QAbstractItemView::model()` + property("model") fallback | Covers all widget views and most QML views |
| QVariant to JSON for model data | Custom conversion | Existing `variantToJson()` in variant_json.cpp | Already handles all common Qt types |

**Key insight:** Qt's public API already provides all the building blocks for QML introspection (QQmlContext, qmlContext, QQuickItem) and model navigation (QAbstractItemModel). The implementation is primarily wiring these existing APIs into the JSON-RPC surface.

## Common Pitfalls

### Pitfall 1: QQmlContext::nameForObject() Returns Empty for Wrong Context

**What goes wrong:** You call `nameForObject(obj)` on a parent context, but the QML id was declared in a child context (e.g., inside a Loader or Component). Returns empty string.
**Why it happens:** QML ids are scoped to the context where they are declared. An object can appear in multiple contexts.
**How to avoid:** Use `qmlContext(obj)` to get the object's own context, not a parent or root context. This returns the context where the object was actually instantiated.
**Warning signs:** QML items with known ids showing up with empty qmlId in the tree output.

### Pitfall 2: QObject::children() vs QQuickItem::childItems() Discrepancy

**What goes wrong:** The visual tree (childItems()) and the QObject ownership tree (children()) can differ. Some QML items may be visual children but not QObject children, or vice versa.
**Why it happens:** QQuickItem has separate visual parent (parentItem) and QObject parent concepts. Reparenting in QML only changes the visual parent.
**How to avoid:** For Phase 6, continue using `QObject::children()` as the primary tree structure (matching existing ObjectRegistry behavior). This is the correct choice because the ObjectRegistry hooks track QObject creation/destruction, not visual parenting. Add `parentItem` info to QML item metadata for agents that need visual tree awareness.
**Warning signs:** Items appearing in unexpected tree positions; missing items in subtrees.

### Pitfall 3: Model roleNames() Can Be Empty

**What goes wrong:** Calling `roleNames()` on models that don't override it returns the default Qt roles only (Display, Edit, etc. — about 15 standard roles).
**Why it happens:** `QAbstractItemModel::roleNames()` has a default implementation returning standard role names. Custom models override this to expose custom roles.
**How to avoid:** Always fall back to standard Qt roles when `roleNames()` returns the default set. Expose both the role name string and the integer role ID so the agent can request data by either.
**Warning signs:** Model data requests returning nothing when role names aren't found.

### Pitfall 4: QModelIndex Validity and Parent Navigation

**What goes wrong:** Creating `QModelIndex` with invalid row/column causes undefined behavior or crashes. Calling `parent()` on an invalid index returns an invalid index.
**Why it happens:** `model->index(row, col, parent)` requires `row < rowCount(parent)` and `col < columnCount(parent)`.
**How to avoid:** Always validate row and column against `rowCount()` and `columnCount()` before creating indices. Return clear error messages for out-of-bounds requests.
**Warning signs:** Crashes or empty data for valid-looking requests.

### Pitfall 5: Qt Quick Module Not Installed

**What goes wrong:** Build fails because `Qt6::Qml` or `Qt6::Quick` is not found by CMake.
**Why it happens:** Target Qt installation may be widgets-only (no Qt Quick). Server environments commonly lack Qt Quick.
**How to avoid:** Make Qt Quick/QML an optional dependency with compile-time feature guard (`QTPILOT_HAS_QML`). All QML-specific code guarded by `#ifdef QTPILOT_HAS_QML`. Model/View support (QAbstractItemModel) is in Qt Core and always available.
**Warning signs:** CMake warnings about missing Qt6::Qml or Qt6::Quick.

### Pitfall 6: Model Data Fetching Performance

**What goes wrong:** Fetching all data from a large model (100k+ rows) blocks the event loop and freezes the target application.
**Why it happens:** `model->data()` is called synchronously for each cell. Proxy models may trigger expensive recomputation.
**How to avoid:** Smart pagination (100-row default page size). Return `totalRows` and `hasMore` so the agent knows to paginate. Never fetch all rows for large models.
**Warning signs:** UI freezes when agent queries model data.

## Code Examples

### Detecting QML Items and Extracting Metadata

```cpp
// Source: Qt6 public API — QQmlContext, QQuickItem
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>

struct QmlItemInfo {
    bool isQmlItem = false;
    QString qmlId;
    QString qmlFile;
    QString shortTypeName;
};

QmlItemInfo inspectQmlItem(QObject* obj) {
    QmlItemInfo info;

    QQuickItem* quickItem = qobject_cast<QQuickItem*>(obj);
    if (!quickItem)
        return info;

    info.isQmlItem = true;

    // Short type name: strip QQuick prefix
    QString className = QString::fromLatin1(obj->metaObject()->className());
    if (className.startsWith(QLatin1String("QQuick")))
        info.shortTypeName = className.mid(6);
    else
        info.shortTypeName = className;

    // QML id and source file
    QQmlContext* ctx = qmlContext(obj);
    if (ctx) {
        info.qmlId = ctx->nameForObject(obj);
        QUrl baseUrl = ctx->baseUrl();
        if (baseUrl.isValid())
            info.qmlFile = baseUrl.toLocalFile().isEmpty()
                ? baseUrl.toString() : baseUrl.toLocalFile();
    }

    return info;
}
```

### Discovering All QAbstractItemModel Instances

```cpp
// Source: Qt6 QAbstractItemModel, ObjectRegistry
QJsonArray listAllModels() {
    QJsonArray result;
    QList<QObject*> allObjs = ObjectRegistry::instance()->allObjects();
    for (QObject* obj : allObjs) {
        auto* model = qobject_cast<QAbstractItemModel*>(obj);
        if (!model)
            continue;

        QString objId = ObjectRegistry::instance()->objectId(obj);
        int numId = ObjectResolver::assignNumericId(obj);
        QJsonObject entry;
        entry["objectId"] = objId;
        entry["className"] = QString::fromLatin1(obj->metaObject()->className());
        entry["numericId"] = numId;
        entry["rowCount"] = model->rowCount();
        entry["columnCount"] = model->columnCount();

        // Include role names
        QHash<int, QByteArray> roles = model->roleNames();
        QJsonObject rolesObj;
        for (auto it = roles.constBegin(); it != roles.constEnd(); ++it) {
            rolesObj[QString::number(it.key())] = QString::fromLatin1(it.value());
        }
        entry["roleNames"] = rolesObj;

        result.append(entry);
    }
    return result;
}
```

### Navigating Model Hierarchy (Tree Models)

```cpp
// Source: Qt6 QAbstractItemModel public API
QJsonObject getModelChildren(QAbstractItemModel* model,
                             int parentRow, int parentCol,
                             const QModelIndex& grandparent) {
    // Build parent index
    QModelIndex parentIdx;
    if (parentRow >= 0) {
        parentIdx = model->index(parentRow, parentCol, grandparent);
        if (!parentIdx.isValid()) {
            // Return error: invalid parent index
        }
    }

    int rows = model->rowCount(parentIdx);
    int cols = model->columnCount(parentIdx);
    bool hasChildren = model->hasChildren(parentIdx);

    QJsonObject result;
    result["rowCount"] = rows;
    result["columnCount"] = cols;
    result["hasChildren"] = hasChildren;
    // ... fetch data with pagination ...
    return result;
}
```

### Role Name Resolution

```cpp
// Source: Qt6 QAbstractItemModel::roleNames()
int resolveRoleName(QAbstractItemModel* model, const QString& roleName) {
    // Check custom roles first
    QHash<int, QByteArray> roles = model->roleNames();
    for (auto it = roles.constBegin(); it != roles.constEnd(); ++it) {
        if (QString::fromLatin1(it.value()) == roleName)
            return it.key();
    }

    // Check standard Qt roles by name
    static const QHash<QString, int> standardRoles = {
        {"display", Qt::DisplayRole},
        {"edit", Qt::EditRole},
        {"decoration", Qt::DecorationRole},
        {"toolTip", Qt::ToolTipRole},
        {"statusTip", Qt::StatusTipRole},
        {"whatsThis", Qt::WhatsThisRole},
    };
    auto it = standardRoles.find(roleName.toLower());
    if (it != standardRoles.end())
        return it.value();

    return -1;  // Not found
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| QQmlPrivate for type introspection | Public QQmlContext + QMetaObject | Always available | Private APIs break between minor releases; public API is stable |
| QDeclarativeView (Qt4/early Qt5) | QQuickView / QQuickWidget (Qt5+) | Qt 5.0 | All modern QML uses Qt Quick 2 |
| Manual role ID tracking | QAbstractItemModel::roleNames() | Qt 5.0 | roleNames() provides runtime-discoverable role mapping |
| Context properties for C++/QML bridging | QML_ELEMENT / singletons | Qt 5.15+ / Qt 6.x | Context properties are deprecated; but contextProperty() API still works for reading |

**Deprecated/outdated:**
- `QDeclarativeView` / `QDeclarativeEngine`: Replaced by QQuickView / QQmlEngine in Qt 5.0
- `qmlRegisterType()`: Replaced by declarative registration (QML_ELEMENT macro) in Qt 5.15+, but runtime registration still works
- Context properties: Deprecated in favor of singletons, but `QQmlContext::contextProperty()` / `nameForObject()` remain public API

## Open Questions

1. **Custom QML type name stripping logic**
   - What we know: Stripping `QQuick` prefix works for all built-in Qt Quick types
   - What's unclear: Custom QML types registered via QML_ELEMENT may have arbitrary C++ class names (e.g., `MyApp::CustomButton` -> `CustomButton` in QML). Without private `QQmlMetaType` API, we cannot look up the registered QML type name.
   - Recommendation: Start with `QQuick` prefix stripping. For custom types, fall back to the full C++ class name. The `qmlId` will be the primary identifier anyway — the type name is secondary. This can be enhanced later if needed.

2. **QML views (ListView, GridView, etc.) model() resolution**
   - What we know: `QAbstractItemView::model()` works for widget views. QML views (ListView, TreeView) are QQuickItem subclasses and don't inherit from QAbstractItemView.
   - What's unclear: Whether `QObject::property("model")` reliably returns the QAbstractItemModel* from QML views. QML models can be JavaScript arrays or ListModel (not QAbstractItemModel).
   - Recommendation: Use `QObject::property("model")` and try `qobject_cast<QAbstractItemModel*>`. If the cast fails, the view uses a non-C++ model — return an appropriate error.

3. **Thread safety of QML context access**
   - What we know: QML contexts belong to the QML engine's thread. The probe's JSON-RPC handler may call from a different thread.
   - What's unclear: Whether `qmlContext(obj)` is safe to call from any thread.
   - Recommendation: Ensure all QML introspection calls happen on the main/GUI thread using `QMetaObject::invokeMethod` with `Qt::BlockingQueuedConnection` if needed. The existing `NativeModeApi` handlers already run on the main thread (WebSocket events are dispatched there), so this may not be an issue in practice.

4. **Qt5 fallback for QML features**
   - What we know: The project supports Qt5 fallback. Qt5 has QQmlContext::nameForObject(), QQuickItem, etc.
   - What's unclear: Whether all the APIs used are available in Qt 5.15.
   - Recommendation: Qt 5.15 supports all the public APIs discussed here. Guard with `#ifdef QTPILOT_HAS_QML` (compile-time check for Qt Qml/Quick availability), not Qt version checks. The only difference is the `objectForName()` method was added in Qt 6.2 — avoid using it; use `nameForObject()` which exists in both Qt5 and Qt6.

## Sources

### Primary (HIGH confidence)
- [QQmlContext Class - Qt 6.10](https://doc.qt.io/qt-6/qqmlcontext.html) — nameForObject(), contextProperty(), baseUrl()
- [QQuickItem Class - Qt 6.10](https://doc.qt.io/qt-6/qquickitem.html) — childItems(), parentItem(), window()
- [QAbstractItemModel Class - Qt 6.10](https://doc.qt.io/qt-6/qabstractitemmodel.html) — rowCount(), data(), index(), roleNames()
- [QAbstractItemView Class - Qt 6.10](https://doc.qt.io/qt-6/qabstractitemview.html) — model() method
- [Interacting with QML from C++ - Qt 6.10](https://doc.qt.io/qt-6/qtqml-cppintegration-interactqmlfromcpp.html) — findChild, qmlContext, qmlEngine
- [QQmlEngine Class - Qt 6.10](https://doc.qt.io/qt-6/qqmlengine.html) — contextForObject()
- [QQuickWidget Class - Qt 6.10](https://doc.qt.io/qt-6/qquickwidget.html) — rootObject()
- [CMake Build QML Application - Qt 6.10](https://doc.qt.io/qt-6/cmake-build-qml-application.html) — find_package targets

### Secondary (MEDIUM confidence)
- [Qt Forum: How to get QML id in C++](https://forum.qt.io/topic/161297/how-to-get-the-qml-object-s-id-in-the-c-class-which-defined-for-qml) — Confirmed nameForObject approach
- [Qt 6 QML Book - Models in C++](https://www.qt.io/product/qt6/qml-book/ch17-qtcpp-cpp-models) — Model implementation patterns
- [DMC: Using QAbstractListModel in QML](https://www.dmcinfo.com/latest-thinking/blog/id/10428/using-a-qabstractlistmodel-in-qml) — roleNames() patterns

### Tertiary (LOW confidence)
- QML type name stripping heuristic (QQuick prefix) — based on observation of Qt source naming conventions, not officially documented
- Thread safety of qmlContext() — inferred from Qt threading model, not explicitly documented

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — Qt public APIs well-documented, all methods verified in official docs
- Architecture: MEDIUM — Integration patterns are clear but QML type name stripping is heuristic
- Pitfalls: MEDIUM — Based on Qt documentation and general Qt development experience
- Model navigation: HIGH — QAbstractItemModel is one of Qt's most stable and documented APIs

**Research date:** 2026-02-01
**Valid until:** 2026-03-01 (30 days — Qt public API is very stable)
