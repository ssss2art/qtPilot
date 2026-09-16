// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

// Addressing QGraphicsView scene items.
//
// Everything in a QGraphicsView plan is a QGraphicsObject: a QObject, so
// discoverable and readable, but historically not *addressable* -- qt.ui.geometry
// threw kObjectNotWidget and qt.ui.hitTest never descended past the viewport.
// A caller wanting to click one had to reverse-engineer the view transform by
// dragging and dividing. These tests pin the two entry points that make an item
// addressable by identity instead.

#include "api/error_codes.h"
#include "api/native_mode_api.h"
#include "common/qt_matchers.h"
#include "core/object_registry.h"
#include "core/object_resolver.h"
#include "interaction/hit_test.h"
#include "transport/jsonrpc_handler.h"

#include <stdexcept>

#include <QApplication>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPushButton>
#include <QScrollBar>
#include <QtTest>

using namespace qtPilot;
using namespace qtPilot::test;

namespace {

/// @brief A QGraphicsObject standing in for a scene item in a plan- or
/// diagram-style app: a QObject with a body of its own plus a child that extends
/// past it, so boundingRect() and childrenBoundingRect() differ.
class TestSceneItem : public QGraphicsObject {
  Q_OBJECT
 public:
  explicit TestSceneItem(QGraphicsItem* parent = nullptr) : QGraphicsObject(parent) {}

  QRectF boundingRect() const override { return QRectF(0, 0, 40, 20); }
  void paint(QPainter*, const QStyleOptionGraphicsItem*, QWidget*) override {}
};

}  // namespace

class TestGraphicsView : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase();
  void cleanupTestCase();
  void init();
  void cleanup();

  // HitTest::graphicsItemGeometry
  void geometryReportsItemLocalAndSceneRects();
  void geometryMapsThroughViewTransformAndScroll();
  void geometryRoundTripsToTheItemUnderThatPoint();
  void geometryListsEveryViewOnTheScene();
  void geometryMirrorsRequestedViewAtTopLevel();
  void uiGeometryRejectsAViewOnAnotherScene();
  void geometryReportsScrolledOutItemAsNotVisible();
  void geometryReportsHiddenItemAsNotVisiblePerView();
  void geometryWithoutAViewReportsNullGlobal();
  void geometryUsesBoundingRectNotChildren();
  void geometrySurvivesARotatedViewTransform();
  void geometryFollowsTheItemsOwnTransform();
  void geometryComposesNestedItemPositions();
  void geometryReportsHiddenViewAsNotVisible();
  void geometryRejectsANullItem();

  // HitTest::graphicsItemIdAt
  void hitTestDescendsIntoTheScene();
  void hitTestReturnsNearestGraphicsObjectAncestor();
  void hitTestMissesOnEmptyCanvas();
  void hitTestMissesItemWithNoGraphicsObjectAncestor();
  void hitTestReturnsTopmostOfOverlappingItems();
  void hitTestRejectsANullView();

  // HitTest::widgetIdAt -- global-coordinate descent into a scene
  void widgetIdAtDescendsIntoTheSceneFromAGlobalPoint();
  void widgetIdAtFallsBackToTheViewOnEmptyCanvas();

  // qt.ui.* wiring
  void uiGeometryAcceptsAGraphicsObject();
  void uiGeometryStillWorksForWidgets();
  void uiHitTestScopedToAViewFindsTheItem();
  void uiGeometryHonoursViewObjectId();
  void uiGeometryRejectsAnUnknownViewObjectId();
  void uiGeometryRejectsAViewObjectIdThatIsNotAView();
  void uiHitTestScopedReportsAMissAsAnError();
  void inspectGeometryPartCoversGraphicsObjects();
  void inspectGeometryPartNamesTheCoordinateSpace();

 private:
  QJsonObject call(const QString& method, const QJsonObject& params);
  QJsonValue callResult(const QString& method, const QJsonObject& params);
  QJsonObject callExpectError(const QString& method, const QJsonObject& params);

  /// @brief Centre of @a rect ("x"/"y"/"width"/"height", doubles), rounded.
  static QPoint centreOf(const QJsonObject& rect);

  JsonRpcHandler* m_handler = nullptr;
  NativeModeApi* m_api = nullptr;
  QGraphicsScene* m_scene = nullptr;
  QGraphicsView* m_view = nullptr;
  TestSceneItem* m_item = nullptr;
  int m_requestId = 1;
};

void TestGraphicsView::initTestCase() {
  installObjectHooks();
}

void TestGraphicsView::cleanupTestCase() {
  uninstallObjectHooks();
}

void TestGraphicsView::init() {
  m_handler = new JsonRpcHandler(this);
  m_api = new NativeModeApi(m_handler, this);

  m_scene = new QGraphicsScene();
  m_scene->setObjectName(QStringLiteral("testScene"));
  m_scene->setSceneRect(0, 0, 1000, 1000);

  m_item = new TestSceneItem();
  m_item->setObjectName(QStringLiteral("sceneItem"));
  m_item->setPos(300, 300);
  m_scene->addItem(m_item);

  m_view = new QGraphicsView(m_scene);
  m_view->setObjectName(QStringLiteral("planView"));
  // A non-identity transform and a scrolled viewport are the two things the
  // hand-rolled workaround had to guess. Bake both in so any implementation
  // that ignores either one fails here rather than in the field.
  m_view->setTransform(QTransform::fromScale(1.25, 1.25));
  m_view->resize(400, 300);
  m_view->show();
  QApplication::processEvents();
  // Scrolled far enough to matter, but leaving the item inside the viewport:
  // at 1.25x the item sits at viewport (175, 175), well within ~380x280.
  m_view->horizontalScrollBar()->setValue(200);
  m_view->verticalScrollBar()->setValue(200);
  QApplication::processEvents();

  ObjectRegistry::instance()->scanExistingObjects(m_view);
  ObjectRegistry::instance()->scanExistingObjects(m_scene);
}

void TestGraphicsView::cleanup() {
  ObjectResolver::clearNumericIds();

  delete m_view;
  m_view = nullptr;
  delete m_scene;  // owns m_item
  m_scene = nullptr;
  m_item = nullptr;

  delete m_api;
  m_api = nullptr;
  delete m_handler;
  m_handler = nullptr;
}

QJsonObject TestGraphicsView::call(const QString& method, const QJsonObject& params) {
  QJsonObject request;
  request[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
  request[QStringLiteral("method")] = method;
  request[QStringLiteral("params")] = params;
  request[QStringLiteral("id")] = m_requestId++;

  const QString response = m_handler->HandleMessage(
      QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact)));
  return QJsonDocument::fromJson(response.toUtf8()).object();
}

QJsonValue TestGraphicsView::callResult(const QString& method, const QJsonObject& params) {
  const QJsonObject response = call(method, params);
  if (response.contains(QStringLiteral("error"))) {
    const QString message =
        response[QStringLiteral("error")].toObject()[QStringLiteral("message")].toString();
    QTest::qFail(qPrintable(QStringLiteral("%1 failed: %2").arg(method, message)), __FILE__,
                 __LINE__);
    return QJsonValue();
  }
  const QJsonValue envelope = response[QStringLiteral("result")];
  return envelope.toObject()[QStringLiteral("result")];
}

QJsonObject TestGraphicsView::callExpectError(const QString& method, const QJsonObject& params) {
  return call(method, params)[QStringLiteral("error")].toObject();
}

QPoint TestGraphicsView::centreOf(const QJsonObject& rect) {
  return QPoint(
      qRound(rect[QStringLiteral("x")].toDouble() + rect[QStringLiteral("width")].toDouble() / 2.0),
      qRound(rect[QStringLiteral("y")].toDouble() +
             rect[QStringLiteral("height")].toDouble() / 2.0));
}

// === HitTest::graphicsItemGeometry ==========================================

void TestGraphicsView::geometryReportsItemLocalAndSceneRects() {
  const QJsonObject geo = HitTest::graphicsItemGeometry(m_item);

  // Local is the item's own boundingRect, in item coordinates.
  QEXPECT_THAT(geo[QStringLiteral("local")],
               AllOf(JsonField("x", 0.0), JsonField("y", 0.0), JsonField("width", 40.0),
                     JsonField("height", 20.0)));

  // Scene is that rect mapped through the item's own transform/position -- the
  // number `pos` already told callers, but as a rect.
  QEXPECT_THAT(geo[QStringLiteral("scene")],
               AllOf(JsonField("x", 300.0), JsonField("y", 300.0), JsonField("width", 40.0),
                     JsonField("height", 20.0)));

  QEXPECT_THAT(geo, AllOf(JsonField("visible", true), HasJsonField("devicePixelRatio")));
}

void TestGraphicsView::geometryMapsThroughViewTransformAndScroll() {
  const QJsonObject geo = HitTest::graphicsItemGeometry(m_item);
  const QJsonObject global = geo[QStringLiteral("global")].toObject();

  // The whole point of the fix: the reported rect must already carry the view's
  // scale and scroll offset, so no caller ever has to calibrate them again.
  // Worked by hand rather than by re-running the mapping under test:
  // scene 300 * 1.25 == 375, minus the 200 scrolled away, is viewport 175.
  QEXPECT_THAT(m_view->horizontalScrollBar()->value(), Eq(200));
  QEXPECT_THAT(m_view->verticalScrollBar()->value(), Eq(200));
  // 40 scene units at 1.25x is 50 pixels, not 40 -- and exactly 50: mapping a
  // rect through integer polygon corners would report 51.
  QEXPECT_THAT(geo[QStringLiteral("viewport")],
               AllOf(JsonField("x", 175.0), JsonField("y", 175.0), JsonField("width", 50.0),
                     JsonField("height", 25.0)));

  // Global is the same rect, offset by the viewport's origin on screen.
  const QPoint origin = m_view->viewport()->mapToGlobal(QPoint(0, 0));
  QEXPECT_THAT(global, AllOf(JsonField("x", 175.0 + origin.x()), JsonField("y", 175.0 + origin.y()),
                             JsonField("width", 50.0), JsonField("height", 25.0)));
}

void TestGraphicsView::geometryRoundTripsToTheItemUnderThatPoint() {
  // Acceptance, stated as a property: the centre of the reported global rect,
  // handed straight back as a screen coordinate, lands on this item.
  const QJsonObject geo = HitTest::graphicsItemGeometry(m_item);
  const QPoint globalCentre = centreOf(geo[QStringLiteral("global")].toObject());

  const QPoint viewportPos = m_view->viewport()->mapFromGlobal(globalCentre);
  QEXPECT_THAT(m_view->itemAt(viewportPos), Eq(static_cast<QGraphicsItem*>(m_item)));
}

void TestGraphicsView::geometryListsEveryViewOnTheScene() {
  // A plan editor commonly renders one scene into two views at once -- an
  // overview and a detail pane.
  // Returning "the first view" would be a coin flip, so every view is reported.
  QGraphicsView second(m_scene);
  second.setObjectName(QStringLiteral("deviceLayoutView"));
  second.setTransform(QTransform::fromScale(0.5, 0.5));
  second.resize(200, 200);
  second.show();
  QApplication::processEvents();
  ObjectRegistry::instance()->scanExistingObjects(&second);

  const QJsonObject geo = HitTest::graphicsItemGeometry(m_item);
  const QJsonArray views = geo[QStringLiteral("views")].toArray();
  QEXPECT_THAT(views, JsonArraySize(Eq(2)));
  QEXPECT_THAT(views, Each(AllOf(HasJsonField("viewObjectId"), HasJsonField("global"),
                                 HasJsonField("viewport"), HasJsonField("visible"))));

  QEXPECT_THAT(views, JsonArrayContains(JsonField(
                          "viewObjectId", QStrEq(ObjectRegistry::instance()->objectId(m_view)))));
  QEXPECT_THAT(views, JsonArrayContains(JsonField(
                          "viewObjectId", QStrEq(ObjectRegistry::instance()->objectId(&second)))));

  // The two views disagree about size; that is exactly why the caller must be
  // able to pick one rather than be handed an arbitrary winner.
  QEXPECT_THAT(
      views[0].toObject()[QStringLiteral("global")].toObject()[QStringLiteral("width")].toDouble(),
      Ne(views[1]
             .toObject()[QStringLiteral("global")]
             .toObject()[QStringLiteral("width")]
             .toDouble()));
}

void TestGraphicsView::geometryMirrorsRequestedViewAtTopLevel() {
  QGraphicsView second(m_scene);
  second.setObjectName(QStringLiteral("deviceLayoutView"));
  second.setTransform(QTransform::fromScale(0.5, 0.5));
  second.resize(200, 200);
  second.show();
  QApplication::processEvents();

  const QJsonObject geo = HitTest::graphicsItemGeometry(m_item, &second);
  // 40 scene units at 0.5x -- the second view's scale, not the first view's.
  QEXPECT_THAT(geo, AllOf(JsonField("global", JsonField("width", 20.0)),
                          JsonField("viewport", JsonField("width", 20.0))));
}

void TestGraphicsView::geometryReportsScrolledOutItemAsNotVisible() {
  // Off-screen is not an error: a caller may legitimately want to scroll to it.
  m_item->setPos(5000, 5000);
  QApplication::processEvents();

  const QJsonObject geo = HitTest::graphicsItemGeometry(m_item);
  QEXPECT_THAT(geo, JsonField("visible", false));
  // Still reported, just off-viewport.
  QEXPECT_THAT(geo[QStringLiteral("global")].isObject(), IsTrue());
}

void TestGraphicsView::geometryReportsHiddenItemAsNotVisiblePerView() {
  // The per-view entries must agree with the top level: a hidden item is not
  // clickable in any view, however well-placed its rect.
  m_item->setVisible(false);

  const QJsonObject geo = HitTest::graphicsItemGeometry(m_item);
  QEXPECT_THAT(geo, JsonField("visible", false));
  QEXPECT_THAT(geo[QStringLiteral("views")].toArray(),
               AllOf(JsonArraySize(Eq(1)), Each(JsonField("visible", false))));
}

void TestGraphicsView::geometryWithoutAViewReportsNullGlobal() {
  QGraphicsScene orphanScene;
  auto* orphan = new TestSceneItem();
  orphan->setObjectName(QStringLiteral("orphanItem"));
  orphanScene.addItem(orphan);

  const QJsonObject geo = HitTest::graphicsItemGeometry(orphan);
  QEXPECT_THAT(geo[QStringLiteral("global")].isNull(), IsTrue());
  QEXPECT_THAT(geo, AllOf(JsonField("visible", false), JsonField("views", JsonArraySize(Eq(0))),
                          // Local and scene are still meaningful without a view.
                          JsonField("local", JsonField("width", 40.0))));
}

void TestGraphicsView::geometryUsesBoundingRectNotChildren() {
  // A product is a group: symbol plus label. The clickable body is the item
  // itself, so the default must be boundingRect(), not childrenBoundingRect().
  auto* label = new TestSceneItem(m_item);
  label->setPos(0, 100);  // hangs well below the parent's 20px body

  const QJsonObject geo = HitTest::graphicsItemGeometry(m_item);
  QEXPECT_THAT(geo, JsonField("local", JsonField("height", 20.0)));
}

void TestGraphicsView::uiGeometryRejectsAViewOnAnotherScene() {
  // Naming a view that does not render this item's scene is a caller mistake.
  // Silently reporting a null rect would look exactly like "not on screen yet",
  // so say which views the item actually has.
  QGraphicsScene otherScene;
  QGraphicsView strangerView(&otherScene);
  strangerView.setObjectName(QStringLiteral("strangerView"));
  strangerView.show();
  QApplication::processEvents();
  ObjectRegistry::instance()->scanExistingObjects(&strangerView);

  const QJsonObject error = callExpectError(
      QStringLiteral("qt.ui.geometry"),
      QJsonObject{
          {QStringLiteral("objectId"), ObjectRegistry::instance()->objectId(m_item)},
          {QStringLiteral("viewObjectId"), ObjectRegistry::instance()->objectId(&strangerView)}});

  QEXPECT_THAT(error, QIsNotEmpty());
  const QJsonArray candidates =
      error[QStringLiteral("data")].toObject()[QStringLiteral("views")].toArray();
  QEXPECT_THAT(candidates, JsonArraySize(Eq(1)));
  QEXPECT_THAT(candidates, JsonArrayContains(QStrEq(ObjectRegistry::instance()->objectId(m_view))));
}

void TestGraphicsView::geometrySurvivesARotatedViewTransform() {
  // The hand calibration this replaces recovered a single scale factor by
  // dividing a drag distance, which "silently breaks under any rotation or
  // shear in the view transform". Mapping through the matrix does not.
  m_view->setTransform(QTransform().rotate(90));
  m_view->centerOn(m_item);  // guarantee it is in view whatever the rotation did
  QApplication::processEvents();

  const QJsonObject geo = HitTest::graphicsItemGeometry(m_item);

  // A 40x20 item under a quarter turn presents as 20x40 on screen.
  QEXPECT_THAT(geo[QStringLiteral("viewport")],
               AllOf(JsonField("width", 20.0), JsonField("height", 40.0)));
  QEXPECT_THAT(geo, JsonField("visible", true));

  // And the acceptance property still holds: the reported centre hits the item.
  const QPoint globalCentre = centreOf(geo[QStringLiteral("global")].toObject());
  QEXPECT_THAT(m_view->itemAt(m_view->viewport()->mapFromGlobal(globalCentre)),
               Eq(static_cast<QGraphicsItem*>(m_item)));
}

void TestGraphicsView::geometryFollowsTheItemsOwnTransform() {
  // The item's own transform belongs in the scene rect, not just its position:
  // mapToScene(boundingRect()) carries it, plain pos() arithmetic would not.
  m_item->setRotation(90);

  const QJsonObject geo = HitTest::graphicsItemGeometry(m_item);

  // local is untouched -- it is the item's own space.
  QEXPECT_THAT(geo[QStringLiteral("local")],
               AllOf(JsonField("width", 40.0), JsonField("height", 20.0)));
  // Rotated a quarter turn about its origin, then offset by pos(300, 300).
  QEXPECT_THAT(geo[QStringLiteral("scene")],
               AllOf(JsonField("x", 280.0), JsonField("y", 300.0), JsonField("width", 20.0),
                     JsonField("height", 40.0)));
}

void TestGraphicsView::geometryComposesNestedItemPositions() {
  // A child's scene position is its own plus every ancestor's. Reporting the
  // raw pos() would put a nested item 300 units from where it really is.
  auto* child = new TestSceneItem(m_item);
  child->setObjectName(QStringLiteral("childItem"));
  child->setPos(10, 5);

  const QJsonObject geo = HitTest::graphicsItemGeometry(child);
  QEXPECT_THAT(geo[QStringLiteral("scene")],
               AllOf(JsonField("x", 310.0), JsonField("y", 305.0), JsonField("width", 40.0),
                     JsonField("height", 20.0)));
}

void TestGraphicsView::geometryReportsHiddenViewAsNotVisible() {
  // A view that is not on screen cannot be clicked through, however well the
  // item sits inside its scroll window.
  m_view->hide();
  QApplication::processEvents();

  const QJsonObject geo = HitTest::graphicsItemGeometry(m_item);
  QEXPECT_THAT(geo, JsonField("visible", false));
  QEXPECT_THAT(geo[QStringLiteral("views")].toArray(),
               AllOf(JsonArraySize(Eq(1)), Each(JsonField("visible", false))));
}

void TestGraphicsView::geometryRejectsANullItem() {
  // Plain try/catch, not QVERIFY_THROWS_EXCEPTION: that macro needs Qt 6.3 and
  // the older QVERIFY_EXCEPTION_THROWN is deprecated in Qt 6.11. This compiles
  // identically on both ends of the supported range.
  bool threw = false;
  try {
    HitTest::graphicsItemGeometry(nullptr);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  QEXPECT_THAT(threw, IsTrue());
}

// === HitTest::graphicsItemIdAt ==============================================

void TestGraphicsView::hitTestDescendsIntoTheScene() {
  const QPoint viewportPos =
      m_view->mapFromScene(QPointF(300 + 20, 300 + 10));  // centre of the item

  QEXPECT_THAT(HitTest::graphicsItemIdAt(m_view, viewportPos),
               QStrEq(ObjectRegistry::instance()->objectId(m_item)));
}

void TestGraphicsView::hitTestReturnsNearestGraphicsObjectAncestor() {
  // Plain QGraphicsItems are not QObjects and have no objectId, so a hit on one
  // must report the nearest QGraphicsObject above it rather than nothing.
  auto* decoration = new QGraphicsRectItem(QRectF(0, 0, 40, 20), m_item);
  decoration->setBrush(Qt::red);

  const QPoint viewportPos = m_view->mapFromScene(QPointF(300 + 20, 300 + 10));
  QEXPECT_THAT(HitTest::graphicsItemIdAt(m_view, viewportPos),
               QStrEq(ObjectRegistry::instance()->objectId(m_item)));
}

void TestGraphicsView::hitTestMissesOnEmptyCanvas() {
  const QPoint viewportPos = m_view->mapFromScene(QPointF(900, 900));
  QEXPECT_THAT(HitTest::graphicsItemIdAt(m_view, viewportPos), QIsEmpty());
}

void TestGraphicsView::hitTestMissesItemWithNoGraphicsObjectAncestor() {
  // A bare QGraphicsItem parented to nothing addressable has no objectId to
  // report and no QGraphicsObject to fall back to. That is a miss, not a crash
  // and not the view.
  auto* bare = new QGraphicsRectItem(QRectF(0, 0, 40, 20));
  bare->setPos(350, 250);
  m_scene->addItem(bare);

  const QPoint viewportPos = m_view->mapFromScene(QPointF(350 + 20, 250 + 10));
  QEXPECT_THAT(m_view->itemAt(viewportPos), Eq(static_cast<QGraphicsItem*>(bare)));
  QEXPECT_THAT(HitTest::graphicsItemIdAt(m_view, viewportPos), QIsEmpty());
}

void TestGraphicsView::hitTestReturnsTopmostOfOverlappingItems() {
  // Scene content stacks: an item sits on its container. The hit must be the one the
  // user would actually grab, i.e. the topmost by z.
  auto* above = new TestSceneItem();
  above->setObjectName(QStringLiteral("itemAbove"));
  above->setPos(300, 300);  // exactly over m_item
  above->setZValue(1);
  m_scene->addItem(above);

  const QPoint viewportPos = m_view->mapFromScene(QPointF(320, 310));
  QEXPECT_THAT(HitTest::graphicsItemIdAt(m_view, viewportPos),
               QStrEq(ObjectRegistry::instance()->objectId(above)));

  // Lower it back and the item underneath becomes the answer.
  above->setZValue(-1);
  QEXPECT_THAT(HitTest::graphicsItemIdAt(m_view, viewportPos),
               QStrEq(ObjectRegistry::instance()->objectId(m_item)));
}

void TestGraphicsView::hitTestRejectsANullView() {
  bool threw = false;
  try {
    HitTest::graphicsItemIdAt(nullptr, QPoint(0, 0));
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  QEXPECT_THAT(threw, IsTrue());
}

// === HitTest::widgetIdAt -- global descent ==================================

void TestGraphicsView::widgetIdAtDescendsIntoTheSceneFromAGlobalPoint() {
  // The secondary half of the fix: a global hit landing on a view must report
  // the item, not the viewport widget it is painted on. Without this, every
  // point in a plan resolves to the same opaque widget.
  const QPoint viewportPos = m_view->mapFromScene(QPointF(320, 310));
  const QPoint globalPos = m_view->viewport()->mapToGlobal(viewportPos);

  // Guard the premise: if the platform cannot hit-test widgets, the assertion
  // below would pass or fail for reasons that have nothing to do with scenes.
  if (QApplication::widgetAt(globalPos) != m_view->viewport()) {
    QSKIP("platform hit testing unavailable: QApplication::widgetAt missed the viewport");
  }

  QEXPECT_THAT(HitTest::widgetIdAt(globalPos),
               QStrEq(ObjectRegistry::instance()->objectId(m_item)));
}

void TestGraphicsView::widgetIdAtFallsBackToTheViewOnEmptyCanvas() {
  // Empty canvas is not a miss -- the viewport really is what is under the
  // point, and callers relied on that answer before scenes were searched.
  // Well inside the viewport and well clear of the item at (300, 300): a point
  // near the edge risks landing on a scroll bar rather than the canvas.
  const QPoint viewportPos = m_view->mapFromScene(QPointF(250, 250));
  const QPoint globalPos = m_view->viewport()->mapToGlobal(viewportPos);

  if (QApplication::widgetAt(globalPos) != m_view->viewport()) {
    QSKIP("platform hit testing unavailable: QApplication::widgetAt missed the viewport");
  }

  QEXPECT_THAT(m_view->itemAt(viewportPos), IsNull());
  QEXPECT_THAT(HitTest::widgetIdAt(globalPos),
               QStrEq(ObjectRegistry::instance()->objectId(m_view->viewport())));
}

// === qt.ui.* wiring =========================================================

void TestGraphicsView::uiGeometryAcceptsAGraphicsObject() {
  const QString objectId = ObjectRegistry::instance()->objectId(m_item);
  QEXPECT_THAT(objectId, QIsNotEmpty());

  const QJsonValue result = callResult(QStringLiteral("qt.ui.geometry"),
                                       QJsonObject{{QStringLiteral("objectId"), objectId}});
  QEXPECT_THAT(result.isObject(), IsTrue());

  const QJsonObject geo = result.toObject();
  QEXPECT_THAT(geo, AllOf(HasJsonField("global"), HasJsonField("scene"), HasJsonField("views"),
                          JsonField("visible", true)));

  // Same acceptance property as above, this time end to end through the API.
  const QPoint globalCentre = centreOf(geo[QStringLiteral("global")].toObject());
  QEXPECT_THAT(m_view->itemAt(m_view->viewport()->mapFromGlobal(globalCentre)),
               Eq(static_cast<QGraphicsItem*>(m_item)));
}

void TestGraphicsView::uiGeometryStillWorksForWidgets() {
  // The widget path keeps its existing int-valued {local, global,
  // devicePixelRatio} shape; nothing about it changes.
  const QString viewId = ObjectRegistry::instance()->objectId(m_view);
  const QJsonObject geo = callResult(QStringLiteral("qt.ui.geometry"),
                                     QJsonObject{{QStringLiteral("objectId"), viewId}})
                              .toObject();

  QEXPECT_THAT(geo,
               AllOf(JsonField("local", JsonField("width", 400)), DoesNotHaveJsonField("views")));

  // A plain QObject is still an error, and still names the class.
  QObject plain;
  plain.setObjectName(QStringLiteral("notVisual"));
  const QJsonObject error = callExpectError(
      QStringLiteral("qt.ui.geometry"),
      QJsonObject{{QStringLiteral("objectId"), ObjectRegistry::instance()->objectId(&plain)}});
  QEXPECT_THAT(error, QIsNotEmpty());
}

void TestGraphicsView::uiHitTestScopedToAViewFindsTheItem() {
  // Global-coordinate hit testing goes through QApplication::widgetAt, which is
  // unreliable under the minimal QPA plugin. Scoping to a view makes the scene
  // descent testable headlessly -- and is what a caller who already knows the
  // view wants anyway.
  const QPoint viewportPos = m_view->mapFromScene(QPointF(320, 310));

  const QJsonObject result = callResult(QStringLiteral("qt.ui.hitTest"),
                                        QJsonObject{{QStringLiteral("viewObjectId"),
                                                     ObjectRegistry::instance()->objectId(m_view)},
                                                    {QStringLiteral("x"), viewportPos.x()},
                                                    {QStringLiteral("y"), viewportPos.y()}})
                                 .toObject();

  QEXPECT_THAT(result,
               AllOf(JsonField("objectId", QStrEq(ObjectRegistry::instance()->objectId(m_item))),
                     JsonField("className", QStrEq(QStringLiteral("TestSceneItem")))));
}

void TestGraphicsView::uiGeometryHonoursViewObjectId() {
  QGraphicsView second(m_scene);
  second.setObjectName(QStringLiteral("deviceLayoutView"));
  second.setTransform(QTransform::fromScale(0.5, 0.5));
  second.resize(200, 200);
  second.show();
  QApplication::processEvents();
  ObjectRegistry::instance()->scanExistingObjects(&second);

  const QJsonObject geo =
      callResult(
          QStringLiteral("qt.ui.geometry"),
          QJsonObject{
              {QStringLiteral("objectId"), ObjectRegistry::instance()->objectId(m_item)},
              {QStringLiteral("viewObjectId"), ObjectRegistry::instance()->objectId(&second)}})
          .toObject();

  // 40 scene units at the second view's 0.5x, not the first view's 1.25x.
  QEXPECT_THAT(geo, AllOf(JsonField("viewport", JsonField("width", 20.0)),
                          JsonField("views", JsonArraySize(Eq(2)))));
}

void TestGraphicsView::uiGeometryRejectsAnUnknownViewObjectId() {
  QEXPECT_THAT(
      call(QStringLiteral("qt.ui.geometry"),
           QJsonObject{{QStringLiteral("objectId"), ObjectRegistry::instance()->objectId(m_item)},
                       {QStringLiteral("viewObjectId"), QStringLiteral("NoSuchView")}}),
      IsJsonRpcError(Eq(ErrorCode::kObjectNotFound)));
}

void TestGraphicsView::uiGeometryRejectsAViewObjectIdThatIsNotAView() {
  QPushButton button;
  button.setObjectName(QStringLiteral("notAView"));

  QEXPECT_THAT(
      call(QStringLiteral("qt.ui.geometry"),
           QJsonObject{
               {QStringLiteral("objectId"), ObjectRegistry::instance()->objectId(m_item)},
               {QStringLiteral("viewObjectId"), ObjectRegistry::instance()->objectId(&button)}}),
      IsJsonRpcError(Eq(ErrorCode::kObjectNotWidget)));
}

void TestGraphicsView::uiHitTestScopedReportsAMissAsAnError() {
  // Empty canvas inside a named view: a miss, reported as one rather than as
  // the view itself, since the caller asked about the scene.
  const QPoint viewportPos = m_view->mapFromScene(QPointF(450, 350));

  QEXPECT_THAT(call(QStringLiteral("qt.ui.hitTest"),
                    QJsonObject{{QStringLiteral("viewObjectId"),
                                 ObjectRegistry::instance()->objectId(m_view)},
                                {QStringLiteral("x"), viewportPos.x()},
                                {QStringLiteral("y"), viewportPos.y()}}),
               IsJsonRpcError(Eq(ErrorCode::kObjectNotFound)));
}

void TestGraphicsView::inspectGeometryPartNamesTheCoordinateSpace() {
  // A widget's geometry part is parent-relative and a scene item's is scene
  // space. They are not comparable, so the item's says which it is.
  const QJsonObject result =
      callResult(
          QStringLiteral("qt.objects.inspect"),
          QJsonObject{{QStringLiteral("objectId"), ObjectRegistry::instance()->objectId(m_item)},
                      {QStringLiteral("parts"), QJsonArray{QStringLiteral("geometry")}}})
          .toObject();
  QEXPECT_THAT(result[QStringLiteral("geometry")], JsonField("coordinateSpace", "scene"));

  // The widget form carries no such key, and keeps its integer values.
  const QJsonObject viewResult =
      callResult(
          QStringLiteral("qt.objects.inspect"),
          QJsonObject{{QStringLiteral("objectId"), ObjectRegistry::instance()->objectId(m_view)},
                      {QStringLiteral("parts"), QJsonArray{QStringLiteral("geometry")}}})
          .toObject();
  QEXPECT_THAT(viewResult[QStringLiteral("geometry")].toObject(),
               DoesNotHaveJsonField("coordinateSpace"));
}

void TestGraphicsView::inspectGeometryPartCoversGraphicsObjects() {
  // qt.objects.inspect reported className/objectName and a null geometry for
  // scene items, which is what left callers with nothing to map with.
  const QJsonObject result =
      callResult(
          QStringLiteral("qt.objects.inspect"),
          QJsonObject{{QStringLiteral("objectId"), ObjectRegistry::instance()->objectId(m_item)},
                      {QStringLiteral("parts"), QJsonArray{QStringLiteral("geometry")}}})
          .toObject();

  QEXPECT_THAT(result[QStringLiteral("geometry")],
               AllOf(JsonField("x", 300.0), JsonField("y", 300.0), JsonField("width", 40.0),
                     JsonField("visible", true)));
}

QTEST_MAIN(TestGraphicsView)
#include "test_graphics_view.moc"
