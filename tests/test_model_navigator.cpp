// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "api/error_codes.h"
#include "api/native_mode_api.h"
#include "core/object_registry.h"
#include "core/object_resolver.h"
#include "transport/jsonrpc_handler.h"

#include <QApplication>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTableView>
#include <QtTest>

using namespace qtPilot;

/// @brief Integration tests for qt.models.* and qt.qml.inspect API methods.
///
/// Tests model discovery, info, data retrieval (with smart pagination),
/// role filtering, view-to-model auto-resolution, QML inspect on non-QML
/// objects, and error handling for all model/QML methods.
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

  // qt.models.info
  void testModelsInfoByModelId();
  void testModelsInfoByViewId();
  void testModelsInfoInvalidId();

  // qt.models.data
  void testModelsDataSmallModel();
  void testModelsDataDisplayRole();
  void testModelsDataWithOffset();
  void testModelsDataSmartPaginationLargeModel();
  void testModelsDataByRoleName();
  void testModelsDataByRoleId();
  void testModelsDataInvalidRole();
  void testModelsDataNotAModel();

  // qt.qml.inspect
  void testQmlInspectNonQmlObject();
  void testQmlInspectInvalidId();

  // Response format
  void testResponseEnvelopeWrapping();
  void testModelsListResponseEnvelope();

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
// qt.models.info Tests
// ========================================================================

void TestModelNavigator::testModelsInfoByModelId() {
  QString modelId = ObjectRegistry::instance()->objectId(m_smallModel);
  QVERIFY(!modelId.isEmpty());

  QJsonValue result = callResult("qt.models.info", QJsonObject{{"objectId", modelId}});
  QVERIFY(result.isObject());

  QJsonObject info = result.toObject();
  QCOMPARE(info["rowCount"].toInt(), 3);
  QCOMPARE(info["columnCount"].toInt(), 2);
  QCOMPARE(info["className"].toString(), QString("QStandardItemModel"));
  QVERIFY(info.contains("roleNames"));
  QVERIFY(info.contains("hasChildren"));
}

void TestModelNavigator::testModelsInfoByViewId() {
  // Use the VIEW's objectId - should auto-resolve to its model
  QString viewId = ObjectRegistry::instance()->objectId(m_tableView);
  QVERIFY(!viewId.isEmpty());

  QJsonValue result = callResult("qt.models.info", QJsonObject{{"objectId", viewId}});
  QVERIFY(result.isObject());

  QJsonObject info = result.toObject();
  // Should return the same model info as calling with model ID directly
  QCOMPARE(info["rowCount"].toInt(), 3);
  QCOMPARE(info["columnCount"].toInt(), 2);
  QCOMPARE(info["className"].toString(), QString("QStandardItemModel"));
}

void TestModelNavigator::testModelsInfoInvalidId() {
  QJsonObject error =
      callExpectError("qt.models.info", QJsonObject{{"objectId", "nonexistent/path/xyz"}});

  QCOMPARE(error["code"].toInt(), ErrorCode::kObjectNotFound);
  QVERIFY(!error["message"].toString().isEmpty());
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
// qt.qml.inspect Tests
// ========================================================================

void TestModelNavigator::testQmlInspectNonQmlObject() {
  // Without QTPILOT_HAS_QML, this will throw kQmlNotAvailable.
  // With QTPILOT_HAS_QML, it returns isQmlItem=false for a QWidget.
  // Either way, the test verifies the method handles non-QML objects.
  QString buttonId = ObjectRegistry::instance()->objectId(m_plainButton);

  QJsonObject response = callRaw("qt.qml.inspect", QJsonObject{{"objectId", buttonId}});

  if (response.contains("result")) {
    // QML support compiled: should get isQmlItem=false
    QJsonObject envelope = response["result"].toObject();
    QJsonObject result = envelope["result"].toObject();
    QCOMPARE(result["isQmlItem"].toBool(), false);
  } else {
    // QML not compiled: kQmlNotAvailable error
    QJsonObject error = response["error"].toObject();
    QCOMPARE(error["code"].toInt(), ErrorCode::kQmlNotAvailable);
  }
}

void TestModelNavigator::testQmlInspectInvalidId() {
  QJsonObject error =
      callExpectError("qt.qml.inspect", QJsonObject{{"objectId", "nonexistent/path/xyz"}});

  QCOMPARE(error["code"].toInt(), ErrorCode::kObjectNotFound);
  QVERIFY(!error["message"].toString().isEmpty());
}

// ========================================================================
// Response Envelope Tests
// ========================================================================

void TestModelNavigator::testResponseEnvelopeWrapping() {
  // qt.models.info should return envelope with result + meta
  QString modelId = ObjectRegistry::instance()->objectId(m_smallModel);

  QJsonObject envelope = callEnvelope("qt.models.info", QJsonObject{{"objectId", modelId}});

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

QTEST_MAIN(TestModelNavigator)
#include "test_model_navigator.moc"
