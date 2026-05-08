// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "api/error_codes.h"
#include "api/native_mode_api.h"
#include "core/object_registry.h"
#include "core/object_resolver.h"
#include "transport/jsonrpc_handler.h"
#include "introspection/model_navigator.h"

#include <QAbstractListModel>
#include <QApplication>
#include <QComboBox>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTableView>
#include <QTreeView>
#include <QtTest>

using namespace qtPilot;

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

/// @brief Integration tests for qt.models.* API methods.
///
/// Tests model discovery, data retrieval (with smart pagination), role
/// filtering, view-to-model auto-resolution, and error handling for all
/// model methods.
class TestModelNavigator : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase();
  void cleanupTestCase();
  void init();
  void cleanup();

  // qt.models.list
  void testModelsListFindsTestModel();
  void testModelsListIncludesRoleNames();

  // qt.models.data
  void testModelsDataSmallModel();
  void testModelsDataDisplayRole();
  void testModelsDataWithOffset();
  void testModelsDataSmartPaginationLargeModel();
  void testModelsDataByRoleName();
  void testModelsDataByRoleId();
  void testModelsDataInvalidRole();
  void testModelsDataNotAModel();

  // Tree/path helpers
  void testEnsureFetchedCallsFetchMoreOnLazyModel();
  void testPathToIndexRoot();
  void testPathToIndexTwoLevel();
  void testPathToIndexInvalidReturnsFailedSegment();
  void testPathToIndexCallsEnsureFetched();
  void testTextPathToIndexTwoLevel();
  void testTextPathToIndexMissingSegment();
  void testTextPathToIndexWithColumn();

  // Response format
  void testResponseEnvelopeWrapping();
  void testModelsListResponseEnvelope();

  // indexToRowData helper
  void testIndexToRowDataProducesPathAndCells();

  // getModelData with parentPath
  void testGetModelDataWithTwoLevelParentPath();
  void testGetModelDataRowsHavePathField();
  void testGetModelDataInvalidParentPathReturnsEmpty();

  // qt.models.data JSON-RPC handler
  void testApiModelsDataWithParentArray();
  void testApiModelsDataInvalidParentPathError();
  void testApiModelsDataEchoesParent();

  // findRecursive walker
  void testFindRecursiveExactMatch();
  void testFindRecursiveContainsMultipleLevels();
  void testFindRecursiveRespectsMaxHits();
  void testFindRecursiveScopesToParent();
  void testFindRecursiveFindsLazyChildren();
  void testFindRecursiveRegex();
  void testFindRecursiveInvalidRegex();

  // qt.models.search JSON-RPC handler
  void testApiModelsFindExact();
  void testApiModelsFindContainsReturnsPathAndCells();
  void testApiModelsFindScopesToParent();
  void testApiModelsFindTruncated();
  void testApiModelsFindInvalidRegexError();
  void testApiModelsFindRoleNotFoundError();

  // qt.ui.clickItem JSON-RPC handler
  void testApiUiClickItemPathSelect();
  void testApiUiClickItemMissingPathAndItemPath();
  void testApiUiClickItemBothPathAndItemPathError();
  void testApiUiClickItemInvalidPathError();
  void testApiUiClickItemTextPathSelect();
  void testApiUiClickItemTextPathMissingSegment();
  void testApiUiClickItemClickAction();
  void testApiUiClickItemDoubleClickAction();
  void testApiUiClickItemEditOpensEditor();
  void testApiUiClickItemEditInvalidColumnError();
  void testApiUiClickItemEditNonEditableError();
  void testApiUiClickItemComboBoxSelectsValue();
  void testApiUiClickItemComboBoxEditReturnsNotEditable();
  void testApiUiClickItemComboBoxRejectsNonZeroColumn();
  void testApiUiClickItemExpandsAncestors();

 private:
  /// @brief Make a JSON-RPC call and return the full parsed response object.
  QJsonObject callRaw(const QString& method, const QJsonObject& params);

  /// @brief Make a JSON-RPC call and return the envelope result.
  QJsonObject callEnvelope(const QString& method, const QJsonObject& params);

  /// @brief Make a JSON-RPC call and return the inner result value.
  QJsonValue callResult(const QString& method, const QJsonObject& params);

  /// @brief Make a JSON-RPC call expecting an error, return the error object.
  QJsonObject callExpectError(const QString& method, const QJsonObject& params);

  JsonRpcHandler* m_handler = nullptr;
  NativeModeApi* m_api = nullptr;

  // Small model: 3 rows x 2 columns
  QStandardItemModel* m_smallModel = nullptr;
  QTableView* m_tableView = nullptr;

  // Large model: 150 rows x 1 column (for pagination tests)
  QStandardItemModel* m_largeModel = nullptr;

  // Plain QObject for error tests
  QPushButton* m_plainButton = nullptr;

  int m_requestId = 1;
};

void TestModelNavigator::initTestCase() {
  installObjectHooks();
}

void TestModelNavigator::cleanupTestCase() {
  uninstallObjectHooks();
}

void TestModelNavigator::init() {
  m_handler = new JsonRpcHandler(this);
  m_api = new NativeModeApi(m_handler, this);

  // Create small model: 3 rows x 2 columns with known data
  m_smallModel = new QStandardItemModel(3, 2, this);
  m_smallModel->setObjectName("testModel");
  m_smallModel->setHorizontalHeaderLabels({"Col1", "Col2"});
  m_smallModel->setItem(0, 0, new QStandardItem("A1"));
  m_smallModel->setItem(0, 1, new QStandardItem("A2"));
  m_smallModel->setItem(1, 0, new QStandardItem("B1"));
  m_smallModel->setItem(1, 1, new QStandardItem("B2"));
  m_smallModel->setItem(2, 0, new QStandardItem("C1"));
  m_smallModel->setItem(2, 1, new QStandardItem("C2"));

  // Create QTableView with the small model
  m_tableView = new QTableView();
  m_tableView->setObjectName("testView");
  m_tableView->setModel(m_smallModel);

  // Create large model: 150 rows x 1 column
  m_largeModel = new QStandardItemModel(150, 1, this);
  m_largeModel->setObjectName("largeModel");
  for (int i = 0; i < 150; ++i) {
    m_largeModel->setItem(i, 0, new QStandardItem(QString("Row%1").arg(i)));
  }

  // Plain button (not a model, not a view)
  m_plainButton = new QPushButton("Plain");
  m_plainButton->setObjectName("plainButton");

  // Show widgets and process events
  m_tableView->show();
  m_plainButton->show();
  QApplication::processEvents();

  // Register all objects with ObjectRegistry
  ObjectRegistry::instance()->scanExistingObjects(m_smallModel);
  ObjectRegistry::instance()->scanExistingObjects(m_largeModel);
  ObjectRegistry::instance()->scanExistingObjects(m_tableView);
  ObjectRegistry::instance()->scanExistingObjects(m_plainButton);
}

void TestModelNavigator::cleanup() {
  ObjectResolver::clearNumericIds();

  delete m_tableView;
  m_tableView = nullptr;
  delete m_plainButton;
  m_plainButton = nullptr;

  // Models owned by 'this' via parent, but clear pointers
  delete m_smallModel;
  m_smallModel = nullptr;
  delete m_largeModel;
  m_largeModel = nullptr;

  delete m_api;
  m_api = nullptr;
  delete m_handler;
  m_handler = nullptr;
}

QJsonObject TestModelNavigator::callRaw(const QString& method, const QJsonObject& params) {
  QJsonObject request;
  request["jsonrpc"] = "2.0";
  request["method"] = method;
  request["params"] = params;
  request["id"] = m_requestId++;

  QString requestStr = QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact));
  QString responseStr = m_handler->HandleMessage(requestStr);

  return QJsonDocument::fromJson(responseStr.toUtf8()).object();
}

QJsonObject TestModelNavigator::callEnvelope(const QString& method, const QJsonObject& params) {
  QJsonObject response = callRaw(method, params);
  QJsonValue resultVal = response["result"];
  if (resultVal.isObject()) {
    return resultVal.toObject();
  }
  return QJsonObject();
}

QJsonValue TestModelNavigator::callResult(const QString& method, const QJsonObject& params) {
  QJsonObject envelope = callEnvelope(method, params);
  return envelope["result"];
}

QJsonObject TestModelNavigator::callExpectError(const QString& method, const QJsonObject& params) {
  QJsonObject response = callRaw(method, params);
  return response["error"].toObject();
}

// ========================================================================
// qt.models.list Tests
// ========================================================================

void TestModelNavigator::testModelsListFindsTestModel() {
  QJsonValue result = callResult("qt.models.list", QJsonObject());
  QVERIFY(result.isArray());

  QJsonArray models = result.toArray();
  QVERIFY(models.size() >= 2);  // At least testModel and largeModel

  // Find testModel in the list
  bool foundTestModel = false;
  for (const QJsonValue& v : models) {
    QJsonObject model = v.toObject();
    if (model["className"].toString() == "QStandardItemModel") {
      int rowCount = model["rowCount"].toInt();
      int colCount = model["columnCount"].toInt();
      if (rowCount == 3 && colCount == 2) {
        foundTestModel = true;
        QVERIFY(!model["objectId"].toString().isEmpty());
        break;
      }
    }
  }
  QVERIFY2(foundTestModel, "testModel (3x2 QStandardItemModel) not found in qt.models.list");
}

void TestModelNavigator::testModelsListIncludesRoleNames() {
  QJsonValue result = callResult("qt.models.list", QJsonObject());
  QJsonArray models = result.toArray();

  // Every model entry should have roleNames
  for (const QJsonValue& v : models) {
    QJsonObject model = v.toObject();
    QVERIFY2(model.contains("roleNames"),
             qPrintable(QString("Model %1 missing roleNames").arg(model["className"].toString())));
    QVERIFY(model["roleNames"].isObject());
  }
}

// ========================================================================
// qt.models.data Tests
// ========================================================================

void TestModelNavigator::testModelsDataSmallModel() {
  QString modelId = ObjectRegistry::instance()->objectId(m_smallModel);

  // Call without offset/limit - small model should return all rows
  QJsonValue result = callResult("qt.models.data", QJsonObject{{"objectId", modelId}});
  QVERIFY(result.isObject());

  QJsonObject data = result.toObject();
  QCOMPARE(data["totalRows"].toInt(), 3);
  QCOMPARE(data["totalColumns"].toInt(), 2);
  QCOMPARE(data["hasMore"].toBool(), false);

  QJsonArray rows = data["rows"].toArray();
  QCOMPARE(rows.size(), 3);
}

void TestModelNavigator::testModelsDataDisplayRole() {
  QString modelId = ObjectRegistry::instance()->objectId(m_smallModel);

  QJsonValue result = callResult("qt.models.data", QJsonObject{{"objectId", modelId}});
  QJsonObject data = result.toObject();
  QJsonArray rows = data["rows"].toArray();

  // Row 0: cells[0] should have display="A1", cells[1] should have display="A2"
  QJsonArray row0Cells = rows[0].toObject()["cells"].toArray();
  QCOMPARE(row0Cells[0].toObject()["display"].toString(), QString("A1"));
  QCOMPARE(row0Cells[1].toObject()["display"].toString(), QString("A2"));

  // Row 1: "B1", "B2"
  QJsonArray row1Cells = rows[1].toObject()["cells"].toArray();
  QCOMPARE(row1Cells[0].toObject()["display"].toString(), QString("B1"));
  QCOMPARE(row1Cells[1].toObject()["display"].toString(), QString("B2"));

  // Row 2: "C1", "C2"
  QJsonArray row2Cells = rows[2].toObject()["cells"].toArray();
  QCOMPARE(row2Cells[0].toObject()["display"].toString(), QString("C1"));
  QCOMPARE(row2Cells[1].toObject()["display"].toString(), QString("C2"));
}

void TestModelNavigator::testModelsDataWithOffset() {
  QString modelId = ObjectRegistry::instance()->objectId(m_smallModel);

  QJsonValue result =
      callResult("qt.models.data", QJsonObject{{"objectId", modelId}, {"offset", 1}, {"limit", 1}});
  QJsonObject data = result.toObject();

  QCOMPARE(data["totalRows"].toInt(), 3);
  QCOMPARE(data["offset"].toInt(), 1);
  QCOMPARE(data["limit"].toInt(), 1);
  QCOMPARE(data["hasMore"].toBool(), true);

  QJsonArray rows = data["rows"].toArray();
  QCOMPARE(rows.size(), 1);

  // Should be row 1: "B1", "B2"
  QJsonArray cells = rows[0].toObject()["cells"].toArray();
  QCOMPARE(cells[0].toObject()["display"].toString(), QString("B1"));
}

void TestModelNavigator::testModelsDataSmartPaginationLargeModel() {
  QString modelId = ObjectRegistry::instance()->objectId(m_largeModel);

  // Call without limit - large model (150 rows) should auto-paginate to 100
  QJsonValue result = callResult("qt.models.data", QJsonObject{{"objectId", modelId}});
  QJsonObject data = result.toObject();

  QCOMPARE(data["totalRows"].toInt(), 150);
  QCOMPARE(data["limit"].toInt(), 100);
  QCOMPARE(data["hasMore"].toBool(), true);
  QCOMPARE(data["offset"].toInt(), 0);

  QJsonArray rows = data["rows"].toArray();
  QCOMPARE(rows.size(), 100);  // Smart pagination caps at 100
}

void TestModelNavigator::testModelsDataByRoleName() {
  QString modelId = ObjectRegistry::instance()->objectId(m_smallModel);

  QJsonValue result = callResult(
      "qt.models.data", QJsonObject{{"objectId", modelId}, {"roles", QJsonArray{"display"}}});
  QJsonObject data = result.toObject();

  QJsonArray rows = data["rows"].toArray();
  QVERIFY(rows.size() > 0);

  // Verify display data is present
  QJsonArray row0Cells = rows[0].toObject()["cells"].toArray();
  QVERIFY(row0Cells[0].toObject().contains("display"));
  QCOMPARE(row0Cells[0].toObject()["display"].toString(), QString("A1"));
}

void TestModelNavigator::testModelsDataByRoleId() {
  QString modelId = ObjectRegistry::instance()->objectId(m_smallModel);

  // Qt::DisplayRole == 0
  QJsonValue result =
      callResult("qt.models.data", QJsonObject{{"objectId", modelId}, {"roles", QJsonArray{0}}});
  QJsonObject data = result.toObject();

  QJsonArray rows = data["rows"].toArray();
  QVERIFY(rows.size() > 0);

  // DisplayRole (0) should map to "display" key name
  QJsonArray row0Cells = rows[0].toObject()["cells"].toArray();
  QVERIFY(row0Cells[0].toObject().contains("display"));
  QCOMPARE(row0Cells[0].toObject()["display"].toString(), QString("A1"));
}

void TestModelNavigator::testModelsDataInvalidRole() {
  QString modelId = ObjectRegistry::instance()->objectId(m_smallModel);

  QJsonObject error = callExpectError(
      "qt.models.data", QJsonObject{{"objectId", modelId}, {"roles", QJsonArray{"nonexistent"}}});

  QCOMPARE(error["code"].toInt(), ErrorCode::kModelRoleNotFound);
  QVERIFY(error["message"].toString().contains("nonexistent"));

  // Should include available roles in error data
  QJsonObject data = error["data"].toObject();
  QVERIFY(data.contains("availableRoles"));
  QVERIFY(data.contains("roleName"));
}

void TestModelNavigator::testModelsDataNotAModel() {
  // Use a plain QPushButton objectId - not a model or view
  QString buttonId = ObjectRegistry::instance()->objectId(m_plainButton);

  QJsonObject error = callExpectError("qt.models.data", QJsonObject{{"objectId", buttonId}});

  QCOMPARE(error["code"].toInt(), ErrorCode::kNotAModel);
  QVERIFY(!error["message"].toString().isEmpty());

  QJsonObject data = error["data"].toObject();
  QVERIFY(data.contains("hint"));
}

// ========================================================================
// Response Envelope Tests
// ========================================================================

void TestModelNavigator::testResponseEnvelopeWrapping() {
  // qt.models.data should return envelope with result + meta
  QString modelId = ObjectRegistry::instance()->objectId(m_smallModel);

  QJsonObject envelope = callEnvelope("qt.models.data", QJsonObject{{"objectId", modelId}});

  QVERIFY(envelope.contains("result"));
  QVERIFY(envelope.contains("meta"));

  QJsonObject meta = envelope["meta"].toObject();
  QVERIFY(meta.contains("timestamp"));
  qint64 ts = static_cast<qint64>(meta["timestamp"].toDouble());
  QVERIFY(ts > 0);
}

void TestModelNavigator::testModelsListResponseEnvelope() {
  // qt.models.list should also have envelope
  QJsonObject envelope = callEnvelope("qt.models.list", QJsonObject());

  QVERIFY(envelope.contains("result"));
  QVERIFY(envelope.contains("meta"));
  QVERIFY(envelope["result"].isArray());
}

// ========================================================================
// Tree/Path Helper Tests
// ========================================================================

void TestModelNavigator::testEnsureFetchedCallsFetchMoreOnLazyModel() {
  LazyFlatModel lazy(42, this);
  QCOMPARE(lazy.fetchedRows(), 0);

  ModelNavigator::ensureFetched(&lazy, QModelIndex());

  QCOMPARE(lazy.fetchedRows(), 42);
  QCOMPARE(lazy.rowCount(), 42);
}

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

// ========================================================================
// indexToRowData Helper Tests
// ========================================================================

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

// ========================================================================
// getModelData with parentPath Tests
// ========================================================================

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

// ========================================================================
// qt.models.data JSON-RPC handler Tests
// ========================================================================

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

// ========================================================================
// findRecursive Walker Tests
// ========================================================================

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

  QString err;
  QVERIFY(ModelNavigator::compileFindOptions(opts, &err));
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

  QString err;
  bool ok = ModelNavigator::compileFindOptions(opts, &err);
  QCOMPARE(ok, false);
  QVERIFY(!err.isEmpty());

  delete tree;
}

// ========================================================================
// qt.models.search JSON-RPC handler Tests
// ========================================================================

void TestModelNavigator::testApiModelsFindExact() {
  QString modelId = ObjectRegistry::instance()->objectId(m_smallModel);
  QJsonValue result = callResult("qt.models.search",
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
  QJsonValue result = callResult("qt.models.search",
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

  QJsonValue result = callResult("qt.models.search",
                                 QJsonObject{{"objectId", id},
                                             {"value", "target"},
                                             {"match", "exact"},
                                             {"parent", QJsonArray{0}}});
  QCOMPARE(result.toObject()["count"].toInt(), 1);
}

void TestModelNavigator::testApiModelsFindTruncated() {
  QString modelId = ObjectRegistry::instance()->objectId(m_largeModel);
  QJsonValue result = callResult("qt.models.search",
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
  QJsonObject error = callExpectError("qt.models.search",
                                      QJsonObject{{"objectId", modelId},
                                                  {"value", "[unclosed"},
                                                  {"match", "regex"}});
  QCOMPARE(error["code"].toInt(), ErrorCode::kInvalidRegex);
  QVERIFY(error["data"].toObject().contains("pattern"));
  QVERIFY(error["data"].toObject().contains("error"));
}

void TestModelNavigator::testApiModelsFindRoleNotFoundError() {
  QString modelId = ObjectRegistry::instance()->objectId(m_smallModel);
  QJsonObject error = callExpectError("qt.models.search",
                                      QJsonObject{{"objectId", modelId},
                                                  {"value", "x"},
                                                  {"role", "nonexistent"}});
  QCOMPARE(error["code"].toInt(), ErrorCode::kModelRoleNotFound);
}

// ========================================================================
// qt.ui.clickItem JSON-RPC handler Tests
// ========================================================================

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
  // Edit succeeded without throwing, and the current index moved to the
  // edit cell. (state() is protected so we can't query EditingState directly;
  // the non-throw path + currentIndex shift is the observable contract.)
  QCOMPARE(m_tableView->currentIndex().row(), 0);
  QCOMPARE(m_tableView->currentIndex().column(), 1);
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

QTEST_MAIN(TestModelNavigator)
#include "test_model_navigator.moc"
