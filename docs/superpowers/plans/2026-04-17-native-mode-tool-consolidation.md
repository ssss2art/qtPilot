# Native-Mode Tool Consolidation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Consolidate qtPilot's native-mode tool surface from 50 tools down to 39 by merging overlapping tools into canonical ones, standardising names and response shapes, and normalising action responses to `{ok: true}`.

**Architecture:** Pre-1.0 hard break — no backward compatibility. Changes are made surgically per tool with TDD: write the new test first (using the new tool name / new shape), verify failure, implement, verify pass, commit. Killed tools are removed only after their replacement is proven. Follows the spec in `docs/superpowers/specs/2026-04-17-native-mode-tool-consolidation-design.md`.

**Tech Stack:** C++17 / Qt 6 (probe side), Python 3 / FastMCP (server side), Qt Test + pytest.

**Spec reference:** `docs/superpowers/specs/2026-04-17-native-mode-tool-consolidation-design.md` — read this first if unclear on any design decision.

---

## Build & test reference

All C++ tests:
```bash
QT_DIR=$(grep "QTPILOT_QT_DIR:PATH=" build/CMakeCache.txt | cut -d= -f2)
cmd //c "set PATH=${QT_DIR}\\bin;%PATH% && set QT_PLUGIN_PATH=${QT_DIR}\\plugins && ctest --test-dir build -C Release --output-on-failure"
```

One C++ test binary:
```bash
cmake --build build --config Release --target test_native_mode_api
./build/bin/Release/test_native_mode_api.exe
```

Python tests:
```bash
cd python && python -m pytest tests/ -v
```

Single Python test:
```bash
cd python && python -m pytest tests/test_tools.py::TestToolManifest::test_native_tools_registered -v
```

Build only:
```bash
cmake --build build --config Release
```

---

## File Structure

**Files that will be modified:**

- `src/probe/api/error_codes.h` — add `kInvalidField` constant.
- `src/probe/api/native_mode_api.cpp` — the bulk of C++ changes: remove killed method registrations, add new ones, rewrite `qt.objects.inspect`, normalise all `success:true` action responses to `ok:true`.
- `python/src/qtpilot/tools/native.py` — remove Python wrappers for killed tools, add wrappers for new tools, update renamed tools.
- `python/src/qtpilot/tools/discovery_tools.py` — replace `qtpilot_list_probes`, `qtpilot_get_mode`, `qtPilot_probe_status` with single `qtpilot_status`. Normalise connect/disconnect/set_mode responses.
- `python/src/qtpilot/tools/logging_tools.py` — extend `qtpilot_log_status` with `tail` parameter; remove `qtpilot_log_tail`.
- `python/src/qtpilot/tools/recording_tools.py` — rename `qtpilot_start_recording` / `qtpilot_stop_recording` to `qtpilot_recording_start` / `qtpilot_recording_stop`.
- `python/tests/test_tools.py` — update tool-name manifest.
- `python/tests/test_mode_switching.py` — migrate away from `qtpilot_get_mode` / `qtpilot_list_probes`.
- `python/tests/test_logging_tools.py` — migrate away from `qtpilot_log_tail`.
- `python/tests/test_recording_tools.py` — update renamed tool names.
- `tests/test_native_mode_api.cpp` — rewrite tests for killed / renamed JSON-RPC methods; add tests for new behaviour.
- `tests/test_model_navigator.cpp` — update `qt.models.find` → `qt.models.search`.
- `CLAUDE.md` — update "Using qtPilot Tools" examples (lines ~224-232).
- `mcp-diagram.html` — update tool list to reflect new surface.

**Files that will NOT be modified:**
- `src/probe/introspection/meta_inspector.{h,cpp}` — the helpers (`objectInfo`, `listProperties`, `listMethods`, `listSignals`, `getProperty`, `setProperty`) are reused verbatim.
- `src/probe/introspection/object_id.{h,cpp}` — unchanged by this plan. The paging plan (follow-up spec) modifies these.
- `src/probe/api/chrome_mode_api.cpp` / `cu_mode_api.cpp` — out of scope.

---

## Phase 1: Infrastructure — error code

### Task 1: Add `kInvalidField` error code

**Files:**
- Modify: `src/probe/api/error_codes.h`

- [ ] **Step 1: Open error_codes.h and add the new error code**

Add `kInvalidField` in the appropriate range. Looking at the existing layout, there's a gap after "Object errors (-32001 to -32009)" — we'll use `-32004`. Modify `src/probe/api/error_codes.h`:

```cpp
// Object errors (-32001 to -32009)
constexpr int kObjectNotFound = -32001;
constexpr int kObjectStale = -32002;
constexpr int kObjectNotWidget = -32003;
constexpr int kInvalidField = -32004;  // Unknown value for a `parts` / `fields` / similar enum param
```

- [ ] **Step 2: Verify the file compiles**

```bash
cmake --build build --config Release
```

Expected: build succeeds (the constant is unused, but defined).

- [ ] **Step 3: Commit**

```bash
git add src/probe/api/error_codes.h
git commit -m "feat(errors): add kInvalidField error code for parts/fields enum validation"
```

---

## Phase 2: New canonical tools

### Task 2: `qt.objects.search` — probe registration

Replaces `qt.objects.find`, `qt.objects.findByClass`, `qt.objects.query`.

**Files:**
- Modify: `src/probe/api/native_mode_api.cpp` — add registration near existing `qt.objects.query` (line ~278).
- Modify: `tests/test_native_mode_api.cpp` — add test cases.

- [ ] **Step 1: Write failing test for basic search by className**

Add to `tests/test_native_mode_api.cpp` (alongside existing object tests):

```cpp
void TestNativeModeApi::testObjectsSearchByClassName() {
  // Create a test widget to search for
  auto* widget = new QPushButton(QStringLiteral("test"));
  widget->setObjectName(QStringLiteral("searchTarget"));
  m_testObjects.append(widget);

  QJsonObject params;
  params[QStringLiteral("className")] = QStringLiteral("QPushButton");
  QJsonValue result = callResult("qt.objects.search", params);

  QVERIFY(result.isObject());
  QJsonObject obj = result.toObject();
  QVERIFY(obj.contains(QStringLiteral("objects")));
  QVERIFY(obj.contains(QStringLiteral("count")));
  QVERIFY(obj.contains(QStringLiteral("truncated")));
  QJsonArray objects = obj[QStringLiteral("objects")].toArray();
  QVERIFY(objects.size() >= 1);

  // Find our test widget in results
  bool found = false;
  for (const auto& v : objects) {
    QJsonObject entry = v.toObject();
    if (entry[QStringLiteral("objectName")].toString() == QStringLiteral("searchTarget")) {
      found = true;
      QCOMPARE(entry[QStringLiteral("className")].toString(), QStringLiteral("QPushButton"));
      QVERIFY(entry.contains(QStringLiteral("objectId")));
      QVERIFY(entry.contains(QStringLiteral("numericId")));
      break;
    }
  }
  QVERIFY(found);
}
```

Also add the declaration to the class in the test file (look for other `testObjects...` declarations and add alongside).

- [ ] **Step 2: Run test, verify it fails**

```bash
cmake --build build --config Release --target test_native_mode_api
./build/bin/Release/test_native_mode_api.exe -functions testObjectsSearchByClassName
```

Expected: FAIL with "Method not found: qt.objects.search" (or similar JSON-RPC error-code mismatch).

- [ ] **Step 3: Implement qt.objects.search registration**

In `src/probe/api/native_mode_api.cpp`, add the following registration just BEFORE the closing brace of `registerObjectMethods()` (after `qt.objects.query`, around line 329):

```cpp
  // qt.objects.search - unified discovery: objectName / className / properties filters
  m_handler->RegisterMethod(
      QStringLiteral("qt.objects.search"), [](const QString& params) -> QString {
        auto p = parseParams(params);
        QString objectName = p[QStringLiteral("objectName")].toString();
        QString className = p[QStringLiteral("className")].toString();
        QJsonObject propFilters = p[QStringLiteral("properties")].toObject();
        QString rootId = p[QStringLiteral("root")].toString();
        int limit = p.contains(QStringLiteral("limit")) ? p[QStringLiteral("limit")].toInt() : 50;

        // Require at least one filter
        if (objectName.isEmpty() && className.isEmpty() && propFilters.isEmpty()) {
          throw JsonRpcException(
              JsonRpcError::kInvalidParams,
              QStringLiteral("Specify at least one of objectName, className, properties"),
              QJsonObject{{QStringLiteral("method"), QStringLiteral("qt.objects.search")}});
        }
        if (limit < 0) {
          throw JsonRpcException(
              JsonRpcError::kInvalidParams, QStringLiteral("limit must be >= 0"),
              QJsonObject{{QStringLiteral("method"), QStringLiteral("qt.objects.search")}});
        }

        QObject* rootObj = nullptr;
        if (!rootId.isEmpty()) {
          rootObj = ObjectResolver::resolve(rootId);
        }

        // Candidate set: filtered by className if present, else all objects
        QList<QObject*> candidates;
        if (!className.isEmpty()) {
          candidates = ObjectRegistry::instance()->findAllByClassName(className, rootObj);
        } else {
          candidates = ObjectRegistry::instance()->allObjects();
          if (rootObj) {
            // Filter to descendants of rootObj
            QList<QObject*> filtered;
            for (QObject* obj : candidates) {
              QObject* parent = obj;
              while (parent && parent != rootObj) parent = parent->parent();
              if (parent == rootObj) filtered.append(obj);
            }
            candidates = filtered;
          }
        }

        QJsonArray matches;
        bool truncated = false;
        for (QObject* obj : candidates) {
          // objectName filter
          if (!objectName.isEmpty() && obj->objectName() != objectName) {
            continue;
          }
          // property filters
          if (!propFilters.isEmpty()) {
            bool ok = true;
            for (auto it = propFilters.constBegin(); it != propFilters.constEnd(); ++it) {
              try {
                QJsonValue actual = MetaInspector::getProperty(obj, it.key());
                if (actual != it.value()) {
                  ok = false;
                  break;
                }
              } catch (...) {
                ok = false;
                break;
              }
            }
            if (!ok) continue;
          }

          if (matches.size() >= limit) {
            truncated = true;
            break;
          }

          QString objId = ObjectRegistry::instance()->objectId(obj);
          int numId = ObjectResolver::assignNumericId(obj);
          QJsonObject entry;
          entry[QStringLiteral("objectId")] = objId;
          entry[QStringLiteral("className")] = QString::fromUtf8(obj->metaObject()->className());
          entry[QStringLiteral("objectName")] = obj->objectName();
          entry[QStringLiteral("numericId")] = numId;
          matches.append(entry);
        }

        QJsonObject result;
        result[QStringLiteral("objects")] = matches;
        result[QStringLiteral("count")] = matches.size();
        result[QStringLiteral("truncated")] = truncated;
        return envelopeToString(ResponseEnvelope::wrap(result));
      });
```

- [ ] **Step 4: Run test, verify it passes**

```bash
cmake --build build --config Release --target test_native_mode_api
./build/bin/Release/test_native_mode_api.exe -functions testObjectsSearchByClassName
```

Expected: PASS.

- [ ] **Step 5: Write and pass additional test cases — objectName, properties, combined filters, no-filter error, limit truncation**

Add these test methods to `tests/test_native_mode_api.cpp`:

```cpp
void TestNativeModeApi::testObjectsSearchByObjectName() {
  auto* w = new QLabel(QStringLiteral("hello"));
  w->setObjectName(QStringLiteral("uniqueLabel42"));
  m_testObjects.append(w);

  QJsonObject params;
  params[QStringLiteral("objectName")] = QStringLiteral("uniqueLabel42");
  QJsonValue result = callResult("qt.objects.search", params);

  QJsonArray objects = result.toObject()[QStringLiteral("objects")].toArray();
  QCOMPARE(objects.size(), 1);
  QCOMPARE(objects[0].toObject()[QStringLiteral("objectName")].toString(),
           QStringLiteral("uniqueLabel42"));
}

void TestNativeModeApi::testObjectsSearchByProperties() {
  auto* w = new QPushButton(QStringLiteral("Go"));
  w->setObjectName(QStringLiteral("propTestBtn"));
  w->setEnabled(false);
  m_testObjects.append(w);

  QJsonObject params;
  params[QStringLiteral("className")] = QStringLiteral("QPushButton");
  QJsonObject props;
  props[QStringLiteral("enabled")] = false;
  params[QStringLiteral("properties")] = props;
  QJsonValue result = callResult("qt.objects.search", params);

  QJsonArray objects = result.toObject()[QStringLiteral("objects")].toArray();
  bool found = false;
  for (const auto& v : objects) {
    if (v.toObject()[QStringLiteral("objectName")].toString() == QStringLiteral("propTestBtn")) {
      found = true;
      break;
    }
  }
  QVERIFY(found);
}

void TestNativeModeApi::testObjectsSearchNoFiltersIsError() {
  QJsonObject params;  // intentionally empty
  QJsonValue error = callError("qt.objects.search", params);
  QCOMPARE(error.toObject()[QStringLiteral("code")].toInt(), int(JsonRpcError::kInvalidParams));
}

void TestNativeModeApi::testObjectsSearchLimitTruncation() {
  // Create 5 labels matching a unique class filter, ask for limit=2
  for (int i = 0; i < 5; ++i) {
    auto* lbl = new QLabel(QStringLiteral("truncTest"));
    lbl->setObjectName(QStringLiteral("truncLabel_%1").arg(i));
    m_testObjects.append(lbl);
  }
  QJsonObject params;
  params[QStringLiteral("objectName")] = QStringLiteral("truncLabel_0");  // matches 1
  params[QStringLiteral("limit")] = 0;
  QJsonValue result = callResult("qt.objects.search", params);
  // limit=0 means no results kept, but something matched → truncated:true
  QJsonObject obj = result.toObject();
  QCOMPARE(obj[QStringLiteral("count")].toInt(), 0);
  QCOMPARE(obj[QStringLiteral("truncated")].toBool(), true);
}
```

Declare them in the class header alongside `testObjectsSearchByClassName`. (Tests in this file use Qt Test's slot-based declaration; add `void testObjectsSearchByObjectName();` etc. inside the `private slots:` section.)

If `callError` helper doesn't exist in this test file, look for how existing tests verify JSON-RPC error responses (check existing tests that test error paths — there should be a pattern). If there's no helper, skip the `NoFiltersIsError` test temporarily and open an issue; the production code still throws the exception.

- [ ] **Step 6: Build, run tests, fix failures**

```bash
cmake --build build --config Release --target test_native_mode_api
./build/bin/Release/test_native_mode_api.exe
```

Expected: all four new tests PASS. If any fail, fix the implementation or test.

- [ ] **Step 7: Commit**

```bash
git add src/probe/api/native_mode_api.cpp tests/test_native_mode_api.cpp
git commit -m "feat(native): add qt.objects.search unified discovery method"
```

---

### Task 3: `qt.objects.search` — Python wrapper

**Files:**
- Modify: `python/src/qtpilot/tools/native.py` — add `qt_objects_search` tool.
- Modify: `python/tests/test_tools.py` — add to manifest.

- [ ] **Step 1: Write failing manifest test**

In `python/tests/test_tools.py`, find the `tool_names` set assertion (around line 33 per earlier exploration) and add `"qt_objects_search"` to the expected set. Run:

```bash
cd python && python -m pytest tests/test_tools.py -v
```

Expected: FAIL — `qt_objects_search` not found among registered tools.

- [ ] **Step 2: Implement Python wrapper**

In `python/src/qtpilot/tools/native.py`, find an existing `qt_objects_*` wrapper as a template (likely `qt_objects_query`). Add the new tool next to it:

```python
    @mcp.tool
    async def qt_objects_search(
        objectName: str | None = None,
        className: str | None = None,
        properties: dict | None = None,
        root: str | None = None,
        limit: int | None = None,
        ctx: Context = None,
    ) -> dict:
        """Discover objects by name, class, and/or property filters.

        At least one of `objectName`, `className`, or `properties` must be provided.
        Returns a uniform envelope with `objects`, `count`, `truncated`.

        To discover which property names are available for filtering, call
        qt_objects_inspect(objectId=X, parts=["properties"]) on a sample instance.

        Args:
            objectName: Exact match on QObject::objectName()
            className: Exact match (subclass-aware) on metaObject()->className()
            properties: Property-value filters; every listed property must equal the given value
            root: Restrict search to this subtree
            limit: Maximum matches returned (default 50)

        Example: qt_objects_search(className="QPushButton", properties={"enabled": True})
        """
        params: dict = {}
        if objectName is not None: params["objectName"] = objectName
        if className is not None: params["className"] = className
        if properties is not None: params["properties"] = properties
        if root is not None: params["root"] = root
        if limit is not None: params["limit"] = limit
        return await _call_probe(ctx, "qt.objects.search", params)
```

(The helper name `_call_probe` may differ; match the pattern used by neighbouring `qt_objects_*` wrappers in the same file.)

- [ ] **Step 3: Run manifest test, verify it passes**

```bash
cd python && python -m pytest tests/test_tools.py -v
```

Expected: PASS.

- [ ] **Step 4: Commit**

```bash
git add python/src/qtpilot/tools/native.py python/tests/test_tools.py
git commit -m "feat(python): add qt_objects_search tool wrapper"
```

---

### Task 4: Rewrite `qt.objects.inspect` with `parts` parameter

Replaces `qt.objects.info`, `qt.properties.list`, `qt.methods.list`, `qt.signals.list`, `qt.qml.inspect`, `qt.models.info` when accessed through inspect.

**Files:**
- Modify: `src/probe/api/native_mode_api.cpp:263` — rewrite the existing `qt.objects.inspect` registration.
- Modify: `tests/test_native_mode_api.cpp` — add new test cases, remove/update old ones.

- [ ] **Step 1: Write failing test for default parts=["info"] only**

Add to `tests/test_native_mode_api.cpp`:

```cpp
void TestNativeModeApi::testInspectDefaultInfoOnly() {
  auto* w = new QLabel(QStringLiteral("inspTest"));
  w->setObjectName(QStringLiteral("inspTarget"));
  m_testObjects.append(w);
  QString id = ObjectRegistry::instance()->objectId(w);

  QJsonObject params;
  params[QStringLiteral("objectId")] = id;
  QJsonValue result = callResult("qt.objects.inspect", params);

  QJsonObject obj = result.toObject();
  QVERIFY(obj.contains(QStringLiteral("info")));
  QVERIFY(!obj.contains(QStringLiteral("properties")));
  QVERIFY(!obj.contains(QStringLiteral("methods")));
  QVERIFY(!obj.contains(QStringLiteral("signals")));
  QVERIFY(!obj.contains(QStringLiteral("qml")));
  QVERIFY(!obj.contains(QStringLiteral("geometry")));
  QVERIFY(!obj.contains(QStringLiteral("model")));
}
```

- [ ] **Step 2: Run test, verify it fails**

```bash
cmake --build build --config Release --target test_native_mode_api
./build/bin/Release/test_native_mode_api.exe -functions testInspectDefaultInfoOnly
```

Expected: FAIL — current inspect returns all four sections unconditionally.

- [ ] **Step 3: Rewrite qt.objects.inspect registration**

Replace the current registration at `src/probe/api/native_mode_api.cpp:261-275` with:

```cpp
  // qt.objects.inspect - consolidated read-only introspection with parts selector
  m_handler->RegisterMethod(
      QStringLiteral("qt.objects.inspect"), [](const QString& params) -> QString {
        auto p = parseParams(params);
        QObject* obj = resolveObjectParam(p, QStringLiteral("qt.objects.inspect"));
        QString objectId = p[QStringLiteral("objectId")].toString();

        // Parse parts: accept string (preset) or array of part names
        QStringList requestedParts;
        static const QStringList allParts = {
            QStringLiteral("info"),       QStringLiteral("properties"),
            QStringLiteral("methods"),    QStringLiteral("signals"),
            QStringLiteral("qml"),        QStringLiteral("geometry"),
            QStringLiteral("model")};

        QJsonValue partsValue = p[QStringLiteral("parts")];
        if (partsValue.isUndefined() || partsValue.isNull()) {
          requestedParts << QStringLiteral("info");
        } else if (partsValue.isString()) {
          QString s = partsValue.toString();
          if (s == QStringLiteral("all")) {
            requestedParts = allParts;
          } else {
            requestedParts << s;
          }
        } else if (partsValue.isArray()) {
          for (const auto& v : partsValue.toArray()) {
            requestedParts << v.toString();
          }
        }

        // Validate each requested part
        for (const QString& part : requestedParts) {
          if (!allParts.contains(part)) {
            QJsonArray validArray;
            for (const QString& v : allParts) validArray.append(v);
            validArray.append(QStringLiteral("all"));
            throw JsonRpcException(
                ErrorCode::kInvalidField,
                QStringLiteral("Unknown part: %1").arg(part),
                QJsonObject{{QStringLiteral("field"), part},
                            {QStringLiteral("validFields"), validArray}});
          }
        }

        QJsonObject result;

        if (requestedParts.contains(QStringLiteral("info"))) {
          result[QStringLiteral("info")] = MetaInspector::objectInfo(obj);
        }
        if (requestedParts.contains(QStringLiteral("properties"))) {
          result[QStringLiteral("properties")] = MetaInspector::listProperties(obj);
        }
        if (requestedParts.contains(QStringLiteral("methods"))) {
          result[QStringLiteral("methods")] = MetaInspector::listMethods(obj);
        }
        if (requestedParts.contains(QStringLiteral("signals"))) {
          result[QStringLiteral("signals")] = MetaInspector::listSignals(obj);
        }
        if (requestedParts.contains(QStringLiteral("qml"))) {
#ifdef QTPILOT_HAS_QML
          QmlItemInfo qmlInfo = inspectQmlItem(obj);
          if (qmlInfo.isQmlItem) {
            QJsonObject qmlResult;
            qmlResult[QStringLiteral("isQmlItem")] = true;
            qmlResult[QStringLiteral("qmlId")] = qmlInfo.qmlId;
            qmlResult[QStringLiteral("qmlFile")] = qmlInfo.qmlFile;
            qmlResult[QStringLiteral("qmlTypeName")] = qmlInfo.shortTypeName;
            result[QStringLiteral("qml")] = qmlResult;
          } else {
            result[QStringLiteral("qml")] = QJsonValue();  // null
          }
#else
          result[QStringLiteral("qml")] = QJsonValue();  // null when QML support not compiled
#endif
        }
        if (requestedParts.contains(QStringLiteral("geometry"))) {
          if (auto* widget = qobject_cast<QWidget*>(obj)) {
            QJsonObject geom;
            geom[QStringLiteral("x")] = widget->x();
            geom[QStringLiteral("y")] = widget->y();
            geom[QStringLiteral("width")] = widget->width();
            geom[QStringLiteral("height")] = widget->height();
            geom[QStringLiteral("visible")] = widget->isVisible();
            result[QStringLiteral("geometry")] = geom;
          } else {
            result[QStringLiteral("geometry")] = QJsonValue();  // null
          }
        }
        if (requestedParts.contains(QStringLiteral("model"))) {
          QAbstractItemModel* model = ModelNavigator::resolveModel(obj);
          if (model) {
            QJsonObject modelInfo;
            modelInfo[QStringLiteral("rowCount")] = model->rowCount();
            modelInfo[QStringLiteral("columnCount")] = model->columnCount();
            QJsonObject roleNames;
            const auto roles = model->roleNames();
            for (auto it = roles.constBegin(); it != roles.constEnd(); ++it) {
              roleNames[QString::number(it.key())] = QString::fromUtf8(it.value());
            }
            modelInfo[QStringLiteral("roleNames")] = roleNames;
            modelInfo[QStringLiteral("hasChildren")] = model->hasChildren();
            result[QStringLiteral("model")] = modelInfo;
          } else {
            result[QStringLiteral("model")] = QJsonValue();  // null
          }
        }

        return envelopeToString(ResponseEnvelope::wrap(result, objectId));
      });
```

Make sure `#include <QWidget>` is present in the file (it likely already is, given existing widget code).

- [ ] **Step 4: Verify the default-info-only test passes**

```bash
cmake --build build --config Release --target test_native_mode_api
./build/bin/Release/test_native_mode_api.exe -functions testInspectDefaultInfoOnly
```

Expected: PASS.

- [ ] **Step 5: Add tests for each remaining part**

Add to `tests/test_native_mode_api.cpp`:

```cpp
void TestNativeModeApi::testInspectPropertiesPart() {
  auto* w = new QLabel(QStringLiteral("propTest"));
  m_testObjects.append(w);
  QString id = ObjectRegistry::instance()->objectId(w);

  QJsonObject params;
  params[QStringLiteral("objectId")] = id;
  QJsonArray parts;
  parts.append(QStringLiteral("properties"));
  params[QStringLiteral("parts")] = parts;
  QJsonValue result = callResult("qt.objects.inspect", params);

  QJsonObject obj = result.toObject();
  QVERIFY(obj.contains(QStringLiteral("properties")));
  QVERIFY(!obj.contains(QStringLiteral("info")));
  QVERIFY(obj[QStringLiteral("properties")].toArray().size() > 0);
}

void TestNativeModeApi::testInspectAllAlias() {
  auto* w = new QLabel(QStringLiteral("allTest"));
  m_testObjects.append(w);
  QString id = ObjectRegistry::instance()->objectId(w);

  QJsonObject params;
  params[QStringLiteral("objectId")] = id;
  params[QStringLiteral("parts")] = QStringLiteral("all");
  QJsonValue result = callResult("qt.objects.inspect", params);

  QJsonObject obj = result.toObject();
  QVERIFY(obj.contains(QStringLiteral("info")));
  QVERIFY(obj.contains(QStringLiteral("properties")));
  QVERIFY(obj.contains(QStringLiteral("methods")));
  QVERIFY(obj.contains(QStringLiteral("signals")));
  QVERIFY(obj.contains(QStringLiteral("qml")));
  QVERIFY(obj.contains(QStringLiteral("geometry")));
  QVERIFY(obj.contains(QStringLiteral("model")));
}

void TestNativeModeApi::testInspectUnknownPartError() {
  auto* w = new QLabel(QStringLiteral("errTest"));
  m_testObjects.append(w);
  QString id = ObjectRegistry::instance()->objectId(w);

  QJsonObject params;
  params[QStringLiteral("objectId")] = id;
  QJsonArray parts;
  parts.append(QStringLiteral("bogus"));
  params[QStringLiteral("parts")] = parts;
  QJsonValue error = callError("qt.objects.inspect", params);
  QCOMPARE(error.toObject()[QStringLiteral("code")].toInt(), ErrorCode::kInvalidField);
}

void TestNativeModeApi::testInspectModelPartNullForNonModel() {
  auto* w = new QLabel(QStringLiteral("nullModelTest"));
  m_testObjects.append(w);
  QString id = ObjectRegistry::instance()->objectId(w);

  QJsonObject params;
  params[QStringLiteral("objectId")] = id;
  QJsonArray parts;
  parts.append(QStringLiteral("model"));
  params[QStringLiteral("parts")] = parts;
  QJsonValue result = callResult("qt.objects.inspect", params);

  QJsonObject obj = result.toObject();
  QVERIFY(obj.contains(QStringLiteral("model")));
  QVERIFY(obj[QStringLiteral("model")].isNull());
}

void TestNativeModeApi::testInspectGeometryPartOnWidget() {
  auto* w = new QLabel(QStringLiteral("geomTest"));
  w->resize(100, 50);
  m_testObjects.append(w);
  QString id = ObjectRegistry::instance()->objectId(w);

  QJsonObject params;
  params[QStringLiteral("objectId")] = id;
  QJsonArray parts;
  parts.append(QStringLiteral("geometry"));
  params[QStringLiteral("parts")] = parts;
  QJsonValue result = callResult("qt.objects.inspect", params);

  QJsonObject geom = result.toObject()[QStringLiteral("geometry")].toObject();
  QCOMPARE(geom[QStringLiteral("width")].toInt(), 100);
  QCOMPARE(geom[QStringLiteral("height")].toInt(), 50);
  QVERIFY(geom.contains(QStringLiteral("visible")));
}
```

Declare each in the test class header.

- [ ] **Step 6: Build and run all inspect tests**

```bash
cmake --build build --config Release --target test_native_mode_api
./build/bin/Release/test_native_mode_api.exe -functions testInspectDefaultInfoOnly testInspectPropertiesPart testInspectAllAlias testInspectUnknownPartError testInspectModelPartNullForNonModel testInspectGeometryPartOnWidget
```

Expected: all 6 PASS.

- [ ] **Step 7: Commit**

```bash
git add src/probe/api/native_mode_api.cpp tests/test_native_mode_api.cpp
git commit -m "feat(native): rewrite qt.objects.inspect with parts parameter"
```

---

### Task 5: Update `qt_objects_inspect` Python wrapper

**Files:**
- Modify: `python/src/qtpilot/tools/native.py` — update the existing `qt_objects_inspect` wrapper.

- [ ] **Step 1: Find and update the Python wrapper**

Locate the existing `qt_objects_inspect` definition in `python/src/qtpilot/tools/native.py` (search for `qt_objects_inspect`). Replace it with:

```python
    @mcp.tool
    async def qt_objects_inspect(
        objectId: str,
        parts: str | list[str] | None = None,
        ctx: Context = None,
    ) -> dict:
        """Inspect an object with selectable detail sections.

        `parts` controls which sections are returned. Default is `["info"]` for a
        lightweight overview. Use `"all"` or a list like `["info","properties","methods"]`
        for more detail.

        Valid parts: info, properties, methods, signals, qml, geometry, model.
        Parts that don't apply to the object (e.g. `model` on a non-model object) return
        as a null value rather than raising.

        Args:
            objectId: The object to inspect
            parts: Sections to include. String "all" includes everything; string "info" (or
                   omitted) returns just info. A list names specific sections.

        Example: qt_objects_inspect(objectId="MainWindow", parts=["info","geometry"])
        """
        params = {"objectId": objectId}
        if parts is not None: params["parts"] = parts
        return await _call_probe(ctx, "qt.objects.inspect", params)
```

Match the helper name (`_call_probe`) to neighbouring wrappers in the same file.

- [ ] **Step 2: Run Python tests**

```bash
cd python && python -m pytest tests/ -v
```

Expected: all pass (no new tests added yet — this just ensures the wrapper change doesn't break anything).

- [ ] **Step 3: Commit**

```bash
git add python/src/qtpilot/tools/native.py
git commit -m "feat(python): update qt_objects_inspect wrapper with parts parameter"
```

---

### Task 6: `qtpilot_status` — unified session status tool

Replaces `qt_modes`, `qtpilot_get_mode`, `qtpilot_list_probes`, `qtPilot_probe_status`.

**Files:**
- Modify: `python/src/qtpilot/tools/discovery_tools.py` — add new tool, leave old ones in place for now (removed in Phase 5).
- Modify: `python/tests/test_tools.py` — add to manifest.

- [ ] **Step 1: Add manifest entry**

In `python/tests/test_tools.py`, add `"qtpilot_status"` to the expected tool-name set.

- [ ] **Step 2: Run manifest test, verify fail**

```bash
cd python && python -m pytest tests/test_tools.py -v
```

Expected: FAIL — `qtpilot_status` not registered.

- [ ] **Step 3: Add qtpilot_status to discovery_tools.py**

In `python/src/qtpilot/tools/discovery_tools.py`, add inside `register_discovery_tools` (after the existing tools):

```python
    @mcp.tool
    async def qtpilot_status(ctx: Context = None) -> dict:
        """Get the full qtPilot session status in one call.

        Returns active mode, available modes, probe connection state, and discovery state.

        Example: qtpilot_status()
        """
        from qtpilot.server import get_discovery, get_mode, get_probe, get_state

        state = get_state()
        probe = get_probe()
        discovery = get_discovery()

        connection = {
            "connected": probe is not None and probe.is_connected,
        }
        if probe and probe.is_connected:
            connection["ws_url"] = probe.ws_url
            # probe_version / protocol_version are best-effort from probe metadata
            connection["probe_version"] = getattr(probe, "probe_version", None)
            connection["protocol_version"] = getattr(probe, "protocol_version", None)

        disc_info = {
            "active": discovery is not None and discovery.is_running,
            "probes": [],
        }
        if discovery:
            discovery.prune_stale()
            current_url = probe.ws_url if probe and probe.is_connected else None
            for dp in discovery.probes.values():
                disc_info["probes"].append({
                    "ws_url": dp.ws_url,
                    "app_name": dp.app_name,
                    "pid": dp.pid,
                    "qt_version": dp.qt_version,
                    "hostname": dp.hostname,
                    "mode": dp.mode,
                    "uptime": round(dp.uptime, 1),
                    "connected": dp.ws_url == current_url,
                })

        return {
            "mode": state.mode,
            "available_modes": ["native", "cu", "chrome", "all"],
            "connection": connection,
            "discovery": disc_info,
        }
```

- [ ] **Step 4: Run manifest test, verify pass**

```bash
cd python && python -m pytest tests/test_tools.py -v
```

Expected: PASS.

- [ ] **Step 5: Write behaviour test**

In a new or existing session-tools test file (`python/tests/test_mode_switching.py` is a good home), add:

```python
async def test_qtpilot_status_structure():
    """qtpilot_status returns the three-subgroup envelope regardless of state."""
    # Use whatever fixture/client pattern the file already uses
    result = await call_tool("qtpilot_status", {})
    assert "mode" in result
    assert "available_modes" in result
    assert set(result["available_modes"]) == {"native", "cu", "chrome", "all"}
    assert "connection" in result
    assert "connected" in result["connection"]
    assert "discovery" in result
    assert "active" in result["discovery"]
    assert "probes" in result["discovery"]
```

Match the actual test style used in `test_mode_switching.py` — patterns like `client.call_tool(...)`, `mcp_server.call_tool(...)`, etc.

- [ ] **Step 6: Run behaviour test, verify pass**

```bash
cd python && python -m pytest tests/test_mode_switching.py::test_qtpilot_status_structure -v
```

Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add python/src/qtpilot/tools/discovery_tools.py python/tests/test_tools.py python/tests/test_mode_switching.py
git commit -m "feat(python): add qtpilot_status unified session status tool"
```

---

### Task 7: Extend `qtpilot_log_status` with `tail` parameter

Absorbs `qtpilot_log_tail`.

**Files:**
- Modify: `python/src/qtpilot/tools/logging_tools.py` — extend `qtpilot_log_status`.
- Modify: `python/tests/test_logging_tools.py` — add test for tail.

- [ ] **Step 1: Write failing test for tail behaviour**

Read `python/tests/test_logging_tools.py` first to learn the test pattern used there. Then add:

```python
async def test_log_status_with_tail(log_setup):
    """qtpilot_log_status(tail=N) returns the last N entries inline."""
    # Assume log_setup has started logging and generated some entries
    # (adapt to actual fixture structure in the file)
    result = await call_tool("qtpilot_log_status", {"tail": 5})
    assert "entries" in result
    assert isinstance(result["entries"], list)
    assert len(result["entries"]) <= 5

async def test_log_status_default_no_entries_key():
    """qtpilot_log_status() (tail=0 default) omits the entries key."""
    result = await call_tool("qtpilot_log_status", {})
    assert "entries" not in result
```

- [ ] **Step 2: Run tests, verify failure**

```bash
cd python && python -m pytest tests/test_logging_tools.py::test_log_status_with_tail tests/test_logging_tools.py::test_log_status_default_no_entries_key -v
```

Expected: FAIL — `tail` param unsupported, or `entries` key not optional.

- [ ] **Step 3: Extend qtpilot_log_status**

Locate `qtpilot_log_status` in `python/src/qtpilot/tools/logging_tools.py`. Update its signature and body:

```python
    @mcp.tool
    async def qtpilot_log_status(tail: int = 0, ctx: Context = None) -> dict:
        """Get logging status and optionally tail recent entries.

        Args:
            tail: When >0, include that many most-recent entries in the response.
                  When 0 (default), no entries are returned — cheap status only.

        Example: qtpilot_log_status(tail=10)
        """
        from qtpilot.server import get_logger

        if tail < 0:
            raise ValueError("tail must be >= 0")

        logger = get_logger()
        status = logger.status()  # preserve existing status dict
        if tail > 0:
            status["entries"] = logger.recent(tail)
        return status
```

Adapt `logger.status()` / `logger.recent(...)` to the actual methods available. If the existing `qtpilot_log_tail` body calls a specific method (e.g. `logger.tail(n)`), reuse that for the `recent` equivalent. Read the existing `qtpilot_log_tail` body to find the exact calls.

- [ ] **Step 4: Run tests, verify pass**

```bash
cd python && python -m pytest tests/test_logging_tools.py -v
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add python/src/qtpilot/tools/logging_tools.py python/tests/test_logging_tools.py
git commit -m "feat(python): fold qtpilot_log_tail into qtpilot_log_status(tail=N)"
```

---

## Phase 3: Renames

### Task 8: Rename `qt.models.find` → `qt.models.search`

**Files:**
- Modify: `src/probe/api/native_mode_api.cpp` — rename method registration.
- Modify: `python/src/qtpilot/tools/native.py` — rename tool function.
- Modify: `tests/test_native_mode_api.cpp`, `tests/test_model_navigator.cpp`, `python/tests/test_tools.py` — update references.

- [ ] **Step 1: Update C++ registration**

Find `qt.models.find` in `src/probe/api/native_mode_api.cpp` (one occurrence as a method name). Change the registered method string to `qt.models.search`.

- [ ] **Step 2: Update Python wrapper**

Find `qt_models_find` in `python/src/qtpilot/tools/native.py`. Rename function to `qt_models_search`. Update the internal `_call_probe(ctx, "qt.models.find", ...)` to `"qt.models.search"`.

- [ ] **Step 3: Update tests**

- `tests/test_native_mode_api.cpp`: replace all `"qt.models.find"` with `"qt.models.search"`.
- `tests/test_model_navigator.cpp`: same.
- `python/tests/test_tools.py`: replace `"qt_models_find"` with `"qt_models_search"` in the manifest.

- [ ] **Step 4: Build and run all tests**

```bash
cmake --build build --config Release
cmd //c "set PATH=$(grep QTPILOT_QT_DIR:PATH= build/CMakeCache.txt | cut -d= -f2)\\bin;%PATH% && set QT_PLUGIN_PATH=$(grep QTPILOT_QT_DIR:PATH= build/CMakeCache.txt | cut -d= -f2)\\plugins && ctest --test-dir build -C Release --output-on-failure"
cd python && python -m pytest tests/ -v && cd ..
```

Expected: all tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/probe/api/native_mode_api.cpp python/src/qtpilot/tools/native.py \
        tests/test_native_mode_api.cpp tests/test_model_navigator.cpp \
        python/tests/test_tools.py
git commit -m "refactor: rename qt.models.find to qt.models.search"
```

---

### Task 9: Rename `qt.events.startCapture/stopCapture` → `qt.events.start/stop`

**Files:**
- Modify: `src/probe/api/native_mode_api.cpp` — rename both method registrations.
- Modify: `python/src/qtpilot/tools/native.py` — rename both Python tool wrappers.
- Modify: `tests/test_native_mode_api.cpp`, `python/tests/test_tools.py` — update references.

- [ ] **Step 1: Update C++ registrations**

Find both `qt.events.startCapture` and `qt.events.stopCapture` in `src/probe/api/native_mode_api.cpp`. Rename to `qt.events.start` and `qt.events.stop`.

- [ ] **Step 2: Update Python wrappers**

Find `qt_events_startCapture` and `qt_events_stopCapture` in `python/src/qtpilot/tools/native.py`. Rename functions to `qt_events_start` and `qt_events_stop`. Update the internal probe-method strings.

- [ ] **Step 3: Update tests**

- `tests/test_native_mode_api.cpp`: `qt.events.startCapture` → `qt.events.start`; `qt.events.stopCapture` → `qt.events.stop`.
- `python/tests/test_tools.py`: same rename in the manifest set.

- [ ] **Step 4: Build and run all tests**

Same as Task 8 Step 4.

- [ ] **Step 5: Commit**

```bash
git add src/probe/api/native_mode_api.cpp python/src/qtpilot/tools/native.py \
        tests/test_native_mode_api.cpp python/tests/test_tools.py
git commit -m "refactor: rename qt.events.{startCapture,stopCapture} to qt.events.{start,stop}"
```

---

### Task 10: Rename `qtpilot_start_recording/stop_recording` → `qtpilot_recording_start/stop`

**Files:**
- Modify: `python/src/qtpilot/tools/recording_tools.py` — rename both tool functions.
- Modify: `python/tests/test_recording_tools.py`, `python/tests/test_tools.py` — update references.

- [ ] **Step 1: Update Python tool registrations**

In `python/src/qtpilot/tools/recording_tools.py`, find and rename:
- `qtpilot_start_recording` → `qtpilot_recording_start`
- `qtpilot_stop_recording` → `qtpilot_recording_stop`

(The third tool `qtpilot_recording_status` is already correctly named.)

- [ ] **Step 2: Update tests**

- `python/tests/test_recording_tools.py`: replace all `qtpilot_start_recording` / `qtpilot_stop_recording` references with the new names.
- `python/tests/test_tools.py`: update manifest set.

- [ ] **Step 3: Run Python tests**

```bash
cd python && python -m pytest tests/ -v
```

Expected: PASS.

- [ ] **Step 4: Commit**

```bash
git add python/src/qtpilot/tools/recording_tools.py python/tests/test_recording_tools.py python/tests/test_tools.py
git commit -m "refactor: rename qtpilot_{start,stop}_recording to qtpilot_recording_{start,stop}"
```

---

## Phase 4: Action-response normalisation (Rule 7)

### Task 11: Normalise probe action responses to `{ok: true}`

Touches C++ actions that currently return `{success: true/false}`.

**Files:**
- Modify: `src/probe/api/native_mode_api.cpp` — replace `"success"` key with `"ok"` for action responses, and switch bool paths to throw on failure.
- Modify: `tests/test_native_mode_api.cpp` — update assertions.

- [ ] **Step 1: Write failing test asserting new shape**

Pick one action test already in the test file (e.g. a `qt.properties.set` test) and update it to assert on `ok` instead of `success`:

```cpp
void TestNativeModeApi::testPropertiesSetReturnsOk() {
  auto* w = new QLabel();
  w->setObjectName(QStringLiteral("okTest"));
  m_testObjects.append(w);
  QString id = ObjectRegistry::instance()->objectId(w);

  QJsonObject params;
  params[QStringLiteral("objectId")] = id;
  params[QStringLiteral("name")] = QStringLiteral("text");
  params[QStringLiteral("value")] = QStringLiteral("hello");
  QJsonValue result = callResult("qt.properties.set", params);

  QJsonObject obj = result.toObject();
  QVERIFY(obj.contains(QStringLiteral("ok")));
  QCOMPARE(obj[QStringLiteral("ok")].toBool(), true);
  QVERIFY(!obj.contains(QStringLiteral("success")));
}
```

Declare in the private slots.

- [ ] **Step 2: Run test, verify fail**

```bash
cmake --build build --config Release --target test_native_mode_api
./build/bin/Release/test_native_mode_api.exe -functions testPropertiesSetReturnsOk
```

Expected: FAIL — current response uses `success` key.

- [ ] **Step 3: Update each C++ action site**

In `src/probe/api/native_mode_api.cpp`, change each of the following to use `"ok"` instead of `"success"`. Line numbers are approximate — grep for `QStringLiteral("success")`:

| Current location (approx) | Change |
|---|---|
| Line 399 — `qt.properties.set` | `result["success"] = ok` → if `!ok` throw `kPropertyTypeMismatch`; else `result["ok"] = true` |
| Line 520 — `qt.methods.invoke` | `result["success"] = true` → `result["ok"] = true` |
| Line 589 — `qt.signals.subscribe` | same |
| Line 610 — `qt.signals.unsubscribe` | same |
| Line 912 — `qt.events.start` | same |
| Line 930 — `qt.events.stop` | same |
| Line 968 — `qt.names.register` | same |
| Line 986 — `qt.names.unregister` | if `!ok` throw `kNameNotFound`; else `result["ok"] = true` |

For `qt.properties.set` specifically, the existing bool-path already throws via the `catch (const std::runtime_error&)` block for the common failure modes — the `success:false` branch occurred only when `setProperty` returned false without throwing, which is rare. Change it to throw `kPropertyTypeMismatch` with a generic "set failed" message for that path.

For `qt.names.unregister` (line 986), trace the code: if it currently returns `{success:false}` when the name doesn't exist, switch that path to throw `ErrorCode::kNameNotFound` with detail `{"name": name}`.

- [ ] **Step 4: Run the single test, verify pass**

```bash
./build/bin/Release/test_native_mode_api.exe -functions testPropertiesSetReturnsOk
```

Expected: PASS.

- [ ] **Step 5: Update any existing tests that asserted on `success`**

Grep for `"success"` in `tests/test_native_mode_api.cpp` and update assertions to check `"ok"` instead. For tests that asserted on `success:false` branches that are now exceptions, change them to use `callError` and assert on the error code.

- [ ] **Step 6: Run full native-mode test binary**

```bash
./build/bin/Release/test_native_mode_api.exe
```

Expected: all PASS.

- [ ] **Step 7: Commit**

```bash
git add src/probe/api/native_mode_api.cpp tests/test_native_mode_api.cpp
git commit -m "refactor(native): normalise action responses to {ok:true}"
```

---

### Task 12: Normalise Python session tool responses to `{ok: true}`

Touches `qtpilot_connect_probe`, `qtpilot_disconnect_probe`, `qtpilot_set_mode`.

**Files:**
- Modify: `python/src/qtpilot/tools/discovery_tools.py`.
- Modify: `python/tests/test_mode_switching.py` — update assertions where they exist.

- [ ] **Step 1: Write failing test**

In `python/tests/test_mode_switching.py` (or the appropriate session-tool test file), add:

```python
async def test_connect_probe_returns_ok():
    """qtpilot_connect_probe returns {ok: true, ws_url: ...} on success."""
    # Set up a fake/real probe URL per existing test patterns
    result = await call_tool("qtpilot_connect_probe", {"ws_url": test_probe_url})
    assert result.get("ok") is True
    assert "ws_url" in result
    assert "connected" not in result  # old key gone

async def test_set_mode_returns_ok():
    result = await call_tool("qtpilot_set_mode", {"mode": "native"})
    assert result.get("ok") is True
    assert "mode" in result
    assert "changed" not in result
```

Use the same fixtures/patterns as other tests in the file.

- [ ] **Step 2: Run tests, verify fail**

```bash
cd python && python -m pytest tests/test_mode_switching.py::test_connect_probe_returns_ok tests/test_mode_switching.py::test_set_mode_returns_ok -v
```

Expected: FAIL — responses use old keys.

- [ ] **Step 3: Update Python wrappers**

In `python/src/qtpilot/tools/discovery_tools.py`, update the three tools:

```python
    @mcp.tool
    async def qtpilot_connect_probe(ws_url: str, ctx: Context = None) -> dict:
        """Connect to a qtPilot probe by its WebSocket URL.

        Example: qtpilot_connect_probe(ws_url="ws://localhost:9222")
        """
        from qtpilot.server import connect_to_probe
        from fastmcp.exceptions import ToolError

        try:
            conn = await connect_to_probe(ws_url)
            return {"ok": True, "ws_url": conn.ws_url}
        except Exception as e:
            raise ToolError(f"Failed to connect: {e}") from e

    @mcp.tool
    async def qtpilot_disconnect_probe(ctx: Context = None) -> dict:
        """Disconnect from the currently connected qtPilot probe.

        Idempotent — returns {ok: true} even when no probe is connected.

        Example: qtpilot_disconnect_probe()
        """
        from qtpilot.server import disconnect_probe, get_probe

        probe = get_probe()
        if probe is None or not probe.is_connected:
            return {"ok": True}
        ws_url = probe.ws_url
        await disconnect_probe()
        return {"ok": True, "ws_url": ws_url}

    @mcp.tool
    async def qtpilot_set_mode(mode: str, ctx: Context) -> dict:
        """Switch the active API mode, changing which tools are available.

        Modes: "native", "cu", "chrome", "all".

        Example: qtpilot_set_mode(mode="native")
        """
        from qtpilot.server import get_state
        from fastmcp.exceptions import ToolError

        state = get_state()
        result = state.set_mode(mode)
        if "error" in result:
            raise ToolError(result["error"])
        if result.get("changed", False):
            await ctx.send_tool_list_changed()
        return {"ok": True, "mode": state.mode}
```

If `ToolError` import path differs, check how other tools in the project signal errors to the MCP layer and match that pattern.

- [ ] **Step 4: Run tests, verify pass**

```bash
cd python && python -m pytest tests/test_mode_switching.py -v
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add python/src/qtpilot/tools/discovery_tools.py python/tests/test_mode_switching.py
git commit -m "refactor(python): normalise session action responses to {ok:true}"
```

---

## Phase 5: Remove killed tools

### Task 13: Remove killed C++ method registrations

**Files:**
- Modify: `src/probe/api/native_mode_api.cpp` — delete `RegisterMethod` blocks for killed methods.
- Modify: `tests/test_native_mode_api.cpp` — delete tests that exercised the killed methods.

Killed methods (see spec):
- `qt.objects.find`
- `qt.objects.findByClass`
- `qt.objects.query`
- `qt.objects.info`
- `qt.properties.list`
- `qt.methods.list`
- `qt.signals.list`
- `qt.qml.inspect`
- `qt.models.info`
- `qt.modes`

- [ ] **Step 1: Delete each RegisterMethod block**

For each killed method, find its `m_handler->RegisterMethod(QStringLiteral("qt.objects.find"), ...)` block in `native_mode_api.cpp` and delete the entire lambda block (the whole `RegisterMethod(...)` statement, including its closing `});`). Work through them in the order listed above.

Grep to verify none remain:

```bash
```

Use Grep tool in Claude Code — pattern `qt\\.objects\\.find|qt\\.objects\\.findByClass|qt\\.objects\\.query|qt\\.objects\\.info|qt\\.properties\\.list|qt\\.methods\\.list|qt\\.signals\\.list|qt\\.qml\\.inspect|qt\\.models\\.info|qt\\.modes` against `src/probe/api/native_mode_api.cpp`.

Expected: 0 matches.

- [ ] **Step 2: Delete or rewrite old tests that call killed methods**

In `tests/test_native_mode_api.cpp`, grep for the same methods. For each old test that called one:

- If the test's intent is subsumed by a new `qt.objects.search` or `qt.objects.inspect` test already written in Task 2/4 — delete the old test entirely (including its declaration in the header).
- If the test was testing a niche case not covered by new tests (rare — check by re-reading the spec), rewrite it against the new method. Note in the commit message.

- [ ] **Step 3: Build and run full test suite**

```bash
cmake --build build --config Release
./build/bin/Release/test_native_mode_api.exe
```

Expected: all PASS (no tests for removed methods remain).

- [ ] **Step 4: Commit**

```bash
git add src/probe/api/native_mode_api.cpp tests/test_native_mode_api.cpp
git commit -m "refactor(native): remove registrations for consolidated methods"
```

---

### Task 14: Remove killed Python wrappers

**Files:**
- Modify: `python/src/qtpilot/tools/native.py` — delete wrappers for killed tools.
- Modify: `python/src/qtpilot/tools/discovery_tools.py` — delete `qtpilot_list_probes`, `qtpilot_get_mode`, `qtPilot_probe_status`.
- Modify: `python/src/qtpilot/tools/logging_tools.py` — delete `qtpilot_log_tail` (now folded into `log_status`).

Killed Python tools:
- `qt_objects_find`, `qt_objects_findByClass`, `qt_objects_query`
- `qt_objects_info`, `qt_properties_list`, `qt_methods_list`, `qt_signals_list`
- `qt_qml_inspect`, `qt_models_info`
- `qt_modes`
- `qtpilot_get_mode`, `qtpilot_list_probes`, `qtPilot_probe_status`
- `qtpilot_log_tail`

- [ ] **Step 1: Update manifest assertion**

In `python/tests/test_tools.py`, remove the killed tool names from the expected set. Any leftover names not actually registered will cause the assertion to fail after removal — which is what we want (the canary).

- [ ] **Step 2: Delete wrappers**

In each of `native.py`, `discovery_tools.py`, `logging_tools.py`, delete the `@mcp.tool` function definitions listed above. Grep each file after to confirm no references remain.

- [ ] **Step 3: Update any tests that still reference killed tools**

Grep across `python/tests/`:

```
qt_objects_find|qt_objects_findByClass|qt_objects_query|qt_objects_info|qt_properties_list|qt_methods_list|qt_signals_list|qt_qml_inspect|qt_models_info|qt_modes|qtpilot_get_mode|qtpilot_list_probes|qtPilot_probe_status|qtpilot_log_tail
```

For each hit, migrate the test to use the replacement tool per the spec's kill list. This should mostly already be done by earlier tasks — this step confirms nothing was missed.

- [ ] **Step 4: Run full Python test suite**

```bash
cd python && python -m pytest tests/ -v
```

Expected: all PASS; any missed reference surfaces here.

- [ ] **Step 5: Commit**

```bash
git add python/src/qtpilot/tools/native.py \
        python/src/qtpilot/tools/discovery_tools.py \
        python/src/qtpilot/tools/logging_tools.py \
        python/tests/
git commit -m "refactor(python): remove wrappers for consolidated tools"
```

---

## Phase 6: Docs

### Task 15: Update CLAUDE.md usage examples

**Files:**
- Modify: `CLAUDE.md` — update the "Using qtPilot Tools" section (lines ~224-232 contain examples using killed tools).

- [ ] **Step 1: Read the current section**

Use Read on `CLAUDE.md`, lines 200-240, to see current examples.

- [ ] **Step 2: Update the examples**

Replace references to killed tools with their new equivalents:
- `qt_objects_findByClass` / `qt_objects_query` → `qt_objects_search`
- `qt_properties_list` → `qt_objects_inspect(objectId=..., parts=["properties"])`
- `qt_methods_list` / `qt_signals_list` → same pattern with `parts=["methods"]` / `parts=["signals"]`
- `qt_models_find` → `qt_models_search`
- `qtpilot_list_probes` / `qtPilot_probe_status` / `qtpilot_get_mode` → `qtpilot_status`

Keep the examples concrete (real object names from the test app).

- [ ] **Step 3: Commit**

```bash
git add CLAUDE.md
git commit -m "docs(claude-md): update usage examples for consolidated tools"
```

---

### Task 16: Update mcp-diagram.html tool list

**Files:**
- Modify: `mcp-diagram.html` — update the tool list (contains `qt_objects_tree` and others at line ~1986).

- [ ] **Step 1: Read the tool-list section of the HTML**

Find the list of tools (the file is a single-page HTML diagram — grep for `qt_objects_tree` to locate the section).

- [ ] **Step 2: Update the list**

- Remove killed tool entries (see kill list in spec).
- Rename the 5 renamed tools.
- Add `qt_objects_search` and `qtpilot_status` entries.
- Match the existing styling / formatting of neighbouring entries.

- [ ] **Step 3: Open the HTML in a browser and visually confirm**

Open `mcp-diagram.html` in a local browser; scan the tool list for consistency.

- [ ] **Step 4: Commit**

```bash
git add mcp-diagram.html
git commit -m "docs(diagram): update tool list for consolidated surface"
```

---

## Phase 7: Final verification

### Task 17: Full sweep and manifest canary

- [ ] **Step 1: Grep for any remaining references to killed / renamed tool names across the entire project**

Use Grep with pattern covering all killed and renamed tools:

```
qt_objects_find|qt_objects_findByClass|qt_objects_query|qt_objects_info|qt_properties_list|qt_methods_list|qt_signals_list|qt_qml_inspect|qt_models_info|qt_modes|qtpilot_get_mode|qtpilot_list_probes|qtPilot_probe_status|qtpilot_log_tail|qt_events_startCapture|qt_events_stopCapture|qt_models_find|qtpilot_start_recording|qtpilot_stop_recording
```

Expected: zero matches in source and test files. Matches in `.planning/`, old `docs/plans/`, archived spec files, or this plan's own file are fine.

If any source-tree match appears, fix inline and return to Task 14/15/16 before proceeding.

- [ ] **Step 2: Run the full test suite**

```bash
# C++ tests
cmake --build build --config Release
QT_DIR=$(grep "QTPILOT_QT_DIR:PATH=" build/CMakeCache.txt | cut -d= -f2)
cmd //c "set PATH=${QT_DIR}\\bin;%PATH% && set QT_PLUGIN_PATH=${QT_DIR}\\plugins && ctest --test-dir build -C Release --output-on-failure"

# Python tests
cd python && python -m pytest tests/ -v
```

Expected: 100% pass.

- [ ] **Step 3: Count the final tool surface**

Run the Python manifest inspection (or look at `test_tools.py`'s expected set). Confirm the set has exactly 39 tools matching the spec's "Final tool surface" section.

- [ ] **Step 4: Run a live smoke test against the test app (optional but recommended)**

```bash
# Launch the test app in background
build/bin/Release/qtPilot-launcher.exe build/bin/Release/qtPilot-test-app.exe &

# From Python or an MCP client, exercise new tools:
#   qtpilot_status()               -> should show 4 modes, 1+ discovered probe
#   qt_objects_search(className="QPushButton") -> should return at least the test app's buttons
#   qt_objects_inspect(objectId=<a button>, parts="all") -> should return all 7 keys
#   qt_objects_inspect(objectId=<a button>, parts=["model"]) -> should return {"model": null}

# Stop the test app when done
```

- [ ] **Step 5: Final commit (changelog)**

Update the changelog / release notes to mark this as the v0.4.0 tool consolidation release.

```bash
# Edit CHANGELOG.md if it exists, or leave a brief note in CLAUDE.md
git add CHANGELOG.md  # if applicable
git commit -m "docs(changelog): mark native-mode tool consolidation release"
```

---

## Self-Review Notes

**Spec coverage check:**
- Kill list (14 tools): Tasks 13–14 delete them all. ✓
- Rename list (5 tools): Tasks 8, 9, 10 cover the renames. ✓
- New canonical tools: Tasks 2, 3 (`search`), 4, 5 (`inspect`), 6 (`status`), 7 (`log_status` tail). ✓
- Rule 7 normalisation: Tasks 11 (C++), 12 (Python). ✓
- Error code `kInvalidField`: Task 1. ✓
- Test updates across 6 files: woven throughout Tasks 2–14. ✓
- CLAUDE.md + mcp-diagram.html: Tasks 15, 16. ✓
- Protocol version bump / changelog: Task 17 Step 5. ✓

**Dependencies between tasks:**
- Task 1 (error code) precedes Task 4 (inspect uses `kInvalidField`).
- Tasks 2, 4, 6, 7 (new tools) precede Tasks 13, 14 (deletions) so the replacements exist before removals.
- Task 11 (C++ normalisation) can run in parallel with earlier tasks but touches the same file as Task 4; serialise for clean diffs.

**Risk notes:**
- The `kInvalidField` error code is placed at `-32004` in a gap in the existing range. If a future code review prefers a different number, moving it is a single-line change plus re-compile.
- The `qt.objects.search` `root` subtree filter when no className is given walks up parents manually; if the project has a more efficient `ObjectRegistry::descendantsOf(root)` helper, prefer it.
- The Python helper name `_call_probe` used in Tasks 3, 5 may not match the actual helper in `native.py`. Check neighbouring wrappers before typing.

---

Plan complete and saved to `docs/superpowers/plans/2026-04-17-native-mode-tool-consolidation.md`. Two execution options:

1. **Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, review between tasks, fast iteration.
2. **Inline Execution** — Execute tasks in this session using executing-plans, batch execution with checkpoints.

Which approach?
