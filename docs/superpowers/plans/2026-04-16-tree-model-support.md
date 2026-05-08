# Tree Model Support Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Enable agents to find, page, and act on items in Qt tree/table/list views at arbitrary depth, including lazy and multi-thousand-row models.

**Architecture:** Row paths (`int[]`) replace the old single-level `parentRow`/`parentCol` as the JSON-serializable proxy for `QModelIndex`. Three tools touch the probe: `qt.models.data` extended with `parent` path, new `qt.models.find` with a custom lazy-aware recursive walker, new `qt.ui.clickItem` that accepts either `itemPath` (string[]) or `path` (int[]) and dispatches select/click/doubleClick/edit actions. Lazy models work transparently because every path walk calls `canFetchMore`/`fetchMore` at each level.

**Tech Stack:** C++17, Qt 5.15/6 (`QAbstractItemModel`, `QAbstractItemView`, `QStandardItemModel`), Qt Test (`QTEST_MAIN`), CMake, FastMCP (Python wrapper), pytest.

**Spec:** [docs/superpowers/specs/2026-04-16-tree-model-support-design.md](../specs/2026-04-16-tree-model-support-design.md)

**Build command (Windows):** `cmake --build build --config Release`
**Run C++ tests:**
```bash
QT_DIR=$(grep "QTPILOT_QT_DIR:PATH=" build/CMakeCache.txt | cut -d= -f2)
cmd //c "set PATH=${QT_DIR}\\bin;%PATH% && set QT_PLUGIN_PATH=${QT_DIR}\\plugins && ctest --test-dir build -C Release --output-on-failure -R <TEST_NAME>"
```
**Run single C++ test binary:** `build/bin/Release/<test_name>.exe`
**Run Python tests:** `cd python && pytest tests/test_tools.py -v`

---

### Task 1: Add new error codes

**Files:**
- Modify: `src/probe/api/error_codes.h`

- [ ] **Step 1: Add the five new error codes**

Insert after `kNotAModel = -32093;` in the Model/View block:

```cpp
// Model/View errors (-32090 to -32099)
constexpr int kModelNotFound = -32090;
constexpr int kModelIndexOutOfBounds = -32091;
constexpr int kModelRoleNotFound = -32092;
constexpr int kNotAModel = -32093;
constexpr int kInvalidParentPath = -32094;
constexpr int kItemNotFound = -32095;
constexpr int kInvalidColumn = -32096;
constexpr int kNotEditable = -32097;
constexpr int kInvalidRegex = -32098;
```

- [ ] **Step 2: Build and verify**

```bash
cmake --build build --config Release --target qtPilot_probe
```
Expected: builds clean (header-only change; no tests needed).

- [ ] **Step 3: Commit**

```bash
git add src/probe/api/error_codes.h
git commit -m "feat: add tree-model error codes (kInvalidParentPath, kItemNotFound, kInvalidColumn, kNotEditable, kInvalidRegex)"
```

---

### Task 2: Add `ensureFetched` helper

**Files:**
- Modify: `src/probe/introspection/model_navigator.h`
- Modify: `src/probe/introspection/model_navigator.cpp`
- Modify: `tests/test_model_navigator.cpp`

- [ ] **Step 1: Write the failing test**

Add a declaration in `TestModelNavigator` private slots (after `testModelsDataNotAModel`):

```cpp
  // Tree/path helpers
  void testEnsureFetchedCallsFetchMoreOnLazyModel();
```

Add a lazy-model helper class at file scope (above `TestModelNavigator`):

```cpp
/// Minimal lazy model: rowCount reports 0 until fetchMore is called.
class LazyFlatModel : public QAbstractListModel {
 public:
  explicit LazyFlatModel(int virtualCount, QObject* parent = nullptr)
      : QAbstractListModel(parent), m_virtualCount(virtualCount) {}

  int rowCount(const QModelIndex& parent = QModelIndex()) const override {
    return parent.isValid() ? 0 : m_fetched;
  }
  QVariant data(const QModelIndex& idx, int role = Qt::DisplayRole) const override {
    if (!idx.isValid() || role != Qt::DisplayRole) return {};
    return QString("Row%1").arg(idx.row());
  }
  bool canFetchMore(const QModelIndex& parent) const override {
    return !parent.isValid() && m_fetched < m_virtualCount;
  }
  void fetchMore(const QModelIndex& parent) override {
    if (parent.isValid() || m_fetched >= m_virtualCount) return;
    const int toAdd = m_virtualCount - m_fetched;
    beginInsertRows(QModelIndex(), m_fetched, m_fetched + toAdd - 1);
    m_fetched += toAdd;
    endInsertRows();
  }
  int fetchedRows() const { return m_fetched; }

 private:
  int m_virtualCount;
  int m_fetched = 0;
};
```

Add the test method:

```cpp
void TestModelNavigator::testEnsureFetchedCallsFetchMoreOnLazyModel() {
  LazyFlatModel lazy(42, this);
  QCOMPARE(lazy.fetchedRows(), 0);

  ModelNavigator::ensureFetched(&lazy, QModelIndex());

  QCOMPARE(lazy.fetchedRows(), 42);
  QCOMPARE(lazy.rowCount(), 42);
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build --config Release --target test_model_navigator
```
Expected: build error — `ModelNavigator::ensureFetched` not declared.

- [ ] **Step 3: Declare the helper**

Add to `model_navigator.h` (inside the class, after `getRoleNames`):

```cpp
  /// @brief Force-load lazy children at parentIdx until canFetchMore returns false.
  ///
  /// Qt lazy models (e.g., QFileSystemModel) may report rowCount=0 until
  /// fetchMore is called. This helper drains the fetch queue so subsequent
  /// rowCount/index calls see the full set of children.
  /// @param model The model whose children to fetch.
  /// @param parentIdx The parent index. Default (invalid) = root.
  static void ensureFetched(QAbstractItemModel* model, const QModelIndex& parentIdx = QModelIndex());
```

- [ ] **Step 4: Implement the helper**

Add to `model_navigator.cpp` (before `resolveModel`):

```cpp
void ModelNavigator::ensureFetched(QAbstractItemModel* model, const QModelIndex& parentIdx) {
  if (!model) return;
  // Drain fetchMore loop — some models fetch in batches.
  while (model->canFetchMore(parentIdx)) {
    model->fetchMore(parentIdx);
  }
}
```

- [ ] **Step 5: Run test to verify it passes**

```bash
cmake --build build --config Release --target test_model_navigator
build/bin/Release/test_model_navigator.exe -platform minimal testEnsureFetchedCallsFetchMoreOnLazyModel
```
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/probe/introspection/model_navigator.{h,cpp} tests/test_model_navigator.cpp
git commit -m "feat: add ModelNavigator::ensureFetched for lazy-model path walks"
```

---

### Task 3: Add `pathToIndex` helper

**Files:**
- Modify: `src/probe/introspection/model_navigator.h`
- Modify: `src/probe/introspection/model_navigator.cpp`
- Modify: `tests/test_model_navigator.cpp`

- [ ] **Step 1: Write the failing tests**

Add to `TestModelNavigator` private slots:

```cpp
  void testPathToIndexRoot();
  void testPathToIndexTwoLevel();
  void testPathToIndexInvalidReturnsFailedSegment();
  void testPathToIndexCallsEnsureFetched();
```

Add tests (after the `testEnsureFetched*` test):

```cpp
void TestModelNavigator::testPathToIndexRoot() {
  QModelIndex idx = ModelNavigator::pathToIndex(m_smallModel, {});
  QVERIFY(!idx.isValid());  // root
}

void TestModelNavigator::testPathToIndexTwoLevel() {
  // Build a 2-level tree in a QStandardItemModel.
  auto* tree = new QStandardItemModel(this);
  auto* a = new QStandardItem("A");
  auto* b = new QStandardItem("B");
  a->appendRow(new QStandardItem("A.0"));
  a->appendRow(new QStandardItem("A.1"));
  b->appendRow(new QStandardItem("B.0"));
  tree->appendRow(a);
  tree->appendRow(b);

  QModelIndex idx = ModelNavigator::pathToIndex(tree, {0, 1});
  QVERIFY(idx.isValid());
  QCOMPARE(tree->data(idx, Qt::DisplayRole).toString(), QString("A.1"));

  delete tree;
}

void TestModelNavigator::testPathToIndexInvalidReturnsFailedSegment() {
  int failed = -1;
  QModelIndex idx = ModelNavigator::pathToIndex(m_smallModel, {99}, &failed);
  QVERIFY(!idx.isValid());
  QCOMPARE(failed, 0);
}

void TestModelNavigator::testPathToIndexCallsEnsureFetched() {
  LazyFlatModel lazy(10, this);
  QModelIndex idx = ModelNavigator::pathToIndex(&lazy, {5});
  QVERIFY(idx.isValid());
  QCOMPARE(lazy.data(idx, Qt::DisplayRole).toString(), QString("Row5"));
}
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
cmake --build build --config Release --target test_model_navigator
```
Expected: build error — `pathToIndex` not declared.

- [ ] **Step 3: Declare the helper**

Add to `model_navigator.h`:

```cpp
  /// @brief Walk a row path, calling ensureFetched at each level.
  ///
  /// Each segment indexes into column 0 of the current parent. Returns an
  /// invalid QModelIndex for the root (empty path). If any segment is out
  /// of range, returns an invalid index and (if outFailedSegment is set)
  /// writes the 0-based index of the failing segment.
  /// @param model The model to walk.
  /// @param path  Row indices, one per tree level.
  /// @param outFailedSegment Optional output: index of first failing segment.
  /// @return QModelIndex for the target node, or invalid on failure.
  static QModelIndex pathToIndex(QAbstractItemModel* model, const QList<int>& path,
                                 int* outFailedSegment = nullptr);
```

- [ ] **Step 4: Implement the helper**

Add to `model_navigator.cpp`:

```cpp
QModelIndex ModelNavigator::pathToIndex(QAbstractItemModel* model, const QList<int>& path,
                                        int* outFailedSegment) {
  if (!model) {
    if (outFailedSegment) *outFailedSegment = 0;
    return {};
  }
  QModelIndex current;  // invalid = root
  for (int i = 0; i < path.size(); ++i) {
    ensureFetched(model, current);
    const int row = path[i];
    if (row < 0 || row >= model->rowCount(current)) {
      if (outFailedSegment) *outFailedSegment = i;
      return {};
    }
    current = model->index(row, 0, current);
    if (!current.isValid()) {
      if (outFailedSegment) *outFailedSegment = i;
      return {};
    }
  }
  return current;
}
```

- [ ] **Step 5: Run tests to verify they pass**

```bash
cmake --build build --config Release --target test_model_navigator
build/bin/Release/test_model_navigator.exe -platform minimal
```
Expected: all new `testPathToIndex*` methods PASS.

- [ ] **Step 6: Commit**

```bash
git add src/probe/introspection/model_navigator.{h,cpp} tests/test_model_navigator.cpp
git commit -m "feat: add ModelNavigator::pathToIndex helper"
```

---

### Task 4: Add `textPathToIndex` helper

**Files:**
- Modify: `src/probe/introspection/model_navigator.h`
- Modify: `src/probe/introspection/model_navigator.cpp`
- Modify: `tests/test_model_navigator.cpp`

- [ ] **Step 1: Write the failing tests**

Add to `TestModelNavigator` private slots:

```cpp
  void testTextPathToIndexTwoLevel();
  void testTextPathToIndexMissingSegment();
  void testTextPathToIndexWithColumn();
```

Tests:

```cpp
void TestModelNavigator::testTextPathToIndexTwoLevel() {
  auto* tree = new QStandardItemModel(this);
  auto* etc = new QStandardItem("ETC");
  etc->appendRow(new QStandardItem("fos4 Fresnel"));
  etc->appendRow(new QStandardItem("ColorSource"));
  tree->appendRow(etc);

  QModelIndex idx = ModelNavigator::textPathToIndex(tree, {"ETC", "fos4 Fresnel"}, 0);
  QVERIFY(idx.isValid());
  QCOMPARE(tree->data(idx, Qt::DisplayRole).toString(), QString("fos4 Fresnel"));

  delete tree;
}

void TestModelNavigator::testTextPathToIndexMissingSegment() {
  auto* tree = new QStandardItemModel(this);
  tree->appendRow(new QStandardItem("A"));

  int failed = -1;
  QModelIndex idx = ModelNavigator::textPathToIndex(tree, {"A", "missing"}, 0, &failed);
  QVERIFY(!idx.isValid());
  QCOMPARE(failed, 1);

  delete tree;
}

void TestModelNavigator::testTextPathToIndexWithColumn() {
  // 2-column flat model, match against column 1.
  auto* tree = new QStandardItemModel(3, 2, this);
  tree->setItem(0, 0, new QStandardItem("r0c0"));
  tree->setItem(0, 1, new QStandardItem("r0c1"));
  tree->setItem(1, 0, new QStandardItem("r1c0"));
  tree->setItem(1, 1, new QStandardItem("r1c1"));
  tree->setItem(2, 0, new QStandardItem("r2c0"));
  tree->setItem(2, 1, new QStandardItem("r2c1"));

  QModelIndex idx = ModelNavigator::textPathToIndex(tree, {"r1c1"}, 1);
  QVERIFY(idx.isValid());
  // Row identity should be row 1 (addressed via column 0).
  QCOMPARE(idx.row(), 1);
}
```

- [ ] **Step 2: Run to verify failure**

```bash
cmake --build build --config Release --target test_model_navigator
```
Expected: build error.

- [ ] **Step 3: Declare the helper**

Add to `model_navigator.h`:

```cpp
  /// @brief Walk a text path, matching each segment against cell display text.
  ///
  /// Matching is exact and case-sensitive. First matching row at each level
  /// is taken. Returns a row-identity QModelIndex (column 0). If no match at
  /// some level, returns invalid; (if set) writes segment index to output.
  /// @param model The model to walk.
  /// @param itemPath Display text at each level.
  /// @param matchColumn Column whose display data is matched against each segment.
  /// @param outFailedSegment Optional output: first failing segment index.
  static QModelIndex textPathToIndex(QAbstractItemModel* model, const QStringList& itemPath,
                                     int matchColumn, int* outFailedSegment = nullptr);
```

- [ ] **Step 4: Implement**

Add to `model_navigator.cpp`:

```cpp
QModelIndex ModelNavigator::textPathToIndex(QAbstractItemModel* model, const QStringList& itemPath,
                                            int matchColumn, int* outFailedSegment) {
  if (!model) {
    if (outFailedSegment) *outFailedSegment = 0;
    return {};
  }
  QModelIndex current;  // invalid = root
  for (int i = 0; i < itemPath.size(); ++i) {
    ensureFetched(model, current);
    const QString& wanted = itemPath[i];
    const int rows = model->rowCount(current);
    bool matched = false;
    for (int row = 0; row < rows; ++row) {
      const QModelIndex cell = model->index(row, matchColumn, current);
      if (!cell.isValid()) continue;
      if (model->data(cell, Qt::DisplayRole).toString() == wanted) {
        current = model->index(row, 0, current);  // row identity in column 0
        matched = true;
        break;
      }
    }
    if (!matched) {
      if (outFailedSegment) *outFailedSegment = i;
      return {};
    }
  }
  return current;
}
```

- [ ] **Step 5: Run tests to verify they pass**

```bash
cmake --build build --config Release --target test_model_navigator
build/bin/Release/test_model_navigator.exe -platform minimal
```
Expected: `testTextPathToIndex*` PASS.

- [ ] **Step 6: Commit**

```bash
git add src/probe/introspection/model_navigator.{h,cpp} tests/test_model_navigator.cpp
git commit -m "feat: add ModelNavigator::textPathToIndex helper"
```

---

### Task 5: Add `indexToRowData` helper

**Files:**
- Modify: `src/probe/introspection/model_navigator.h`
- Modify: `src/probe/introspection/model_navigator.cpp`
- Modify: `tests/test_model_navigator.cpp`

- [ ] **Step 1: Write the failing test**

Add to `TestModelNavigator` private slots:

```cpp
  void testIndexToRowDataProducesPathAndCells();
```

Test:

```cpp
void TestModelNavigator::testIndexToRowDataProducesPathAndCells() {
  auto* tree = new QStandardItemModel(this);
  auto* parent = new QStandardItem("P");
  parent->appendRow(new QStandardItem("C0"));
  parent->appendRow(new QStandardItem("C1"));
  tree->appendRow(parent);

  const QModelIndex c1 = tree->index(1, 0, tree->index(0, 0));
  QJsonObject row = ModelNavigator::indexToRowData(tree, c1, {Qt::DisplayRole});

  QJsonArray path = row["path"].toArray();
  QCOMPARE(path.size(), 2);
  QCOMPARE(path[0].toInt(), 0);
  QCOMPARE(path[1].toInt(), 1);

  QJsonArray cells = row["cells"].toArray();
  QCOMPARE(cells.size(), 1);  // single column
  QCOMPARE(cells[0].toObject()["display"].toString(), QString("C1"));
  QCOMPARE(row["hasChildren"].toBool(), false);

  delete tree;
}
```

- [ ] **Step 2: Run to verify failure**

```bash
cmake --build build --config Release --target test_model_navigator
```
Expected: build error — `indexToRowData` not declared.

- [ ] **Step 3: Declare the helper**

Add to `model_navigator.h`:

```cpp
  /// @brief Convert a QModelIndex to a {path, cells, hasChildren} JSON row.
  ///
  /// `path` is built by walking up via parent(). `cells` contains one entry
  /// per column at the index's level, each carrying the requested roles.
  /// Does not fetchMore — caller is responsible if iterating children.
  /// @param model The model.
  /// @param index The index to serialize.
  /// @param roles Role IDs to fetch per cell. Empty → Qt::DisplayRole only.
  static QJsonObject indexToRowData(QAbstractItemModel* model, const QModelIndex& index,
                                    const QList<int>& roles = {});
```

- [ ] **Step 4: Implement**

Add to `model_navigator.cpp` (reuse the same role-name resolution as `getModelData`):

```cpp
QJsonObject ModelNavigator::indexToRowData(QAbstractItemModel* model, const QModelIndex& index,
                                           const QList<int>& roles) {
  QJsonObject row;
  if (!model || !index.isValid()) return row;

  // Build path by walking up to root.
  QJsonArray pathArr;
  for (QModelIndex walk = index; walk.isValid(); walk = walk.parent()) {
    pathArr.prepend(walk.row());
  }
  row[QStringLiteral("path")] = pathArr;

  // Effective roles.
  QList<int> effective = roles;
  if (effective.isEmpty()) effective.append(Qt::DisplayRole);

  const QHash<int, QByteArray> modelRoleNames = model->roleNames();
  const QModelIndex parent = index.parent();
  const int colCount = model->columnCount(parent);

  QJsonArray cells;
  for (int col = 0; col < colCount; ++col) {
    const QModelIndex cellIdx = model->index(index.row(), col, parent);
    QJsonObject cell;
    for (int role : effective) {
      QString roleName;
      if (modelRoleNames.contains(role)) {
        roleName = QString::fromUtf8(modelRoleNames.value(role));
      } else {
        const auto& std = standardRoleNames();
        for (auto it = std.constBegin(); it != std.constEnd(); ++it) {
          if (it.value() == role) { roleName = it.key(); break; }
        }
        if (roleName.isEmpty())
          roleName = QStringLiteral("role_") + QString::number(role);
      }
      cell[roleName] = variantToJson(model->data(cellIdx, role));
    }
    cells.append(cell);
  }
  row[QStringLiteral("cells")] = cells;
  row[QStringLiteral("hasChildren")] = model->hasChildren(model->index(index.row(), 0, parent));
  return row;
}
```

- [ ] **Step 5: Run test to verify it passes**

```bash
cmake --build build --config Release --target test_model_navigator
build/bin/Release/test_model_navigator.exe -platform minimal testIndexToRowDataProducesPathAndCells
```
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/probe/introspection/model_navigator.{h,cpp} tests/test_model_navigator.cpp
git commit -m "feat: add ModelNavigator::indexToRowData helper"
```

---

### Task 6: Refactor `getModelData` to take `parentPath`

**Files:**
- Modify: `src/probe/introspection/model_navigator.h`
- Modify: `src/probe/introspection/model_navigator.cpp`
- Modify: `tests/test_model_navigator.cpp`

**Note:** This task changes the C++ signature. The JSON-RPC handler (Task 7) is the only caller. Old tests continue to use the method; they'll keep passing because `parentPath={}` is the same as the old `parentRow=-1`.

- [ ] **Step 1: Write new failing tests**

Add to `TestModelNavigator` private slots:

```cpp
  void testGetModelDataWithTwoLevelParentPath();
  void testGetModelDataRowsHavePathField();
  void testGetModelDataInvalidParentPathReturnsEmpty();
```

Tests:

```cpp
void TestModelNavigator::testGetModelDataWithTwoLevelParentPath() {
  auto* tree = new QStandardItemModel(this);
  auto* a = new QStandardItem("A");
  a->appendRow(new QStandardItem("A.0"));
  a->appendRow(new QStandardItem("A.1"));
  auto* aa0 = new QStandardItem("A.0 child placeholder");
  a->child(0)->appendRow(aa0);
  tree->appendRow(a);

  QJsonObject data = ModelNavigator::getModelData(tree, {0, 0}, 0, -1, {});
  QCOMPARE(data["totalRows"].toInt(), 1);
  QJsonArray rows = data["rows"].toArray();
  QCOMPARE(rows.size(), 1);
  QJsonArray path = rows[0].toObject()["path"].toArray();
  QCOMPARE(path.size(), 3);
  QCOMPARE(path[0].toInt(), 0);
  QCOMPARE(path[1].toInt(), 0);
  QCOMPARE(path[2].toInt(), 0);

  delete tree;
}

void TestModelNavigator::testGetModelDataRowsHavePathField() {
  QJsonObject data = ModelNavigator::getModelData(m_smallModel, {}, 0, -1, {});
  QJsonArray rows = data["rows"].toArray();
  QCOMPARE(rows.size(), 3);
  for (int i = 0; i < 3; ++i) {
    QJsonArray path = rows[i].toObject()["path"].toArray();
    QCOMPARE(path.size(), 1);
    QCOMPARE(path[0].toInt(), i);
  }
  // parent echo
  QCOMPARE(data["parent"].toArray().size(), 0);
}

void TestModelNavigator::testGetModelDataInvalidParentPathReturnsEmpty() {
  QJsonObject data = ModelNavigator::getModelData(m_smallModel, {99}, 0, -1, {});
  QCOMPARE(data["totalRows"].toInt(), 0);
  QCOMPARE(data["rows"].toArray().size(), 0);
}
```

- [ ] **Step 2: Run to verify failure**

```bash
cmake --build build --config Release --target test_model_navigator
```
Expected: build error (signature mismatch — `parentRow`/`parentCol` replaced).

- [ ] **Step 3: Change the declaration**

In `model_navigator.h`, replace the existing `getModelData` declaration:

```cpp
  /// @brief Retrieve model data at `parentPath`, with smart pagination and role filtering.
  ///
  /// Smart pagination rules (applied only when limit <= 0):
  /// - totalRows <= 100: return all rows
  /// - totalRows > 100: return first 100 rows
  /// Rows returned carry a `path` field relative to the model root.
  /// ensureFetched is called at every level of the path walk and at the
  /// destination parent before counting rows.
  ///
  /// @param model The model to read data from.
  /// @param parentPath Row path to the parent node; empty = root.
  /// @param offset Row offset among children of `parentPath`.
  /// @param limit Max rows; -1 = auto.
  /// @param roles Role IDs; empty = Qt::DisplayRole only.
  /// @return JSON object with parent, rows, totalRows, totalColumns, offset, limit, hasMore.
  static QJsonObject getModelData(QAbstractItemModel* model, const QList<int>& parentPath,
                                  int offset = 0, int limit = -1,
                                  const QList<int>& roles = {});
```

- [ ] **Step 4: Rewrite the implementation**

In `model_navigator.cpp`, replace the existing `getModelData` body:

```cpp
QJsonObject ModelNavigator::getModelData(QAbstractItemModel* model, const QList<int>& parentPath,
                                         int offset, int limit, const QList<int>& roles) {
  QJsonObject result;
  // Echo the parent path first so all early-return shapes match.
  QJsonArray parentArr;
  for (int seg : parentPath) parentArr.append(seg);
  result[QStringLiteral("parent")] = parentArr;

  auto fillEmpty = [&](int off, int lim) {
    result[QStringLiteral("rows")] = QJsonArray();
    result[QStringLiteral("totalRows")] = 0;
    result[QStringLiteral("totalColumns")] = 0;
    result[QStringLiteral("offset")] = off;
    result[QStringLiteral("limit")] = lim;
    result[QStringLiteral("hasMore")] = false;
  };
  if (!model) { fillEmpty(0, 0); return result; }

  // Resolve parent. Invalid path → empty result (handler emits kInvalidParentPath).
  QModelIndex parentIdx = pathToIndex(model, parentPath);
  if (!parentPath.isEmpty() && !parentIdx.isValid()) { fillEmpty(0, 0); return result; }

  ensureFetched(model, parentIdx);
  const int totalRows = model->rowCount(parentIdx);
  const int totalCols = model->columnCount(parentIdx);

  // Smart pagination.
  if (limit <= 0) {
    if (totalRows <= 100) { offset = 0; limit = totalRows; }
    else { limit = 100; }
  }
  if (offset < 0) offset = 0;
  if (offset > totalRows) offset = totalRows;
  const int endRow = qMin(offset + limit, totalRows);

  QList<int> effective = roles;
  if (effective.isEmpty()) effective.append(Qt::DisplayRole);

  QJsonArray rowsArr;
  for (int row = offset; row < endRow; ++row) {
    const QModelIndex idx = model->index(row, 0, parentIdx);
    rowsArr.append(indexToRowData(model, idx, effective));
  }

  result[QStringLiteral("rows")] = rowsArr;
  result[QStringLiteral("totalRows")] = totalRows;
  result[QStringLiteral("totalColumns")] = totalCols;
  result[QStringLiteral("offset")] = offset;
  result[QStringLiteral("limit")] = limit;
  result[QStringLiteral("hasMore")] = (endRow < totalRows);
  return result;
}
```

- [ ] **Step 5: Run tests to verify they pass**

```bash
cmake --build build --config Release --target test_model_navigator
build/bin/Release/test_model_navigator.exe -platform minimal
```
Expected: all tests PASS, including old flat-model tests and new tree tests. Old tests keep passing because `parentPath={}` matches the old root-level behavior.

- [ ] **Step 6: Commit**

```bash
git add src/probe/introspection/model_navigator.{h,cpp} tests/test_model_navigator.cpp
git commit -m "refactor: getModelData takes parentPath (int[]) with tree support"
```

---

### Task 7: Update `qt.models.data` JSON-RPC handler

**Files:**
- Modify: `src/probe/api/native_mode_api.cpp:835-877`
- Modify: `tests/test_model_navigator.cpp`

- [ ] **Step 1: Write the failing integration tests**

Add to `TestModelNavigator` private slots:

```cpp
  void testApiModelsDataWithParentArray();
  void testApiModelsDataInvalidParentPathError();
  void testApiModelsDataEchoesParent();
```

Tests (add after existing `testModelsData*` methods):

```cpp
void TestModelNavigator::testApiModelsDataWithParentArray() {
  auto* tree = new QStandardItemModel(this);
  auto* a = new QStandardItem("A");
  a->appendRow(new QStandardItem("A.0"));
  a->appendRow(new QStandardItem("A.1"));
  tree->appendRow(a);
  tree->setObjectName("myTree");
  ObjectRegistry::instance()->scanExistingObjects(tree);

  QString modelId = ObjectRegistry::instance()->objectId(tree);

  QJsonValue result = callResult("qt.models.data",
                                 QJsonObject{{"objectId", modelId},
                                             {"parent", QJsonArray{0}}});
  QJsonObject data = result.toObject();
  QCOMPARE(data["totalRows"].toInt(), 2);
  QCOMPARE(data["rows"].toArray().size(), 2);
  QCOMPARE(data["rows"].toArray()[0].toObject()["cells"].toArray()[0]
               .toObject()["display"].toString(),
           QString("A.0"));
}

void TestModelNavigator::testApiModelsDataInvalidParentPathError() {
  QString modelId = ObjectRegistry::instance()->objectId(m_smallModel);
  QJsonObject error = callExpectError("qt.models.data",
                                      QJsonObject{{"objectId", modelId},
                                                  {"parent", QJsonArray{99}}});
  QCOMPARE(error["code"].toInt(), ErrorCode::kInvalidParentPath);
  QJsonObject details = error["data"].toObject();
  QCOMPARE(details["failedSegment"].toInt(), 0);
  QVERIFY(details.contains("path"));
  QVERIFY(details.contains("availableRows"));
}

void TestModelNavigator::testApiModelsDataEchoesParent() {
  QString modelId = ObjectRegistry::instance()->objectId(m_smallModel);
  QJsonValue result = callResult("qt.models.data",
                                 QJsonObject{{"objectId", modelId},
                                             {"parent", QJsonArray{}}});
  QCOMPARE(result.toObject()["parent"].toArray().size(), 0);
}
```

- [ ] **Step 2: Run to verify failure**

```bash
cmake --build build --config Release --target test_model_navigator
build/bin/Release/test_model_navigator.exe -platform minimal testApiModelsDataWithParentArray
```
Expected: FAIL — handler still uses `parentRow`/`parentCol`.

- [ ] **Step 3: Update the handler**

In `native_mode_api.cpp`, replace the entire `qt.models.data` handler body (from `m_handler->RegisterMethod(QStringLiteral("qt.models.data"), ...)` at line 835) with:

```cpp
  m_handler->RegisterMethod(QStringLiteral("qt.models.data"), [](const QString& params) -> QString {
    auto p = parseParams(params);
    QObject* obj = resolveObjectParam(p, QStringLiteral("qt.models.data"));
    QString objectId = p[QStringLiteral("objectId")].toString();

    QAbstractItemModel* model = ModelNavigator::resolveModel(obj);
    if (!model) {
      throw JsonRpcException(
          ErrorCode::kNotAModel,
          QStringLiteral("Object is not a model and does not have an associated model"),
          QJsonObject{{QStringLiteral("objectId"), objectId},
                      {QStringLiteral("hint"),
                       QStringLiteral("Use qt.models.list to discover available models")}});
    }

    // Resolve parent path.
    QList<int> parentPath;
    QJsonArray parentArr = p[QStringLiteral("parent")].toArray();
    for (const QJsonValue& v : parentArr) parentPath.append(v.toInt());

    if (!parentPath.isEmpty()) {
      int failed = -1;
      QModelIndex parentIdx = ModelNavigator::pathToIndex(model, parentPath, &failed);
      if (!parentIdx.isValid()) {
        // Compute available rows at the failure point for the error detail.
        QModelIndex walk;
        for (int i = 0; i < failed; ++i) {
          ModelNavigator::ensureFetched(model, walk);
          walk = model->index(parentPath[i], 0, walk);
        }
        ModelNavigator::ensureFetched(model, walk);
        throw JsonRpcException(
            ErrorCode::kInvalidParentPath,
            QStringLiteral("Parent path invalid at segment %1").arg(failed),
            QJsonObject{{QStringLiteral("path"), parentArr},
                        {QStringLiteral("failedSegment"), failed},
                        {QStringLiteral("availableRows"), model->rowCount(walk)}});
      }
    }

    int offset = p[QStringLiteral("offset")].toInt(0);
    int limit = p[QStringLiteral("limit")].toInt(-1);

    // Resolve roles parameter.
    QList<int> resolvedRoles;
    QJsonArray rolesParam = p[QStringLiteral("roles")].toArray();
    for (const QJsonValue& roleVal : rolesParam) {
      if (roleVal.isDouble()) {
        resolvedRoles.append(roleVal.toInt());
      } else if (roleVal.isString()) {
        QString roleName = roleVal.toString();
        int roleId = ModelNavigator::resolveRoleName(model, roleName);
        if (roleId < 0) {
          throw JsonRpcException(
              ErrorCode::kModelRoleNotFound, QStringLiteral("Role not found: %1").arg(roleName),
              QJsonObject{{QStringLiteral("roleName"), roleName},
                          {QStringLiteral("availableRoles"), ModelNavigator::getRoleNames(model)}});
        }
        resolvedRoles.append(roleId);
      }
    }

    QJsonObject data =
        ModelNavigator::getModelData(model, parentPath, offset, limit, resolvedRoles);
    return envelopeToString(ResponseEnvelope::wrap(data, objectId));
  });
```

- [ ] **Step 4: Run tests to verify they pass**

```bash
cmake --build build --config Release --target test_model_navigator
build/bin/Release/test_model_navigator.exe -platform minimal
```
Expected: all tests PASS.

- [ ] **Step 5: Commit**

```bash
git add src/probe/api/native_mode_api.cpp tests/test_model_navigator.cpp
git commit -m "feat: qt.models.data accepts parent=[int] row path"
```

---

### Task 8: Add `findRecursive` walker

**Files:**
- Modify: `src/probe/introspection/model_navigator.h`
- Modify: `src/probe/introspection/model_navigator.cpp`
- Modify: `tests/test_model_navigator.cpp`

- [ ] **Step 1: Write the failing tests**

Add to `TestModelNavigator` private slots:

```cpp
  void testFindRecursiveExactMatch();
  void testFindRecursiveContainsMultipleLevels();
  void testFindRecursiveRespectsMaxHits();
  void testFindRecursiveScopesToParent();
  void testFindRecursiveFindsLazyChildren();
  void testFindRecursiveRegex();
  void testFindRecursiveInvalidRegex();
```

Tests (pick sensible locations):

```cpp
void TestModelNavigator::testFindRecursiveExactMatch() {
  auto* tree = new QStandardItemModel(this);
  auto* etc = new QStandardItem("ETC");
  etc->appendRow(new QStandardItem("fos4 Fresnel"));
  tree->appendRow(etc);
  tree->appendRow(new QStandardItem("Martin"));

  ModelNavigator::FindOptions opts;
  opts.value = "Martin";
  opts.match = ModelNavigator::MatchMode::Exact;
  opts.maxHits = 10;

  QJsonArray matches;
  bool truncated = ModelNavigator::findRecursive(tree, QModelIndex(), opts, matches);
  QCOMPARE(matches.size(), 1);
  QCOMPARE(truncated, false);
  QJsonArray path = matches[0].toObject()["path"].toArray();
  QCOMPARE(path.size(), 1);
  QCOMPARE(path[0].toInt(), 1);

  delete tree;
}

void TestModelNavigator::testFindRecursiveContainsMultipleLevels() {
  auto* tree = new QStandardItemModel(this);
  auto* a = new QStandardItem("ETC");
  a->appendRow(new QStandardItem("fos4 Fresnel"));
  a->appendRow(new QStandardItem("ColorSource"));
  tree->appendRow(a);
  auto* b = new QStandardItem("Martin");
  b->appendRow(new QStandardItem("MAC Fresnel"));
  tree->appendRow(b);

  ModelNavigator::FindOptions opts;
  opts.value = "Fresnel";
  opts.match = ModelNavigator::MatchMode::Contains;
  opts.maxHits = 10;

  QJsonArray matches;
  ModelNavigator::findRecursive(tree, QModelIndex(), opts, matches);
  QCOMPARE(matches.size(), 2);

  delete tree;
}

void TestModelNavigator::testFindRecursiveRespectsMaxHits() {
  auto* tree = new QStandardItemModel(this);
  for (int i = 0; i < 5; ++i) tree->appendRow(new QStandardItem("match"));

  ModelNavigator::FindOptions opts;
  opts.value = "match";
  opts.match = ModelNavigator::MatchMode::Exact;
  opts.maxHits = 2;

  QJsonArray matches;
  bool truncated = ModelNavigator::findRecursive(tree, QModelIndex(), opts, matches);
  QCOMPARE(matches.size(), 2);
  QCOMPARE(truncated, true);

  delete tree;
}

void TestModelNavigator::testFindRecursiveScopesToParent() {
  auto* tree = new QStandardItemModel(this);
  auto* a = new QStandardItem("A");
  a->appendRow(new QStandardItem("target"));
  auto* b = new QStandardItem("B");
  b->appendRow(new QStandardItem("target"));
  tree->appendRow(a);
  tree->appendRow(b);

  ModelNavigator::FindOptions opts;
  opts.value = "target";
  opts.match = ModelNavigator::MatchMode::Exact;
  opts.maxHits = 10;

  QJsonArray matches;
  ModelNavigator::findRecursive(tree, tree->index(0, 0), opts, matches);
  QCOMPARE(matches.size(), 1);

  delete tree;
}

void TestModelNavigator::testFindRecursiveFindsLazyChildren() {
  LazyFlatModel lazy(20, this);

  ModelNavigator::FindOptions opts;
  opts.value = "Row15";
  opts.match = ModelNavigator::MatchMode::Exact;
  opts.maxHits = 10;

  QJsonArray matches;
  ModelNavigator::findRecursive(&lazy, QModelIndex(), opts, matches);
  QCOMPARE(matches.size(), 1);
  QCOMPARE(lazy.fetchedRows(), 20);
}

void TestModelNavigator::testFindRecursiveRegex() {
  auto* tree = new QStandardItemModel(this);
  tree->appendRow(new QStandardItem("foo123"));
  tree->appendRow(new QStandardItem("bar"));
  tree->appendRow(new QStandardItem("foo456"));

  ModelNavigator::FindOptions opts;
  opts.value = "foo[0-9]+";
  opts.match = ModelNavigator::MatchMode::Regex;
  opts.maxHits = 10;

  QJsonArray matches;
  ModelNavigator::findRecursive(tree, QModelIndex(), opts, matches);
  QCOMPARE(matches.size(), 2);

  delete tree;
}

void TestModelNavigator::testFindRecursiveInvalidRegex() {
  auto* tree = new QStandardItemModel(this);
  tree->appendRow(new QStandardItem("x"));

  ModelNavigator::FindOptions opts;
  opts.value = "[unclosed";
  opts.match = ModelNavigator::MatchMode::Regex;
  opts.maxHits = 10;

  QJsonArray matches;
  QString err;
  bool ok = ModelNavigator::compileFindOptions(opts, &err);
  QCOMPARE(ok, false);
  QVERIFY(!err.isEmpty());

  delete tree;
}
```

- [ ] **Step 2: Run to verify failure**

```bash
cmake --build build --config Release --target test_model_navigator
```
Expected: build error — `FindOptions`, `MatchMode`, `findRecursive`, `compileFindOptions` not declared.

- [ ] **Step 3: Add declarations to `model_navigator.h`**

Add before the `private:` line:

```cpp
  /// Match modes for findRecursive.
  enum class MatchMode { Exact, Contains, StartsWith, EndsWith, Regex };

  /// Options bag for findRecursive.
  struct FindOptions {
    QString value;
    int column = 0;
    int role = Qt::DisplayRole;
    MatchMode match = MatchMode::Contains;
    int maxHits = 10;    // -1 = unlimited
    // Compiled regex lives on the options struct so the walker reuses it.
    QRegularExpression compiledRegex;
  };

  /// @brief Pre-compile regex (if applicable) and return false on bad pattern.
  /// @param opts Options to compile. `opts.compiledRegex` is updated in place.
  /// @param outError Optional error message on failure.
  static bool compileFindOptions(FindOptions& opts, QString* outError = nullptr);

  /// @brief Depth-first search of `parent`'s subtree for rows whose cell at
  /// `opts.column` matches `opts.value` under `opts.role` using `opts.match`.
  /// Calls ensureFetched before every rowCount/index. Appends row data (via
  /// indexToRowData) to `outMatches`. Returns true if truncated at maxHits.
  /// @param model The model.
  /// @param parentIdx Starting parent (invalid = root).
  /// @param opts Find options (regex must be pre-compiled via compileFindOptions).
  /// @param outMatches JSON array to append matches to.
  /// @return true if the walk was truncated by maxHits; false if it ran to completion.
  static bool findRecursive(QAbstractItemModel* model, const QModelIndex& parentIdx,
                            FindOptions& opts, QJsonArray& outMatches);
```

Add `#include <QRegularExpression>` to the header's includes.

- [ ] **Step 4: Implement in `model_navigator.cpp`**

Add `#include <QRegularExpression>` at the top if not already present. Add these before `resolveModel`:

```cpp
bool ModelNavigator::compileFindOptions(FindOptions& opts, QString* outError) {
  if (opts.match != MatchMode::Regex) return true;
  opts.compiledRegex.setPattern(opts.value);
  opts.compiledRegex.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
  if (!opts.compiledRegex.isValid()) {
    if (outError) *outError = opts.compiledRegex.errorString();
    return false;
  }
  return true;
}

namespace {
bool matchesValue(const QString& cell, const ModelNavigator::FindOptions& opts) {
  switch (opts.match) {
    case ModelNavigator::MatchMode::Exact:
      return cell.compare(opts.value, Qt::CaseInsensitive) == 0;
    case ModelNavigator::MatchMode::Contains:
      return cell.contains(opts.value, Qt::CaseInsensitive);
    case ModelNavigator::MatchMode::StartsWith:
      return cell.startsWith(opts.value, Qt::CaseInsensitive);
    case ModelNavigator::MatchMode::EndsWith:
      return cell.endsWith(opts.value, Qt::CaseInsensitive);
    case ModelNavigator::MatchMode::Regex:
      return opts.compiledRegex.match(cell).hasMatch();
  }
  return false;
}
}  // namespace

bool ModelNavigator::findRecursive(QAbstractItemModel* model, const QModelIndex& parentIdx,
                                   FindOptions& opts, QJsonArray& outMatches) {
  if (!model) return false;
  ensureFetched(model, parentIdx);
  const int rows = model->rowCount(parentIdx);
  for (int row = 0; row < rows; ++row) {
    const QModelIndex cellIdx = model->index(row, opts.column, parentIdx);
    if (cellIdx.isValid()) {
      const QString cellText = model->data(cellIdx, opts.role).toString();
      if (matchesValue(cellText, opts)) {
        const QModelIndex rowIdx = model->index(row, 0, parentIdx);
        outMatches.append(indexToRowData(model, rowIdx, {opts.role}));
        if (opts.maxHits >= 0 && outMatches.size() >= opts.maxHits) return true;
      }
    }
    const QModelIndex childParent = model->index(row, 0, parentIdx);
    if (model->hasChildren(childParent)) {
      if (findRecursive(model, childParent, opts, outMatches)) return true;
    }
  }
  return false;
}
```

- [ ] **Step 5: Run tests to verify they pass**

```bash
cmake --build build --config Release --target test_model_navigator
build/bin/Release/test_model_navigator.exe -platform minimal
```
Expected: all new `testFindRecursive*` PASS.

- [ ] **Step 6: Commit**

```bash
git add src/probe/introspection/model_navigator.{h,cpp} tests/test_model_navigator.cpp
git commit -m "feat: add ModelNavigator::findRecursive with lazy-model walker"
```

---

### Task 9: Register `qt.models.find` handler

**Files:**
- Modify: `src/probe/api/native_mode_api.cpp` (in `registerModelMethods`, after the data handler)
- Modify: `tests/test_model_navigator.cpp`

- [ ] **Step 1: Write the failing tests**

Add to `TestModelNavigator` private slots:

```cpp
  void testApiModelsFindExact();
  void testApiModelsFindContainsReturnsPathAndCells();
  void testApiModelsFindScopesToParent();
  void testApiModelsFindTruncated();
  void testApiModelsFindInvalidRegexError();
  void testApiModelsFindRoleNotFoundError();
```

Tests:

```cpp
void TestModelNavigator::testApiModelsFindExact() {
  QString modelId = ObjectRegistry::instance()->objectId(m_smallModel);
  QJsonValue result = callResult("qt.models.find",
                                 QJsonObject{{"objectId", modelId},
                                             {"value", "B1"},
                                             {"match", "exact"}});
  QJsonObject data = result.toObject();
  QCOMPARE(data["count"].toInt(), 1);
  QJsonArray matches = data["matches"].toArray();
  QCOMPARE(matches[0].toObject()["path"].toArray()[0].toInt(), 1);
}

void TestModelNavigator::testApiModelsFindContainsReturnsPathAndCells() {
  QString modelId = ObjectRegistry::instance()->objectId(m_smallModel);
  QJsonValue result = callResult("qt.models.find",
                                 QJsonObject{{"objectId", modelId},
                                             {"value", "1"},
                                             {"match", "contains"}});
  QJsonObject data = result.toObject();
  // A1, B1, C1 contain "1" in column 0 → 3 matches.
  QCOMPARE(data["count"].toInt(), 3);
  QJsonArray first = data["matches"].toArray()[0].toObject()["cells"].toArray();
  QVERIFY(!first.isEmpty());
}

void TestModelNavigator::testApiModelsFindScopesToParent() {
  auto* tree = new QStandardItemModel(this);
  auto* a = new QStandardItem("A");
  a->appendRow(new QStandardItem("target"));
  auto* b = new QStandardItem("B");
  b->appendRow(new QStandardItem("target"));
  tree->appendRow(a);
  tree->appendRow(b);
  tree->setObjectName("scopedTree");
  ObjectRegistry::instance()->scanExistingObjects(tree);
  QString id = ObjectRegistry::instance()->objectId(tree);

  QJsonValue result = callResult("qt.models.find",
                                 QJsonObject{{"objectId", id},
                                             {"value", "target"},
                                             {"match", "exact"},
                                             {"parent", QJsonArray{0}}});
  QCOMPARE(result.toObject()["count"].toInt(), 1);
}

void TestModelNavigator::testApiModelsFindTruncated() {
  QString modelId = ObjectRegistry::instance()->objectId(m_largeModel);
  QJsonValue result = callResult("qt.models.find",
                                 QJsonObject{{"objectId", modelId},
                                             {"value", "Row"},
                                             {"match", "contains"},
                                             {"maxHits", 3}});
  QJsonObject data = result.toObject();
  QCOMPARE(data["count"].toInt(), 3);
  QCOMPARE(data["truncated"].toBool(), true);
}

void TestModelNavigator::testApiModelsFindInvalidRegexError() {
  QString modelId = ObjectRegistry::instance()->objectId(m_smallModel);
  QJsonObject error = callExpectError("qt.models.find",
                                      QJsonObject{{"objectId", modelId},
                                                  {"value", "[unclosed"},
                                                  {"match", "regex"}});
  QCOMPARE(error["code"].toInt(), ErrorCode::kInvalidRegex);
  QVERIFY(error["data"].toObject().contains("pattern"));
  QVERIFY(error["data"].toObject().contains("error"));
}

void TestModelNavigator::testApiModelsFindRoleNotFoundError() {
  QString modelId = ObjectRegistry::instance()->objectId(m_smallModel);
  QJsonObject error = callExpectError("qt.models.find",
                                      QJsonObject{{"objectId", modelId},
                                                  {"value", "x"},
                                                  {"role", "nonexistent"}});
  QCOMPARE(error["code"].toInt(), ErrorCode::kModelRoleNotFound);
}
```

- [ ] **Step 2: Run to verify failure**

```bash
cmake --build build --config Release --target test_model_navigator
build/bin/Release/test_model_navigator.exe -platform minimal testApiModelsFindExact
```
Expected: FAIL — method `qt.models.find` not registered.

- [ ] **Step 3: Register the handler**

In `native_mode_api.cpp`, inside `registerModelMethods()`, add after the existing `qt.models.data` handler (inside the same function body, before the closing `}`):

```cpp
  // qt.models.find - recursive value search with match modes (lazy-aware)
  m_handler->RegisterMethod(QStringLiteral("qt.models.find"), [](const QString& params) -> QString {
    auto p = parseParams(params);
    QObject* obj = resolveObjectParam(p, QStringLiteral("qt.models.find"));
    QString objectId = p[QStringLiteral("objectId")].toString();

    QAbstractItemModel* model = ModelNavigator::resolveModel(obj);
    if (!model) {
      throw JsonRpcException(
          ErrorCode::kNotAModel,
          QStringLiteral("Object is not a model and does not have an associated model"),
          QJsonObject{{QStringLiteral("objectId"), objectId}});
    }

    // parent
    QList<int> parentPath;
    QJsonArray parentArr = p[QStringLiteral("parent")].toArray();
    for (const QJsonValue& v : parentArr) parentPath.append(v.toInt());

    QModelIndex parentIdx;
    if (!parentPath.isEmpty()) {
      int failed = -1;
      parentIdx = ModelNavigator::pathToIndex(model, parentPath, &failed);
      if (!parentIdx.isValid()) {
        throw JsonRpcException(
            ErrorCode::kInvalidParentPath,
            QStringLiteral("Parent path invalid at segment %1").arg(failed),
            QJsonObject{{QStringLiteral("path"), parentArr},
                        {QStringLiteral("failedSegment"), failed}});
      }
    }

    // opts
    ModelNavigator::FindOptions opts;
    opts.value = p[QStringLiteral("value")].toString();
    opts.column = p[QStringLiteral("column")].toInt(0);

    QJsonValue roleVal = p[QStringLiteral("role")];
    if (roleVal.isDouble()) {
      opts.role = roleVal.toInt();
    } else {
      QString roleName = roleVal.toString(QStringLiteral("display"));
      int roleId = ModelNavigator::resolveRoleName(model, roleName);
      if (roleId < 0) {
        throw JsonRpcException(
            ErrorCode::kModelRoleNotFound, QStringLiteral("Role not found: %1").arg(roleName),
            QJsonObject{{QStringLiteral("roleName"), roleName},
                        {QStringLiteral("availableRoles"), ModelNavigator::getRoleNames(model)}});
      }
      opts.role = roleId;
    }

    QString matchMode = p[QStringLiteral("match")].toString(QStringLiteral("contains"));
    if      (matchMode == QStringLiteral("exact"))      opts.match = ModelNavigator::MatchMode::Exact;
    else if (matchMode == QStringLiteral("contains"))   opts.match = ModelNavigator::MatchMode::Contains;
    else if (matchMode == QStringLiteral("startsWith")) opts.match = ModelNavigator::MatchMode::StartsWith;
    else if (matchMode == QStringLiteral("endsWith"))   opts.match = ModelNavigator::MatchMode::EndsWith;
    else if (matchMode == QStringLiteral("regex"))      opts.match = ModelNavigator::MatchMode::Regex;
    else {
      throw JsonRpcException(
          JsonRpcError::kInvalidParams,
          QStringLiteral("Unknown match mode: %1").arg(matchMode),
          QJsonObject{{QStringLiteral("match"), matchMode}});
    }
    opts.maxHits = p[QStringLiteral("maxHits")].toInt(10);

    // Compile regex if needed.
    QString regexError;
    if (!ModelNavigator::compileFindOptions(opts, &regexError)) {
      throw JsonRpcException(
          ErrorCode::kInvalidRegex, QStringLiteral("Invalid regex: %1").arg(regexError),
          QJsonObject{{QStringLiteral("pattern"), opts.value},
                      {QStringLiteral("error"), regexError}});
    }

    QJsonArray matches;
    bool truncated = ModelNavigator::findRecursive(model, parentIdx, opts, matches);

    QJsonObject result;
    result[QStringLiteral("matches")] = matches;
    result[QStringLiteral("count")] = matches.size();
    result[QStringLiteral("truncated")] = truncated;
    return envelopeToString(ResponseEnvelope::wrap(result, objectId));
  });
```

- [ ] **Step 4: Run tests to verify they pass**

```bash
cmake --build build --config Release --target test_model_navigator
build/bin/Release/test_model_navigator.exe -platform minimal
```
Expected: all new `testApiModelsFind*` PASS.

- [ ] **Step 5: Commit**

```bash
git add src/probe/api/native_mode_api.cpp tests/test_model_navigator.cpp
git commit -m "feat: register qt.models.find JSON-RPC method"
```

---

### Task 10: Register `qt.ui.clickItem` — path mode with select action

**Files:**
- Modify: `src/probe/api/native_mode_api.cpp` (in `registerUiMethods`, after `qt.ui.sendKeys`)
- Modify: `tests/test_model_navigator.cpp`

- [ ] **Step 1: Write the failing tests**

Add to `TestModelNavigator` private slots:

```cpp
  void testApiUiClickItemPathSelect();
  void testApiUiClickItemMissingPathAndItemPath();
  void testApiUiClickItemBothPathAndItemPathError();
  void testApiUiClickItemInvalidPathError();
```

Tests:

```cpp
void TestModelNavigator::testApiUiClickItemPathSelect() {
  QString viewId = ObjectRegistry::instance()->objectId(m_tableView);
  QJsonValue result = callResult("qt.ui.clickItem",
                                 QJsonObject{{"objectId", viewId},
                                             {"path", QJsonArray{1}},
                                             {"action", "select"}});
  QJsonObject data = result.toObject();
  QCOMPARE(data["found"].toBool(), true);
  QCOMPARE(data["path"].toArray()[0].toInt(), 1);
  // Verify the view's current index reflects the selection.
  QCOMPARE(m_tableView->currentIndex().row(), 1);
}

void TestModelNavigator::testApiUiClickItemMissingPathAndItemPath() {
  QString viewId = ObjectRegistry::instance()->objectId(m_tableView);
  QJsonObject error = callExpectError("qt.ui.clickItem",
                                      QJsonObject{{"objectId", viewId},
                                                  {"action", "select"}});
  QCOMPARE(error["code"].toInt(), JsonRpcError::kInvalidParams);
}

void TestModelNavigator::testApiUiClickItemBothPathAndItemPathError() {
  QString viewId = ObjectRegistry::instance()->objectId(m_tableView);
  QJsonObject error = callExpectError("qt.ui.clickItem",
                                      QJsonObject{{"objectId", viewId},
                                                  {"path", QJsonArray{0}},
                                                  {"itemPath", QJsonArray{"A1"}}});
  QCOMPARE(error["code"].toInt(), JsonRpcError::kInvalidParams);
}

void TestModelNavigator::testApiUiClickItemInvalidPathError() {
  QString viewId = ObjectRegistry::instance()->objectId(m_tableView);
  QJsonObject error = callExpectError("qt.ui.clickItem",
                                      QJsonObject{{"objectId", viewId},
                                                  {"path", QJsonArray{99}}});
  QCOMPARE(error["code"].toInt(), ErrorCode::kItemNotFound);
  QJsonObject details = error["data"].toObject();
  QCOMPARE(details["mode"].toString(), QString("row"));
  QCOMPARE(details["failedSegment"].toInt(), 0);
  QCOMPARE(details["requestedRow"].toInt(), 99);
}
```

- [ ] **Step 2: Run to verify failure**

```bash
cmake --build build --config Release --target test_model_navigator
```
Expected: FAIL — `qt.ui.clickItem` not registered.

- [ ] **Step 3: Register the handler**

In `native_mode_api.cpp`, inside `registerUiMethods()`, after the `qt.ui.sendKeys` handler, add:

```cpp
  // qt.ui.clickItem - select/click/edit an item by itemPath (string[]) or path (int[]).
  m_handler->RegisterMethod(
      QStringLiteral("qt.ui.clickItem"), [](const QString& params) -> QString {
    auto p = parseParams(params);
    QObject* obj = resolveObjectParam(p, QStringLiteral("qt.ui.clickItem"));
    QString objectId = p[QStringLiteral("objectId")].toString();

    const bool hasPath = p.contains(QStringLiteral("path"));
    const bool hasItemPath = p.contains(QStringLiteral("itemPath"));
    if (hasPath == hasItemPath) {
      throw JsonRpcException(
          JsonRpcError::kInvalidParams,
          QStringLiteral("Exactly one of 'path' or 'itemPath' is required"),
          QJsonObject{{QStringLiteral("method"), QStringLiteral("qt.ui.clickItem")}});
    }

    auto* view = qobject_cast<QAbstractItemView*>(obj);
    if (!view) {
      throw JsonRpcException(
          ErrorCode::kNotAModel,
          QStringLiteral("Object is not a QAbstractItemView"),
          QJsonObject{{QStringLiteral("objectId"), objectId},
                      {QStringLiteral("className"),
                       QString::fromUtf8(obj->metaObject()->className())}});
    }
    QAbstractItemModel* model = view->model();
    if (!model) {
      throw JsonRpcException(
          ErrorCode::kNotAModel, QStringLiteral("View has no model"),
          QJsonObject{{QStringLiteral("objectId"), objectId}});
    }

    const int column = p[QStringLiteral("column")].toInt(0);
    const QString action = p[QStringLiteral("action")].toString(QStringLiteral("click"));

    // Resolve the row-identity index.
    QModelIndex rowTarget;
    int failedSegment = -1;
    QJsonObject notFoundDetail;

    if (hasPath) {
      QList<int> rowPath;
      QJsonArray pathArr = p[QStringLiteral("path")].toArray();
      for (const QJsonValue& v : pathArr) rowPath.append(v.toInt());
      rowTarget = ModelNavigator::pathToIndex(model, rowPath, &failedSegment);
      if (!rowTarget.isValid()) {
        // Compute availableRows at failure point for richer detail.
        QModelIndex walk;
        for (int i = 0; i < failedSegment; ++i) {
          ModelNavigator::ensureFetched(model, walk);
          walk = model->index(rowPath[i], 0, walk);
        }
        ModelNavigator::ensureFetched(model, walk);
        QJsonArray partial;
        for (int i = 0; i < failedSegment; ++i) partial.append(rowPath[i]);
        notFoundDetail = QJsonObject{{QStringLiteral("mode"), QStringLiteral("row")},
                                     {QStringLiteral("failedSegment"), failedSegment},
                                     {QStringLiteral("requestedRow"), rowPath.value(failedSegment, -1)},
                                     {QStringLiteral("availableRows"), model->rowCount(walk)},
                                     {QStringLiteral("partialPath"), partial}};
      }
    } else {
      QStringList itemPath;
      QJsonArray ipArr = p[QStringLiteral("itemPath")].toArray();
      for (const QJsonValue& v : ipArr) itemPath.append(v.toString());
      rowTarget = ModelNavigator::textPathToIndex(model, itemPath, column, &failedSegment);
      if (!rowTarget.isValid()) {
        QJsonArray partial;
        // Re-walk to produce partialPath as row indices.
        QModelIndex walk;
        for (int i = 0; i < failedSegment; ++i) {
          ModelNavigator::ensureFetched(model, walk);
          const int rows = model->rowCount(walk);
          for (int r = 0; r < rows; ++r) {
            const QModelIndex cell = model->index(r, column, walk);
            if (model->data(cell, Qt::DisplayRole).toString() == itemPath[i]) {
              partial.append(r);
              walk = model->index(r, 0, walk);
              break;
            }
          }
        }
        notFoundDetail = QJsonObject{{QStringLiteral("mode"), QStringLiteral("text")},
                                     {QStringLiteral("failedSegment"), failedSegment},
                                     {QStringLiteral("segmentText"), itemPath.value(failedSegment)},
                                     {QStringLiteral("partialPath"), partial}};
      }
    }

    if (!rowTarget.isValid()) {
      throw JsonRpcException(ErrorCode::kItemNotFound, QStringLiteral("Item not found"),
                             notFoundDetail);
    }

    // Validate action column.
    const int colCount = model->columnCount(rowTarget.parent());
    if (column < 0 || column >= colCount) {
      throw JsonRpcException(
          ErrorCode::kInvalidColumn,
          QStringLiteral("Column %1 out of range (columnCount=%2)").arg(column).arg(colCount),
          QJsonObject{{QStringLiteral("column"), column},
                      {QStringLiteral("columnCount"), colCount}});
    }

    const QModelIndex actionTarget = model->index(rowTarget.row(), column, rowTarget.parent());

    // Action dispatch — for now, only "select" is implemented. Other actions land in Task 12.
    if (action == QStringLiteral("select")) {
      view->setCurrentIndex(actionTarget);
    } else {
      throw JsonRpcException(
          JsonRpcError::kInvalidParams,
          QStringLiteral("Action not yet implemented in this build: %1").arg(action),
          QJsonObject{{QStringLiteral("action"), action}});
    }

    QJsonObject result = ModelNavigator::indexToRowData(model, rowTarget, {Qt::DisplayRole});
    result[QStringLiteral("found")] = true;
    result[QStringLiteral("action")] = action;
    if (hasItemPath) result[QStringLiteral("itemPath")] = p[QStringLiteral("itemPath")];
    return envelopeToString(ResponseEnvelope::wrap(result, objectId));
  });
```

Also ensure `#include <QAbstractItemView>` is present at the top (it should already be via model_navigator.cpp — add it here if compilation fails for this reason).

- [ ] **Step 4: Run tests to verify they pass**

```bash
cmake --build build --config Release --target test_model_navigator
build/bin/Release/test_model_navigator.exe -platform minimal
```
Expected: all new `testApiUiClickItem*` PASS.

- [ ] **Step 5: Commit**

```bash
git add src/probe/api/native_mode_api.cpp tests/test_model_navigator.cpp
git commit -m "feat: register qt.ui.clickItem (path mode, select action)"
```

---

### Task 11: `qt.ui.clickItem` — itemPath mode + text not-found detail

**Files:**
- Modify: `tests/test_model_navigator.cpp`

(Handler already supports itemPath from Task 10; this task adds test coverage specifically for the text mode.)

- [ ] **Step 1: Write the failing tests**

Add to `TestModelNavigator` private slots:

```cpp
  void testApiUiClickItemTextPathSelect();
  void testApiUiClickItemTextPathMissingSegment();
```

Tests:

```cpp
void TestModelNavigator::testApiUiClickItemTextPathSelect() {
  auto* tree = new QStandardItemModel(this);
  auto* etc = new QStandardItem("ETC");
  etc->appendRow(new QStandardItem("fos4 Fresnel"));
  tree->appendRow(etc);
  auto* view = new QTreeView();
  view->setObjectName("treeView");
  view->setModel(tree);
  view->show();
  QApplication::processEvents();
  ObjectRegistry::instance()->scanExistingObjects(tree);
  ObjectRegistry::instance()->scanExistingObjects(view);

  QString viewId = ObjectRegistry::instance()->objectId(view);
  QJsonValue result = callResult("qt.ui.clickItem",
                                 QJsonObject{{"objectId", viewId},
                                             {"itemPath", QJsonArray{"ETC", "fos4 Fresnel"}},
                                             {"action", "select"}});
  QJsonObject data = result.toObject();
  QCOMPARE(data["found"].toBool(), true);
  QJsonArray path = data["path"].toArray();
  QCOMPARE(path.size(), 2);
  QCOMPARE(path[0].toInt(), 0);
  QCOMPARE(path[1].toInt(), 0);

  delete view;
  delete tree;
}

void TestModelNavigator::testApiUiClickItemTextPathMissingSegment() {
  QString viewId = ObjectRegistry::instance()->objectId(m_tableView);
  QJsonObject error = callExpectError("qt.ui.clickItem",
                                      QJsonObject{{"objectId", viewId},
                                                  {"itemPath", QJsonArray{"not-a-value"}}});
  QCOMPARE(error["code"].toInt(), ErrorCode::kItemNotFound);
  QJsonObject details = error["data"].toObject();
  QCOMPARE(details["mode"].toString(), QString("text"));
  QCOMPARE(details["segmentText"].toString(), QString("not-a-value"));
}
```

Add `#include <QTreeView>` to the test file includes if not already present.

- [ ] **Step 2: Run tests**

```bash
cmake --build build --config Release --target test_model_navigator
build/bin/Release/test_model_navigator.exe -platform minimal
```
Expected: both tests PASS (handler already supports this).

- [ ] **Step 3: Commit**

```bash
git add tests/test_model_navigator.cpp
git commit -m "test: qt.ui.clickItem itemPath mode and text-not-found details"
```

---

### Task 12: `qt.ui.clickItem` — click, doubleClick, edit actions

**Files:**
- Modify: `src/probe/api/native_mode_api.cpp` (extend the action dispatch in the `qt.ui.clickItem` handler)
- Modify: `tests/test_model_navigator.cpp`

- [ ] **Step 1: Write the failing tests**

Add to `TestModelNavigator` private slots:

```cpp
  void testApiUiClickItemClickAction();
  void testApiUiClickItemDoubleClickAction();
  void testApiUiClickItemEditOpensEditor();
  void testApiUiClickItemEditInvalidColumnError();
  void testApiUiClickItemEditNonEditableError();
```

Tests:

```cpp
void TestModelNavigator::testApiUiClickItemClickAction() {
  QString viewId = ObjectRegistry::instance()->objectId(m_tableView);
  callResult("qt.ui.clickItem",
             QJsonObject{{"objectId", viewId},
                         {"path", QJsonArray{1}},
                         {"action", "click"}});
  QCOMPARE(m_tableView->currentIndex().row(), 1);
}

void TestModelNavigator::testApiUiClickItemDoubleClickAction() {
  QString viewId = ObjectRegistry::instance()->objectId(m_tableView);
  QJsonValue result = callResult("qt.ui.clickItem",
                                 QJsonObject{{"objectId", viewId},
                                             {"path", QJsonArray{0}},
                                             {"action", "doubleClick"}});
  QCOMPARE(result.toObject()["found"].toBool(), true);
  QCOMPARE(m_tableView->currentIndex().row(), 0);
}

void TestModelNavigator::testApiUiClickItemEditOpensEditor() {
  // QStandardItem cells are editable by default.
  QString viewId = ObjectRegistry::instance()->objectId(m_tableView);
  QJsonValue result = callResult("qt.ui.clickItem",
                                 QJsonObject{{"objectId", viewId},
                                             {"path", QJsonArray{0}},
                                             {"action", "edit"},
                                             {"column", 0},
                                             {"editColumn", 1}});
  QCOMPARE(result.toObject()["found"].toBool(), true);
  // Editor widget should now be open on cell (0,1).
  QVERIFY(m_tableView->indexWidget(m_tableView->model()->index(0, 1)) != nullptr
          || m_tableView->state() == QAbstractItemView::EditingState);
}

void TestModelNavigator::testApiUiClickItemEditInvalidColumnError() {
  QString viewId = ObjectRegistry::instance()->objectId(m_tableView);
  QJsonObject error = callExpectError("qt.ui.clickItem",
                                      QJsonObject{{"objectId", viewId},
                                                  {"path", QJsonArray{0}},
                                                  {"action", "edit"},
                                                  {"editColumn", 99}});
  QCOMPARE(error["code"].toInt(), ErrorCode::kInvalidColumn);
  QCOMPARE(error["data"].toObject()["editColumn"].toInt(), 99);
}

void TestModelNavigator::testApiUiClickItemEditNonEditableError() {
  // Build a read-only model.
  auto* model = new QStandardItemModel(1, 1, this);
  auto* item = new QStandardItem("readonly");
  item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);  // NOT editable
  model->setItem(0, 0, item);
  auto* view = new QTableView();
  view->setObjectName("readonlyView");
  view->setModel(model);
  view->show();
  QApplication::processEvents();
  ObjectRegistry::instance()->scanExistingObjects(model);
  ObjectRegistry::instance()->scanExistingObjects(view);

  QString viewId = ObjectRegistry::instance()->objectId(view);
  QJsonObject error = callExpectError("qt.ui.clickItem",
                                      QJsonObject{{"objectId", viewId},
                                                  {"path", QJsonArray{0}},
                                                  {"action", "edit"}});
  QCOMPARE(error["code"].toInt(), ErrorCode::kNotEditable);

  delete view;
  delete model;
}
```

- [ ] **Step 2: Run to verify failure**

```bash
cmake --build build --config Release --target test_model_navigator
build/bin/Release/test_model_navigator.exe -platform minimal testApiUiClickItemClickAction
```
Expected: FAIL — handler throws "Action not yet implemented" for click/doubleClick/edit.

- [ ] **Step 3: Extend the action dispatch**

In `native_mode_api.cpp`, locate the `qt.ui.clickItem` handler's action dispatch (`if (action == "select")`). Replace the dispatch block with:

```cpp
    if (action == QStringLiteral("select")) {
      view->setCurrentIndex(actionTarget);
    } else if (action == QStringLiteral("click")) {
      view->setCurrentIndex(actionTarget);
      const QRect rect = view->visualRect(actionTarget);
      InputSimulator::mouseClick(view->viewport(), InputSimulator::MouseButton::Left,
                                 rect.center());
    } else if (action == QStringLiteral("doubleClick")) {
      view->setCurrentIndex(actionTarget);
      const QRect rect = view->visualRect(actionTarget);
      InputSimulator::mouseDoubleClick(view->viewport(), InputSimulator::MouseButton::Left,
                                       rect.center());
    } else if (action == QStringLiteral("edit")) {
      const int editColumn = p.contains(QStringLiteral("editColumn"))
                                 ? p[QStringLiteral("editColumn")].toInt()
                                 : column;
      if (editColumn < 0 || editColumn >= colCount) {
        throw JsonRpcException(
            ErrorCode::kInvalidColumn,
            QStringLiteral("editColumn %1 out of range (columnCount=%2)")
                .arg(editColumn).arg(colCount),
            QJsonObject{{QStringLiteral("editColumn"), editColumn},
                        {QStringLiteral("columnCount"), colCount}});
      }
      const QModelIndex editIdx =
          model->index(rowTarget.row(), editColumn, rowTarget.parent());
      if (!(model->flags(editIdx) & Qt::ItemIsEditable)) {
        QJsonArray pathOut;
        for (QModelIndex w = editIdx; w.isValid(); w = w.parent())
          pathOut.prepend(w.row());
        throw JsonRpcException(
            ErrorCode::kNotEditable, QStringLiteral("Cell is not editable"),
            QJsonObject{{QStringLiteral("path"), pathOut},
                        {QStringLiteral("editColumn"), editColumn}});
      }
      view->setCurrentIndex(editIdx);
      view->edit(editIdx);
    } else {
      throw JsonRpcException(
          JsonRpcError::kInvalidParams,
          QStringLiteral("Unknown action: %1").arg(action),
          QJsonObject{{QStringLiteral("action"), action},
                      {QStringLiteral("validActions"),
                       QJsonArray{QStringLiteral("select"), QStringLiteral("click"),
                                  QStringLiteral("doubleClick"), QStringLiteral("edit")}}});
    }
```

- [ ] **Step 4: Run tests to verify they pass**

```bash
cmake --build build --config Release --target test_model_navigator
build/bin/Release/test_model_navigator.exe -platform minimal
```
Expected: all `testApiUiClickItem*` PASS.

- [ ] **Step 5: Commit**

```bash
git add src/probe/api/native_mode_api.cpp tests/test_model_navigator.cpp
git commit -m "feat: qt.ui.clickItem supports click, doubleClick, edit actions"
```

---

### Task 13: `qt.ui.clickItem` — QComboBox specialization

**Files:**
- Modify: `src/probe/api/native_mode_api.cpp` (extend the `qt.ui.clickItem` handler's view resolution)
- Modify: `tests/test_model_navigator.cpp`

- [ ] **Step 1: Write the failing tests**

Add to `TestModelNavigator` private slots:

```cpp
  void testApiUiClickItemComboBoxSelectsValue();
  void testApiUiClickItemComboBoxEditReturnsNotEditable();
  void testApiUiClickItemComboBoxRejectsNonZeroColumn();
```

Tests (add `#include <QComboBox>` to test file if missing):

```cpp
void TestModelNavigator::testApiUiClickItemComboBoxSelectsValue() {
  auto* combo = new QComboBox();
  combo->setObjectName("combo1");
  combo->addItems({"Alpha", "Beta", "Gamma"});
  combo->show();
  QApplication::processEvents();
  ObjectRegistry::instance()->scanExistingObjects(combo);

  QString id = ObjectRegistry::instance()->objectId(combo);
  callResult("qt.ui.clickItem",
             QJsonObject{{"objectId", id},
                         {"itemPath", QJsonArray{"Beta"}},
                         {"action", "select"}});
  QCOMPARE(combo->currentIndex(), 1);

  delete combo;
}

void TestModelNavigator::testApiUiClickItemComboBoxEditReturnsNotEditable() {
  auto* combo = new QComboBox();
  combo->setObjectName("combo2");
  combo->addItems({"X"});
  combo->show();
  QApplication::processEvents();
  ObjectRegistry::instance()->scanExistingObjects(combo);

  QString id = ObjectRegistry::instance()->objectId(combo);
  QJsonObject error = callExpectError("qt.ui.clickItem",
                                      QJsonObject{{"objectId", id},
                                                  {"itemPath", QJsonArray{"X"}},
                                                  {"action", "edit"}});
  QCOMPARE(error["code"].toInt(), ErrorCode::kNotEditable);

  delete combo;
}

void TestModelNavigator::testApiUiClickItemComboBoxRejectsNonZeroColumn() {
  auto* combo = new QComboBox();
  combo->setObjectName("combo3");
  combo->addItems({"Y"});
  combo->show();
  QApplication::processEvents();
  ObjectRegistry::instance()->scanExistingObjects(combo);

  QString id = ObjectRegistry::instance()->objectId(combo);
  QJsonObject error = callExpectError("qt.ui.clickItem",
                                      QJsonObject{{"objectId", id},
                                                  {"itemPath", QJsonArray{"Y"}},
                                                  {"column", 1}});
  QCOMPARE(error["code"].toInt(), ErrorCode::kInvalidColumn);

  delete combo;
}
```

- [ ] **Step 2: Run to verify failure**

```bash
cmake --build build --config Release --target test_model_navigator
build/bin/Release/test_model_navigator.exe -platform minimal testApiUiClickItemComboBoxSelectsValue
```
Expected: FAIL — current handler rejects non-`QAbstractItemView` objects with `kNotAModel`.

- [ ] **Step 3: Add QComboBox branch**

In `native_mode_api.cpp`, add `#include <QComboBox>` near the other Qt widget includes if not present. In the `qt.ui.clickItem` handler, replace the current view-resolution block with:

```cpp
    auto* view = qobject_cast<QAbstractItemView*>(obj);
    auto* combo = view ? nullptr : qobject_cast<QComboBox*>(obj);
    QAbstractItemModel* model = nullptr;

    if (view) {
      model = view->model();
    } else if (combo) {
      model = combo->model();
    }

    if (!model) {
      throw JsonRpcException(
          ErrorCode::kNotAModel,
          QStringLiteral("Object is not a view or combo box with a model"),
          QJsonObject{{QStringLiteral("objectId"), objectId},
                      {QStringLiteral("className"),
                       QString::fromUtf8(obj->metaObject()->className())}});
    }
```

Then, in the action dispatch, wrap each action in a combo-aware branch. Replace the action block built in Task 12 with:

```cpp
    if (combo) {
      // Combo boxes: length-1 paths only; actions collapse to setCurrentIndex.
      // Path length already validated implicitly — combo models are flat, so any
      // multi-level path fails textPathToIndex/pathToIndex at segment 1.
      if (column != 0) {
        throw JsonRpcException(
            ErrorCode::kInvalidColumn,
            QStringLiteral("QComboBox only supports column 0"),
            QJsonObject{{QStringLiteral("column"), column},
                        {QStringLiteral("columnCount"), 1}});
      }
      if (action == QStringLiteral("edit")) {
        QJsonArray pathOut;
        pathOut.append(rowTarget.row());
        throw JsonRpcException(
            ErrorCode::kNotEditable,
            QStringLiteral("QComboBox cells are not inline-editable"),
            QJsonObject{{QStringLiteral("path"), pathOut},
                        {QStringLiteral("editColumn"), column}});
      }
      if (action == QStringLiteral("select") || action == QStringLiteral("click")
          || action == QStringLiteral("doubleClick")) {
        combo->setCurrentIndex(rowTarget.row());
      } else {
        throw JsonRpcException(
            JsonRpcError::kInvalidParams,
            QStringLiteral("Unknown action: %1").arg(action),
            QJsonObject{{QStringLiteral("action"), action}});
      }
    } else {
      // QAbstractItemView branch — original action dispatch from Task 12.
      // [...existing select/click/doubleClick/edit dispatch...]
    }
```

Keep the QAbstractItemView dispatch code from Task 12 inside the `else` branch. Also for combo, skip the `colCount` validation above the dispatch (it was validated against `columnCount(rowTarget.parent())`) — combo models are single-column, so the validation already passes or throws naturally. The explicit `column != 0` check in the combo branch makes the error message clearer.

Also: move the `const int colCount = ...` / `column` validation block so it runs only on the view branch:

```cpp
    const int colCount = combo ? 1 : model->columnCount(rowTarget.parent());
    if (column < 0 || column >= colCount) {
      throw JsonRpcException(
          ErrorCode::kInvalidColumn,
          QStringLiteral("Column %1 out of range (columnCount=%2)").arg(column).arg(colCount),
          QJsonObject{{QStringLiteral("column"), column},
                      {QStringLiteral("columnCount"), colCount}});
    }
```

The `column != 0` guard inside the combo branch becomes redundant with this — remove the duplicate check inside the combo branch.

- [ ] **Step 4: Also handle `expand`/`scroll` correctly for combo**

Combo actions should not call `view->scrollTo` or tree-expand logic (those are added in Task 12's view branch or via later task). Since the view-branch actions were gated inside `else { ... }`, combo actions naturally skip them. Verify by reading the final handler body.

- [ ] **Step 5: Run tests to verify they pass**

```bash
cmake --build build --config Release --target test_model_navigator
build/bin/Release/test_model_navigator.exe -platform minimal
```
Expected: all tests, including combo tests, PASS.

- [ ] **Step 6: Commit**

```bash
git add src/probe/api/native_mode_api.cpp tests/test_model_navigator.cpp
git commit -m "feat: qt.ui.clickItem supports QComboBox (setCurrentIndex, reject edit/non-zero column)"
```

---

### Task 14: Add `expand` and `scroll` handling to `qt.ui.clickItem` for tree views

**Files:**
- Modify: `src/probe/api/native_mode_api.cpp` (extend view branch of `qt.ui.clickItem`)
- Modify: `tests/test_model_navigator.cpp`

- [ ] **Step 1: Write the failing test**

Add to `TestModelNavigator` private slots:

```cpp
  void testApiUiClickItemExpandsAncestors();
```

Test:

```cpp
void TestModelNavigator::testApiUiClickItemExpandsAncestors() {
  auto* tree = new QStandardItemModel(this);
  auto* a = new QStandardItem("A");
  auto* aa = new QStandardItem("A.A");
  aa->appendRow(new QStandardItem("leaf"));
  a->appendRow(aa);
  tree->appendRow(a);

  auto* view = new QTreeView();
  view->setObjectName("expandTree");
  view->setModel(tree);
  view->show();
  QApplication::processEvents();
  ObjectRegistry::instance()->scanExistingObjects(tree);
  ObjectRegistry::instance()->scanExistingObjects(view);

  QString viewId = ObjectRegistry::instance()->objectId(view);

  // All ancestors start collapsed.
  QVERIFY(!view->isExpanded(tree->index(0, 0)));
  QVERIFY(!view->isExpanded(tree->index(0, 0, tree->index(0, 0))));

  callResult("qt.ui.clickItem",
             QJsonObject{{"objectId", viewId},
                         {"path", QJsonArray{0, 0, 0}},
                         {"action", "select"}});

  QVERIFY(view->isExpanded(tree->index(0, 0)));
  QVERIFY(view->isExpanded(tree->index(0, 0, tree->index(0, 0))));

  delete view;
  delete tree;
}
```

- [ ] **Step 2: Run to verify failure**

```bash
cmake --build build --config Release --target test_model_navigator
build/bin/Release/test_model_navigator.exe -platform minimal testApiUiClickItemExpandsAncestors
```
Expected: FAIL — ancestors still collapsed after select.

- [ ] **Step 3: Add expand + scroll before action dispatch (view branch)**

In the `else { ... }` (view branch) of the `qt.ui.clickItem` handler, just before the `if (action == "select") ...` dispatch, insert:

```cpp
      const bool wantExpand = p.contains(QStringLiteral("expand"))
                                 ? p[QStringLiteral("expand")].toBool()
                                 : true;
      const bool wantScroll = p.contains(QStringLiteral("scroll"))
                                 ? p[QStringLiteral("scroll")].toBool()
                                 : true;
      if (wantExpand) {
        if (auto* treeView = qobject_cast<QTreeView*>(view)) {
          for (QModelIndex anc = actionTarget.parent(); anc.isValid(); anc = anc.parent()) {
            treeView->expand(anc);
          }
        }
      }
      if (wantScroll) view->scrollTo(actionTarget);
```

Add `#include <QTreeView>` at the top of `native_mode_api.cpp` if not already present.

- [ ] **Step 4: Run test to verify it passes**

```bash
cmake --build build --config Release --target test_model_navigator
build/bin/Release/test_model_navigator.exe -platform minimal testApiUiClickItemExpandsAncestors
```
Expected: PASS.

- [ ] **Step 5: Run full suite**

```bash
build/bin/Release/test_model_navigator.exe -platform minimal
```
Expected: all tests PASS.

- [ ] **Step 6: Commit**

```bash
git add src/probe/api/native_mode_api.cpp tests/test_model_navigator.cpp
git commit -m "feat: qt.ui.clickItem expands ancestors and scrolls (tree views)"
```

---

### Task 15: Update Python `qt_models_data` wrapper

**Files:**
- Modify: `python/src/qtpilot/tools/native.py:410-436`

- [ ] **Step 1: Replace the wrapper body**

Replace the entire `qt_models_data` tool (from `@mcp.tool` through the `return` line) at approximately line 410 with:

```python
    @mcp.tool
    async def qt_models_data(
        objectId: str,
        parent: list[int] | None = None,
        offset: int | None = None,
        limit: int | None = None,
        roles: list[str] | None = None,
        ctx: Context = None,
    ) -> dict:
        """Read rows from a model/view, at any depth via `parent` row-path.

        `parent=[]` (or omitted) returns top-level rows. `parent=[0, 2]` returns
        the children of the third child of the first top-level row. Each returned
        row carries a full `path` field and a `hasChildren` flag so callers can
        recurse. Pagination via `offset`/`limit` applies to children of `parent`.
        Lazy models (canFetchMore) are force-fetched at each level.
        Example: qt_models_data(objectId="treeView", parent=[0], limit=50)
        """
        from qtpilot.server import require_probe

        params: dict = {"objectId": objectId}
        if parent is not None:
            params["parent"] = parent
        if offset is not None:
            params["offset"] = offset
        if limit is not None:
            params["limit"] = limit
        if roles is not None:
            params["roles"] = roles
        return await require_probe().call("qt.models.data", params)
```

- [ ] **Step 2: Run Python tool-registration smoke test**

```bash
cd python && pytest tests/test_tools.py::TestNativeTools -v
```
Expected: PASS (tool names unchanged; only parameters differ).

- [ ] **Step 3: Commit**

```bash
git add python/src/qtpilot/tools/native.py
git commit -m "feat(python): qt_models_data takes parent=[int] path + roles list"
```

---

### Task 16: Add Python `qt_models_find` wrapper

**Files:**
- Modify: `python/src/qtpilot/tools/native.py` (insert after `qt_models_data`)
- Modify: `python/tests/test_tools.py:28-54`

- [ ] **Step 1: Add the expected tool name to the Python test**

In `python/tests/test_tools.py`, add `"qt_models_find"` and `"qt_ui_clickItem"` to the `expected` set (line ~29) inside `test_native_tool_names`. Update the count assertion in `test_native_tools_registered` from `>= 32` to `>= 34`:

```python
    def test_native_tools_registered(self, mock_mcp):
        """Native mode registers >= 34 tools."""
        register_native_tools(mock_mcp)
        assert len(_tool_names(mock_mcp)) >= 34
```

And add these lines inside the `expected` set:

```python
            "qt_models_find",
            "qt_ui_clickItem",
```

- [ ] **Step 2: Run to verify failure**

```bash
cd python && pytest tests/test_tools.py::TestNativeTools -v
```
Expected: FAIL — missing tools.

- [ ] **Step 3: Add the `qt_models_find` wrapper**

Insert into `native.py` after the `qt_models_data` tool:

```python
    @mcp.tool
    async def qt_models_find(
        objectId: str,
        value: str,
        column: int = 0,
        role: str = "display",
        match: str = "contains",
        max_hits: int = 10,
        parent: list[int] | None = None,
        ctx: Context = None,
    ) -> dict:
        """Search a model recursively for rows whose cell value matches `value`.

        `match` one of "exact", "contains", "startsWith", "endsWith", "regex".
        Matching is case-insensitive. `max_hits=-1` = unlimited. `parent=[...]`
        restricts the search to that subtree. Lazy models are force-fetched at
        each level (no false negatives).
        Returns: {matches: [{path, cells}], count, truncated}.
        Example: qt_models_find(objectId="treeView", value="Aura", match="contains")
        """
        from qtpilot.server import require_probe

        params: dict = {
            "objectId": objectId,
            "value": value,
            "column": column,
            "role": role,
            "match": match,
            "maxHits": max_hits,
        }
        if parent is not None:
            params["parent"] = parent
        return await require_probe().call("qt.models.find", params)
```

- [ ] **Step 4: Run Python tests**

```bash
cd python && pytest tests/test_tools.py::TestNativeTools -v
```
Expected: `qt_models_find` is now registered; `qt_ui_clickItem` still missing → next task.

- [ ] **Step 5: Commit**

```bash
git add python/src/qtpilot/tools/native.py python/tests/test_tools.py
git commit -m "feat(python): add qt_models_find wrapper"
```

---

### Task 17: Add Python `qt_ui_clickItem` wrapper

**Files:**
- Modify: `python/src/qtpilot/tools/native.py` (insert after `qt_models_find`)

- [ ] **Step 1: Add the wrapper**

```python
    @mcp.tool
    async def qt_ui_clickItem(
        objectId: str,
        itemPath: list[str] | None = None,
        path: list[int] | None = None,
        column: int = 0,
        action: str = "click",
        editColumn: int | None = None,
        expand: bool = True,
        scroll: bool = True,
        ctx: Context = None,
    ) -> dict:
        """Select / click / double-click / edit an item in a view.

        Exactly one of `itemPath` (exact display text per level) or `path`
        (int[] row path) must be provided. `column` selects which cell of the
        addressed row is acted on (and, for `itemPath`, which column's text is
        matched). `action` one of "select", "click", "doubleClick", "edit".
        Works on QTreeView, QTableView, QListView, QComboBox. For combo boxes,
        paths must be length 1 and `edit` returns kNotEditable.
        Example: qt_ui_clickItem(objectId="treeView", itemPath=["ETC","fos4 Fresnel"])
        """
        from qtpilot.server import require_probe

        if (itemPath is None) == (path is None):
            raise ValueError("Exactly one of itemPath or path must be provided")
        params: dict = {
            "objectId": objectId,
            "column": column,
            "action": action,
            "expand": expand,
            "scroll": scroll,
        }
        if itemPath is not None:
            params["itemPath"] = itemPath
        if path is not None:
            params["path"] = path
        if editColumn is not None:
            params["editColumn"] = editColumn
        return await require_probe().call("qt.ui.clickItem", params)
```

- [ ] **Step 2: Run Python tests**

```bash
cd python && pytest tests/test_tools.py -v
```
Expected: all PASS.

- [ ] **Step 3: Commit**

```bash
git add python/src/qtpilot/tools/native.py
git commit -m "feat(python): add qt_ui_clickItem wrapper"
```

---

### Task 18: Add `treeTab` with `QTreeView` to demo app

**Files:**
- Modify: `test_app/mainwindow.ui`
- Modify: `test_app/mainwindow.cpp`
- Modify: `test_app/mainwindow.h`

- [ ] **Step 1: Add the tab to `mainwindow.ui`**

In `mainwindow.ui`, locate the `<widget class="QTabWidget" name="tabWidget">` and find the closing of the `tableTab` widget (the `</widget>` that closes `tableTab`'s `<widget class="QWidget" name="tableTab">`). After that closing tag (but before the `QTabWidget`'s closing `</widget>`), insert:

```xml
      <widget class="QWidget" name="treeTab">
       <attribute name="title">
        <string>Tree</string>
       </attribute>
       <layout class="QVBoxLayout" name="treeLayout">
        <item>
         <widget class="QTreeView" name="treeView">
          <property name="objectName">
           <string>treeView</string>
          </property>
         </widget>
        </item>
       </layout>
      </widget>
```

- [ ] **Step 2: Add the model member to `mainwindow.h`**

Add include and forward declaration at the top of `mainwindow.h`:

```cpp
#include <QStandardItemModel>
```

Add a private member:

```cpp
  QStandardItemModel* treeModel_ = nullptr;
```

- [ ] **Step 3: Populate the model in `mainwindow.cpp`**

Add `#include <QStandardItem>` at the top. In the `MainWindow` constructor, after `ui_->setupUi(this);` but before the other widget setup, insert:

```cpp
  // Populate the tree model for the Tree tab.
  // 3-level hierarchy + a synthetic 1000-row parent to exercise pagination.
  treeModel_ = new QStandardItemModel(this);
  treeModel_->setHorizontalHeaderLabels({"Name", "Type", "Count"});

  auto addRow = [](QStandardItem* parent, const QString& name, const QString& type,
                   const QString& count) {
    QList<QStandardItem*> row{new QStandardItem(name), new QStandardItem(type),
                              new QStandardItem(count)};
    parent->appendRow(row);
    return row.front();
  };
  auto addTopRow = [this](const QString& name, const QString& type, const QString& count) {
    QList<QStandardItem*> row{new QStandardItem(name), new QStandardItem(type),
                              new QStandardItem(count)};
    treeModel_->appendRow(row);
    return row.front();
  };

  QStandardItem* etc = addTopRow("ETC", "Manufacturer", "");
  QStandardItem* fos4 = addRow(etc, "fos4 Fresnel Lustr X8 direct", "Fixture", "0");
  addRow(fos4, "Mode 8ch", "Mode", "0");
  addRow(fos4, "Mode 12ch", "Mode", "0");
  addRow(etc, "ColorSource PAR", "Fixture", "0");

  QStandardItem* martin = addTopRow("Martin", "Manufacturer", "");
  QStandardItem* aura = addRow(martin, "MAC Aura XB", "Fixture", "0");
  addRow(aura, "Mode 8ch", "Mode", "0");
  addRow(aura, "Mode 12ch", "Mode", "0");

  // Synthetic 1200-row child set under a dedicated parent to exercise pagination.
  QStandardItem* bulk = addTopRow("BulkManufacturer", "Manufacturer", "");
  for (int i = 0; i < 1200; ++i) {
    addRow(bulk, QStringLiteral("Fixture %1").arg(i, 4, 10, QChar('0')),
           "Fixture", QString::number(i));
  }

  ui_->treeView->setModel(treeModel_);
  ui_->treeView->setObjectName(QStringLiteral("treeView"));
```

- [ ] **Step 4: Build and visually verify**

```bash
cmake --build build --config Release --target qtPilot_test_app
build/bin/Release/qtPilot-launcher.exe build/bin/Release/qtPilot-test-app.exe
```
Expected: the test app launches; switching to the "Tree" tab shows ETC, Martin, and BulkManufacturer at the top level, each expandable. Close the app manually.

- [ ] **Step 5: Commit**

```bash
git add test_app/mainwindow.{ui,cpp,h}
git commit -m "feat(testapp): add Tree tab with 3-level QStandardItemModel + 1200-row subtree"
```

---

### Task 19: Update `test-mcp-modes` skill with tree scenarios

**Files:**
- Modify: `.claude/skills/test-mcp-modes/SKILL.md:91-95`
- Modify: `.claude/skills/test-mcp-modes/SKILL.md:241` (results table)

- [ ] **Step 1: Fix the stale `qt_models_data` call**

Replace section 10 ("Models (table data)") at lines 91-95 with:

```markdown
10. **Models (table data, flat model):**
    - `qt_models_list()` — expect at least 2 models (tableWidget's internal model, treeView's model)
    - Pick the tableWidget model objectId from the list
    - `qt_models_info(objectId=<tableModel>)` — expect rows >= 3, columns >= 3
    - `qt_models_data(objectId=<tableModel>)` — expect a rows array with display cells; the first row should contain "Alpha"
```

- [ ] **Step 2: Add a new section 11 (Tree navigation) and renumber following sections**

Insert after the new section 10:

```markdown
11. **Tree navigation (qt_models_data with parent, qt_models_find, qt_ui_clickItem):**
    - Switch to the Tree tab: `qt_ui_click(objectId="treeTab")` — expect success
    - `qt_models_data(objectId="treeView")` — expect top-level rows (ETC, Martin, BulkManufacturer), each with `hasChildren=true`, each row having a `path` field
    - `qt_models_data(objectId="treeView", parent=[0])` — expect ETC's children (fos4..., ColorSource PAR), `path` starts with `[0, ...]`
    - `qt_models_data(objectId="treeView", parent=[0, 0])` — expect fos4's children (Mode 8ch, Mode 12ch)
    - `qt_models_data(objectId="treeView", parent=[99])` — expect error `kInvalidParentPath` with `failedSegment=0`
    - `qt_models_find(objectId="treeView", value="Aura", match="contains")` — expect at least 1 match with `path=[1, 0]` (under Martin)
    - `qt_models_find(objectId="treeView", value="Fixture 0500", match="exact", parent=[2])` — expect 1 match inside the BulkManufacturer subtree
    - `qt_ui_clickItem(objectId="treeView", itemPath=["Martin","MAC Aura XB","Mode 12ch"], action="select")` — expect `found=true`, `path=[1,0,1]`, ancestors auto-expanded
    - `qt_ui_clickItem(objectId="treeView", path=[0, 0], action="select")` — dual-addressing proof; expect `found=true`
    - Pagination: `qt_models_data(objectId="treeView", parent=[2], offset=1100, limit=100)` — expect 100 rows, `hasMore=false`, first row path `[2, 1100]`
```

Renumber the original "11. Hit test and geometry", "12. Events", and "13. QML inspect" to 12, 13, 14.

- [ ] **Step 3: Update the results table**

In the results table near line 241, replace the single `| Native | models | PASS/FAIL |` row with three rows:

```markdown
| Native | models_flat_read | PASS/FAIL |
| Native | models_tree_read | PASS/FAIL |
| Native | models_find | PASS/FAIL |
| Native | ui_clickItem | PASS/FAIL |
```

And bump the total at line 270 from `**X/39 tests passed**` to `**X/42 tests passed**`.

- [ ] **Step 4: Commit**

```bash
git add .claude/skills/test-mcp-modes/SKILL.md
git commit -m "docs(test-mcp-modes): add tree navigation scenarios; fix stale qt_models_data call"
```

---

### Task 20: Full-suite verification

**Files:** None — this is a verification task.

- [ ] **Step 1: Run the full C++ test suite**

```bash
QT_DIR=$(grep "QTPILOT_QT_DIR:PATH=" build/CMakeCache.txt | cut -d= -f2)
cmd //c "set PATH=${QT_DIR}\\bin;%PATH% && set QT_PLUGIN_PATH=${QT_DIR}\\plugins && ctest --test-dir build -C Release --output-on-failure"
```
Expected: all tests PASS. If any existing test fails, fix it — the design preserves backward compat at the JSON-RPC layer (`parent=[]` = old root behavior).

- [ ] **Step 2: Run the full Python test suite**

```bash
cd python && pytest tests -v
```
Expected: all PASS.

- [ ] **Step 3: Smoke the demo app manually**

```bash
build/bin/Release/qtPilot-launcher.exe build/bin/Release/qtPilot-test-app.exe
```
Switch to the Tree tab; verify ETC, Martin, BulkManufacturer appear; expand a few nodes. Close the app.

- [ ] **Step 4: Run the `test-mcp-modes` skill end-to-end**

Invoke the skill per its own procedure. Expect the new tree scenarios (section 11) to all PASS.

- [ ] **Step 5: Final commit (if any fixups were needed)**

```bash
git status
# commit any stray changes
```

---

## Self-Review Notes

**Spec coverage check:**
- Tool 1 (`qt.models.data` extended) → Tasks 6, 7, 15.
- Tool 2 (`qt.models.find`) → Tasks 8, 9, 16.
- Tool 3 (`qt.ui.clickItem`) → Tasks 10, 11, 12, 13, 14, 17.
- Shared helpers → Tasks 2 (`ensureFetched`), 3 (`pathToIndex`), 4 (`textPathToIndex`), 5 (`indexToRowData`).
- Error codes → Task 1.
- Lazy-model correctness → exercised in Tasks 2, 6, 8 (LazyFlatModel).
- Demo app + skill → Tasks 18, 19.
- Full-suite verification → Task 20.

**Type consistency:** `FindOptions` declared in Task 8 is used in Task 9 by the handler. `pathToIndex` signature with `outFailedSegment` is consistent across Tasks 3, 7, 10. `indexToRowData`'s roles parameter is consistent between Task 5 and its callers in Tasks 6 and 8.

**Open items from spec (now resolved or deferred to task execution):**
- Combo popup behavior → Task 13 (actions collapse to `setCurrentIndex`, popup not shown).
- Double-click helper → confirmed existing in `input_simulator.h:52`; no new helper needed.
- Old Python params — confirmed single caller is the skill doc being rewritten; straight removal in Task 15.
- Synthetic pagination data — procedurally generated in Task 18.
