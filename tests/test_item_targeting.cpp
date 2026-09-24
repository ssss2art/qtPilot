// Copyright (c) 2024-2026 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "api/error_codes.h"
#include "api/native_mode_api.h"
#include "common/qt_matchers.h"
#include "core/object_registry.h"
#include "transport/jsonrpc_handler.h"

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QGraphicsItem>
#include <QGraphicsObject>
#include <QGraphicsScene>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QPainter>
#include <QRegularExpression>
#include <QTimer>
#include <QtTest>

using namespace qtPilot;
using namespace qtPilot::test;

namespace {

/// @brief A filled rectangular item that records what it receives.
class RecordingItem : public QGraphicsObject {
  Q_OBJECT

 public:
  explicit RecordingItem(const QRectF& rect, QGraphicsItem* parent = nullptr)
      : QGraphicsObject(parent), m_rect(rect) {
    setFlag(QGraphicsItem::ItemIsSelectable, true);
  }

  QRectF boundingRect() const override { return m_rect; }
  void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override {
    painter->fillRect(m_rect, Qt::blue);
  }

  int pressCount = 0;
  int contextMenuCount = 0;
  void forget() {
    pressCount = 0;
    contextMenuCount = 0;
  }

 protected:
  void mousePressEvent(QGraphicsSceneMouseEvent* event) override {
    ++pressCount;
    QGraphicsObject::mousePressEvent(event);
  }
  void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override {
    ++contextMenuCount;
    event->accept();
  }

 private:
  QRectF m_rect;
};

/// @brief An item whose interior is click-through: only a border band is in
/// shape(), so the centre of boundingRect() is not on the item at all.
///
/// Real drawing code does this whenever an outline should not swallow clicks
/// meant for what it encloses.
class HollowItem : public RecordingItem {
  Q_OBJECT

 public:
  HollowItem(const QRectF& rect, qreal band, QGraphicsItem* parent = nullptr)
      : RecordingItem(rect, parent), m_rect(rect), m_band(band) {}

  QPainterPath shape() const override {
    QPainterPath outer;
    outer.addRect(m_rect);
    QPainterPath inner;
    inner.addRect(m_rect.adjusted(m_band, m_band, -m_band, -m_band));
    return outer.subtracted(inner);
  }

 private:
  QRectF m_rect;
  qreal m_band;
};

/// @brief Answers a right-click the most common way an application does: a
/// local QMenu run with exec(), acting on whatever exec() returns.
///
/// Nothing is connected to the actions' triggered() signals, so an entry only
/// takes effect if choosing it makes exec() return it.
class ExecMenuHost : public QWidget {
  Q_OBJECT

 public:
  using QWidget::QWidget;
  QString chosen;

 protected:
  void contextMenuEvent(QContextMenuEvent* event) override {
    QMenu menu(this);
    menu.addAction(QStringLiteral("Keep"));
    menu.addAction(QStringLiteral("Delete"));
    if (QAction* picked = menu.exec(event->globalPos())) {
      chosen = picked->text();
    }
  }
};

}  // namespace

/// @brief Addressing an item by id must act on *that* item, plus context menus.
///
/// Both halves come from the same shortcoming: `qt.ui.click` took a point from
/// the item's bounding rect and dispatched a click there, so whatever was
/// topmost at that coordinate received it. Addressing one item and pressing
/// another -- while the call reports ok -- is the worst kind of wrong, because
/// the caller's assertion then measures the wrong object and passes.
class TestItemTargeting : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase();
  void cleanupTestCase();
  void init();

  // --- targeting ---
  void testClickReachesAnUnobstructedItem();
  void testClickReachesAnItemUnderAnOverlay();
  void testClickReportsThePointItUsed();
  void testClickReachesAHollowItemsBorder();
  void testClickRefusesRatherThanPressTheWrongItem();
  void testOcclusionErrorNamesWhatIsInTheWay();
  void testExplicitPositionIsHonouredWhenItLands();
  void testExplicitPositionOnAnObstructedPointIsRefused();

  // --- context menus ---
  void testContextMenuReachesAGraphicsItem();
  void testContextMenuReachesAWidget();
  void testActiveMenuListsItsItems();
  void testActiveMenuReportsEnabledState();
  void testActiveMenuFailsWhenNothingIsOpen();
  void testActivateMenuItemTriggersTheAction();
  void testActivateMenuItemRejectsAnUnknownLabel();
  void testActivateMenuItemDefersByDefault();
  void testActivateMenuItemRunsInlineWhenNotDeferred();
  void testActivateMenuItemReachesAnExecCaller();
  void testDeferredChoiceIsDroppedIfTheMenuWasDismissed();
  void testDeferredChoiceLeavesAMenuAloneIfTheEntryWasDisabled();
  void testActivateMenuItemRefusesAnEntryThatOpensASubmenu();
  void testActivateMenuItemRejectsANonBooleanDeferred();

 private:
  QJsonObject callRaw(const QString& method, const QJsonObject& params);
  QJsonObject callOk(const QString& method, const QJsonObject& params);
  static QJsonObject payloadOf(const QJsonObject& response);
  void pump();

  JsonRpcHandler* m_handler = nullptr;
  NativeModeApi* m_api = nullptr;
  QGraphicsScene* m_scene = nullptr;
  QGraphicsView* m_view = nullptr;
  RecordingItem* m_clear = nullptr;
  RecordingItem* m_partlyCovered = nullptr;
  RecordingItem* m_overlayStrip = nullptr;
  RecordingItem* m_fullyCovered = nullptr;
  RecordingItem* m_lid = nullptr;
  HollowItem* m_hollow = nullptr;
  QWidget* m_menuHost = nullptr;
  QMenu* m_menu = nullptr;
  int m_requestId = 1;
  int m_actionFired = 0;
};

void TestItemTargeting::initTestCase() {
  m_scene = new QGraphicsScene(this);
  m_scene->setSceneRect(0, 0, 800, 400);
  m_view = new QGraphicsView(m_scene);
  m_view->setObjectName(QStringLiteral("targetView"));
  m_view->resize(820, 420);
  // Keep scene and viewport coordinates aligned so the fixtures stay readable.
  m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_view->setFrameStyle(0);

  m_clear = new RecordingItem(QRectF(0, 0, 80, 60));
  m_clear->setObjectName(QStringLiteral("clearItem"));
  m_clear->setPos(20, 20);
  m_scene->addItem(m_clear);

  // Its centre is covered by a strip drawn on top, but its edges are reachable.
  m_partlyCovered = new RecordingItem(QRectF(0, 0, 80, 60));
  m_partlyCovered->setObjectName(QStringLiteral("partlyCoveredItem"));
  m_partlyCovered->setPos(200, 20);
  m_scene->addItem(m_partlyCovered);

  m_overlayStrip = new RecordingItem(QRectF(0, 0, 80, 16));
  m_overlayStrip->setObjectName(QStringLiteral("overlayStrip"));
  m_overlayStrip->setPos(200, 42);
  m_overlayStrip->setZValue(10);
  m_scene->addItem(m_overlayStrip);

  // Nothing on this one is reachable.
  m_fullyCovered = new RecordingItem(QRectF(0, 0, 60, 40));
  m_fullyCovered->setObjectName(QStringLiteral("fullyCoveredItem"));
  m_fullyCovered->setPos(400, 30);
  m_scene->addItem(m_fullyCovered);

  m_lid = new RecordingItem(QRectF(0, 0, 100, 80));
  m_lid->setObjectName(QStringLiteral("lidItem"));
  m_lid->setPos(390, 20);
  m_lid->setZValue(20);
  m_scene->addItem(m_lid);

  m_hollow = new HollowItem(QRectF(0, 0, 160, 120), 10);
  m_hollow->setObjectName(QStringLiteral("hollowItem"));
  m_hollow->setPos(560, 20);
  m_scene->addItem(m_hollow);

  // QGraphicsScene::addItem does not give an item a QObject parent, so the
  // registry cannot walk to it. Parent them to the scene, as a real app's scene
  // items are, and make sure the registry has seen the tree.
  for (QGraphicsObject* item :
       {static_cast<QGraphicsObject*>(m_clear), static_cast<QGraphicsObject*>(m_partlyCovered),
        static_cast<QGraphicsObject*>(m_overlayStrip),
        static_cast<QGraphicsObject*>(m_fullyCovered), static_cast<QGraphicsObject*>(m_lid),
        static_cast<QGraphicsObject*>(m_hollow)}) {
    item->setParent(m_scene);
  }

  m_view->show();
  QVERIFY(QTest::qWaitForWindowExposed(m_view));

  m_menuHost = new QWidget();
  m_menuHost->setObjectName(QStringLiteral("menuHost"));
  m_menuHost->resize(200, 150);
  m_menuHost->show();
  QVERIFY(QTest::qWaitForWindowExposed(m_menuHost));

  m_menu = new QMenu(m_menuHost);
  m_menu->setObjectName(QStringLiteral("hostContextMenu"));
  m_menu->addAction(QStringLiteral("Rename"));
  QAction* del = m_menu->addAction(QStringLiteral("Delete"));
  connect(del, &QAction::triggered, this, [this] { ++m_actionFired; });
  m_menu->addSeparator();
  QAction* unavailable = m_menu->addAction(QStringLiteral("Unavailable"));
  unavailable->setEnabled(false);

  ObjectRegistry::instance()->scanExistingObjects(m_scene);
  ObjectRegistry::instance()->scanExistingObjects(m_view);
  ObjectRegistry::instance()->scanExistingObjects(m_menuHost);

  m_handler = new JsonRpcHandler(this);
  m_api = new NativeModeApi(m_handler, this);
}

void TestItemTargeting::cleanupTestCase() {
  delete m_view;
  m_view = nullptr;
  delete m_menuHost;
  m_menuHost = nullptr;
}

void TestItemTargeting::init() {
  for (RecordingItem* item : {m_clear, m_partlyCovered, m_overlayStrip, m_fullyCovered, m_lid}) {
    if (item) {
      item->forget();
    }
  }
  if (m_hollow) {
    m_hollow->forget();
  }
  m_actionFired = 0;
  if (m_menu && m_menu->isVisible()) {
    m_menu->hide();
    pump();
  }
}

QJsonObject TestItemTargeting::callRaw(const QString& method, const QJsonObject& params) {
  QJsonObject request;
  request[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
  request[QStringLiteral("method")] = method;
  request[QStringLiteral("params")] = params;
  request[QStringLiteral("id")] = m_requestId++;
  const QString requestStr =
      QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact));
  return QJsonDocument::fromJson(m_handler->HandleMessage(requestStr).toUtf8()).object();
}

QJsonObject TestItemTargeting::callOk(const QString& method, const QJsonObject& params) {
  const QJsonObject response = callRaw(method, params);
  QCHECK_THAT(response, IsJsonRpcSuccess());
  pump();
  return response;
}

QJsonObject TestItemTargeting::payloadOf(const QJsonObject& response) {
  return response.value(QStringLiteral("result"))
      .toObject()
      .value(QStringLiteral("result"))
      .toObject();
}

/// Input is queued so the RPC can return before the event is delivered.
void TestItemTargeting::pump() {
  QCoreApplication::processEvents();
  QTest::qWait(20);
  QCoreApplication::processEvents();
}

// ============================================================================
// Targeting
// ============================================================================

void TestItemTargeting::testClickReachesAnUnobstructedItem() {
  QJsonObject params;
  params[QStringLiteral("objectId")] = ObjectRegistry::instance()->objectId(m_clear);
  callOk(QStringLiteral("qt.ui.click"), params);

  QEXPECT_THAT(m_clear->pressCount, Eq(1));
}

void TestItemTargeting::testClickReachesAnItemUnderAnOverlay() {
  // The bounding-rect centre lands on the strip. The click must still reach the
  // item that was addressed, by moving to a point where that item is on top.
  QJsonObject params;
  params[QStringLiteral("objectId")] = ObjectRegistry::instance()->objectId(m_partlyCovered);
  callOk(QStringLiteral("qt.ui.click"), params);

  QEXPECT_THAT(m_partlyCovered->pressCount, Eq(1));
  QEXPECT_THAT(m_overlayStrip->pressCount, Eq(0));
}

void TestItemTargeting::testClickReportsThePointItUsed() {
  QJsonObject params;
  params[QStringLiteral("objectId")] = ObjectRegistry::instance()->objectId(m_partlyCovered);
  const QJsonObject payload = payloadOf(callOk(QStringLiteral("qt.ui.click"), params));

  // A caller reasoning about where the click landed needs the point back, and a
  // flag saying the centre was not usable.
  QEXPECT_THAT(payload, HasJsonField(QStringLiteral("position")));
  QEXPECT_THAT(payload, HasJsonField(QStringLiteral("adjusted"), true));
}

void TestItemTargeting::testClickReachesAHollowItemsBorder() {
  // Nothing is drawn over this one; its own shape() excludes the centre.
  QJsonObject params;
  params[QStringLiteral("objectId")] = ObjectRegistry::instance()->objectId(m_hollow);
  callOk(QStringLiteral("qt.ui.click"), params);

  QEXPECT_THAT(m_hollow->pressCount, Eq(1));
}

void TestItemTargeting::testClickRefusesRatherThanPressTheWrongItem() {
  // Fully covered: there is no honest way to click it, so the call fails rather
  // than press the lid and report success.
  QJsonObject params;
  params[QStringLiteral("objectId")] = ObjectRegistry::instance()->objectId(m_fullyCovered);

  QEXPECT_THAT(callRaw(QStringLiteral("qt.ui.click"), params),
               IsJsonRpcError(static_cast<int>(ErrorCode::kItemOccluded)));
  QEXPECT_THAT(m_fullyCovered->pressCount, Eq(0));
  QEXPECT_THAT(m_lid->pressCount, Eq(0));
}

void TestItemTargeting::testOcclusionErrorNamesWhatIsInTheWay() {
  QJsonObject params;
  params[QStringLiteral("objectId")] = ObjectRegistry::instance()->objectId(m_fullyCovered);
  const QJsonObject data = callRaw(QStringLiteral("qt.ui.click"), params)
                               .value(QStringLiteral("error"))
                               .toObject()
                               .value(QStringLiteral("data"))
                               .toObject();

  // "It didn't work" is not actionable; naming what is on top of it is.
  QEXPECT_THAT(data, HasJsonField(QStringLiteral("occludedBy")));
  QEXPECT_THAT(data.value(QStringLiteral("occludedBy")).toString(), QStrContains("lidItem"));
}

void TestItemTargeting::testExplicitPositionIsHonouredWhenItLands() {
  QJsonObject position;
  position[QStringLiteral("x")] = 5.0;
  position[QStringLiteral("y")] = 5.0;  // on the hollow item's band
  QJsonObject params;
  params[QStringLiteral("objectId")] = ObjectRegistry::instance()->objectId(m_hollow);
  params[QStringLiteral("position")] = position;
  const QJsonObject payload = payloadOf(callOk(QStringLiteral("qt.ui.click"), params));

  QEXPECT_THAT(m_hollow->pressCount, Eq(1));
  // The caller chose the point, so nothing was moved.
  QEXPECT_THAT(payload, HasJsonField(QStringLiteral("adjusted"), false));
}

void TestItemTargeting::testExplicitPositionOnAnObstructedPointIsRefused() {
  // The caller named a point that belongs to the overlay. Quietly sliding off it
  // would be worse than refusing: they asked for that exact point.
  QJsonObject position;
  position[QStringLiteral("x")] = 40.0;
  position[QStringLiteral("y")] = 30.0;
  QJsonObject params;
  params[QStringLiteral("objectId")] = ObjectRegistry::instance()->objectId(m_partlyCovered);
  params[QStringLiteral("position")] = position;

  QEXPECT_THAT(callRaw(QStringLiteral("qt.ui.click"), params),
               IsJsonRpcError(static_cast<int>(ErrorCode::kItemOccluded)));
}

// ============================================================================
// Context menus
//
// A synthesized right-click does not produce the QContextMenuEvent that opens a
// Qt context menu, so driving one needs a method of its own. The event is
// queued rather than sent: a handler that calls QMenu::exec() would otherwise
// block inside the RPC handler, which runs on the GUI thread.
// ============================================================================

void TestItemTargeting::testContextMenuReachesAGraphicsItem() {
  QJsonObject params;
  params[QStringLiteral("objectId")] = ObjectRegistry::instance()->objectId(m_clear);
  callOk(QStringLiteral("qt.ui.contextMenu"), params);

  QEXPECT_THAT(m_clear->contextMenuCount, Eq(1));
}

void TestItemTargeting::testContextMenuReachesAWidget() {
  QJsonObject params;
  params[QStringLiteral("objectId")] = ObjectRegistry::instance()->objectId(m_menuHost);

  QEXPECT_THAT(callRaw(QStringLiteral("qt.ui.contextMenu"), params), IsJsonRpcSuccess());
}

void TestItemTargeting::testActiveMenuListsItsItems() {
  m_menu->popup(QPoint(10, 10));
  pump();

  const QJsonObject payload = payloadOf(callOk(QStringLiteral("qt.ui.activeMenu"), QJsonObject()));
  QEXPECT_THAT(payload, HasJsonField(QStringLiteral("objectId")));

  QStringList labels;
  const QJsonArray items = payload.value(QStringLiteral("items")).toArray();
  for (const auto& entry : items) {
    labels << entry.toObject().value(QStringLiteral("text")).toString();
  }
  QEXPECT_THAT(labels, Contains(QStringLiteral("Rename")));
  QEXPECT_THAT(labels, Contains(QStringLiteral("Delete")));

  m_menu->hide();
}

void TestItemTargeting::testActiveMenuReportsEnabledState() {
  m_menu->popup(QPoint(10, 10));
  pump();

  const QJsonArray items = payloadOf(callOk(QStringLiteral("qt.ui.activeMenu"), QJsonObject()))
                               .value(QStringLiteral("items"))
                               .toArray();
  bool sawDisabled = false;
  for (const auto& entry : items) {
    const QJsonObject item = entry.toObject();
    if (item.value(QStringLiteral("text")).toString() == QStringLiteral("Unavailable")) {
      // A greyed-out entry is a normal assertion target: "Delete is not offered
      // in a read-only state" is exactly the kind of thing a caller checks.
      QEXPECT_THAT(item, HasJsonField(QStringLiteral("enabled"), false));
      sawDisabled = true;
    }
  }
  QVERIFY2(sawDisabled, "the disabled entry was not listed at all");

  m_menu->hide();
}

void TestItemTargeting::testActiveMenuFailsWhenNothingIsOpen() {
  QEXPECT_THAT(callRaw(QStringLiteral("qt.ui.activeMenu"), QJsonObject()),
               IsJsonRpcError(static_cast<int>(ErrorCode::kNoActiveMenu)));
}

void TestItemTargeting::testActivateMenuItemTriggersTheAction() {
  m_menu->popup(QPoint(10, 10));
  pump();

  QJsonObject params;
  params[QStringLiteral("text")] = QStringLiteral("Delete");
  callOk(QStringLiteral("qt.ui.activateMenuItem"), params);

  // Choosing the entry is what clicking it would do; clicking inside a menu's own
  // modal loop is a different and much worse problem.
  QEXPECT_THAT(m_actionFired, Eq(1));
}

void TestItemTargeting::testActivateMenuItemRejectsAnUnknownLabel() {
  m_menu->popup(QPoint(10, 10));
  pump();

  QJsonObject params;
  params[QStringLiteral("text")] = QStringLiteral("No Such Entry");

  QEXPECT_THAT(callRaw(QStringLiteral("qt.ui.activateMenuItem"), params),
               IsJsonRpcError(static_cast<int>(ErrorCode::kMenuItemNotFound)));
  m_menu->hide();
}

// Choosing an entry runs application code: whatever is connected to the action,
// and whatever follows the menu's exec(). If that code opens a modal dialog, its
// event loop re-enters the WebSocket dispatch the request arrived on. So by
// default the choice is queued and the reply goes out first.
void TestItemTargeting::testActivateMenuItemDefersByDefault() {
  m_menu->popup(QPoint(10, 10));
  pump();

  const QJsonObject response =
      callRaw(QStringLiteral("qt.ui.activateMenuItem"), QJsonObject{{"text", "Delete"}});

  QEXPECT_THAT(payloadOf(response),
               AllOf(HasJsonField("deferred", Eq(true)), HasJsonField("text", QStrEq("Delete")),
                     HasJsonField("objectId")));
  QEXPECT_THAT(m_actionFired, Eq(0));

  pump();
  QEXPECT_THAT(m_actionFired, Eq(1));
  QEXPECT_THAT(m_menu->isVisible(), IsFalse());
}

void TestItemTargeting::testActivateMenuItemRunsInlineWhenNotDeferred() {
  m_menu->popup(QPoint(10, 10));
  pump();

  const QJsonObject response = callRaw(QStringLiteral("qt.ui.activateMenuItem"),
                                       QJsonObject{{"text", "Delete"}, {"deferred", false}});

  QEXPECT_THAT(payloadOf(response), HasJsonField("deferred", Eq(false)));
  QEXPECT_THAT(m_actionFired, Eq(1));
}

// The menu's exec() blocks until an entry is chosen, so the entry is chosen from
// inside that loop, as a client's request would be.
void TestItemTargeting::testActivateMenuItemReachesAnExecCaller() {
  ExecMenuHost host;
  host.setObjectName(QStringLiteral("execMenuHost"));
  host.resize(200, 150);
  host.show();
  QVERIFY(QTest::qWaitForWindowExposed(&host));
  ObjectRegistry::instance()->scanExistingObjects(&host);

  // If the choice never lands, exec() never returns and nothing after it runs.
  // Close the menu after a while so a regression fails the comparison below
  // instead of hanging the test binary.
  QTimer::singleShot(3000, this, [] {
    if (QWidget* popup = QApplication::activePopupWidget()) {
      popup->close();
    }
  });

  bool requested = false;
  QTimer chooser;
  chooser.setInterval(10);
  connect(&chooser, &QTimer::timeout, this, [&] {
    if (requested || !QApplication::activePopupWidget()) {
      return;
    }
    requested = true;
    callRaw(QStringLiteral("qt.ui.activateMenuItem"), QJsonObject{{"text", "Delete"}});
  });
  chooser.start();

  callOk(QStringLiteral("qt.ui.contextMenu"),
         QJsonObject{{"objectId", ObjectRegistry::instance()->objectId(&host)}});

  QTRY_COMPARE(host.chosen, QStringLiteral("Delete"));
  QEXPECT_THAT(requested, IsTrue());
}

// A user cannot choose from a menu that is no longer open. A deferred choice
// must not either: the menu may have been dismissed between the reply and the
// queued step, and a menu kept alive for reuse would still fire its action.
void TestItemTargeting::testDeferredChoiceIsDroppedIfTheMenuWasDismissed() {
  m_menu->popup(QPoint(10, 10));
  pump();
  QTest::ignoreMessage(QtWarningMsg, QRegularExpression("menu .*no longer open"));

  callRaw(QStringLiteral("qt.ui.activateMenuItem"), QJsonObject{{"text", "Delete"}});
  m_menu->hide();
  pump();

  QEXPECT_THAT(m_actionFired, Eq(0));
}

// An entry disabled between the reply and the queued step cannot be chosen, and
// trying must not leave the menu highlighting some other entry instead.
void TestItemTargeting::testDeferredChoiceLeavesAMenuAloneIfTheEntryWasDisabled() {
  m_menu->popup(QPoint(10, 10));
  pump();
  QAction* del = m_menu->actions().at(1);
  QTest::ignoreMessage(QtWarningMsg, QRegularExpression("no longer enabled"));

  callRaw(QStringLiteral("qt.ui.activateMenuItem"), QJsonObject{{"text", "Delete"}});
  del->setEnabled(false);
  pump();

  QEXPECT_THAT(m_actionFired, Eq(0));
  QEXPECT_THAT(m_menu->activeAction(), IsNull());
  del->setEnabled(true);
  m_menu->hide();
}

// Choosing an entry that opens a submenu only opens the submenu; reporting that
// as the entry being chosen would be a lie.
void TestItemTargeting::testActivateMenuItemRefusesAnEntryThatOpensASubmenu() {
  QMenu menu(m_menuHost);
  menu.addMenu(QStringLiteral("Export"))->addAction(QStringLiteral("As PDF"));
  menu.popup(QPoint(10, 10));
  pump();

  QEXPECT_THAT(callRaw(QStringLiteral("qt.ui.activateMenuItem"), QJsonObject{{"text", "Export"}}),
               IsJsonRpcError(static_cast<int>(JsonRpcError::kInvalidParams)));
  menu.hide();
}

void TestItemTargeting::testActivateMenuItemRejectsANonBooleanDeferred() {
  m_menu->popup(QPoint(10, 10));
  pump();

  QEXPECT_THAT(callRaw(QStringLiteral("qt.ui.activateMenuItem"),
                       QJsonObject{{"text", "Delete"}, {"deferred", "false"}}),
               IsJsonRpcError(static_cast<int>(JsonRpcError::kInvalidParams)));
  QEXPECT_THAT(m_actionFired, Eq(0));
  m_menu->hide();
}

QTEST_MAIN(TestItemTargeting)
#include "test_item_targeting.moc"
