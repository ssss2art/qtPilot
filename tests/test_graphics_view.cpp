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
#include <QGraphicsProxyWidget>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPushButton>
#include <QScrollBar>
#include <QWheelEvent>
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
  explicit TestSceneItem(QGraphicsItem* parent = nullptr) : QGraphicsObject(parent) {
    setFlag(QGraphicsItem::ItemIsFocusable);
  }

  QRectF boundingRect() const override { return QRectF(0, 0, 40, 20); }
  void paint(QPainter*, const QStyleOptionGraphicsItem*, QWidget*) override {}

 signals:
  void clicked();
  void doubleClicked();
  void keyText(const QString& text);

 protected:
  void mousePressEvent(QGraphicsSceneMouseEvent* event) override {
    emit clicked();
    QGraphicsObject::mousePressEvent(event);
  }

  void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override {
    emit doubleClicked();
    QGraphicsObject::mouseDoubleClickEvent(event);
  }

  void keyPressEvent(QKeyEvent* event) override {
    emit keyText(event->text());
    QGraphicsObject::keyPressEvent(event);
  }
};

/// @brief An item with an empty bounding rect, positioning children instead.
///
/// A grouping item that exists only to hold its children -- ordinary in a
/// scene, not a corner case. Its rect is 0x0 rather than merely flat on one
/// axis, and that distinction is the whole point: QRect::intersects() handles a
/// zero-height rect correctly and reports no overlap only for a null one.
class GroupingSceneItem : public QGraphicsObject {
  Q_OBJECT
 public:
  QRectF boundingRect() const override { return QRectF(0, 0, 0, 0); }
  void paint(QPainter*, const QStyleOptionGraphicsItem*, QWidget*) override {}
};

/// @brief A view that records every wheel event its viewport receives, then
/// handles it as usual (so a scene can still route it on to embedded widgets).
class WheelRecordingView : public QGraphicsView {
 public:
  struct Wheel {
    QPoint position;
    QPoint globalPosition;
    QPoint angleDelta;
    QPoint pixelDelta;
    Qt::ScrollPhase phase;
    Qt::KeyboardModifiers modifiers;
    Qt::MouseEventSource source;
  };

  using QGraphicsView::QGraphicsView;
  QList<Wheel> wheels;

 protected:
  void wheelEvent(QWheelEvent* event) override {
    wheels.append({event->position().toPoint(), event->globalPosition().toPoint(),
                   event->angleDelta(), event->pixelDelta(), event->phase(), event->modifiers(),
                   event->source()});
    QGraphicsView::wheelEvent(event);
  }
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
  void geometryReportsAnEmptyRectItemOnScreenAsVisible();

  // HitTest::graphicsItemIdAt
  void hitTestDescendsIntoTheScene();
  void hitTestReturnsNearestGraphicsObjectAncestor();
  void hitTestMissesOnEmptyCanvas();
  void hitTestMissesItemWithNoGraphicsObjectAncestor();
  void hitTestReturnsTopmostOfOverlappingItems();
  void hitTestRejectsANullView();
  void hitTestSeesPastABareItemStackedOnTop();

  // HitTest::widgetIdAt -- global-coordinate descent into a scene
  void widgetIdAtDescendsIntoTheSceneFromAGlobalPoint();
  void widgetIdAtFallsBackToTheViewOnEmptyCanvas();
  void widgetIdAtDoesNotDescendFromAScrollBar();
  void hitTestIgnoresPointsOutsideTheViewport();

  // qt.ui.* wiring
  void uiGeometryAcceptsAGraphicsObject();
  void uiGeometryStillWorksForWidgets();
  void uiHitTestScopedToAViewFindsTheItem();
  void uiGeometryHonoursViewObjectId();
  void uiGeometryRejectsAnUnknownViewObjectId();
  void uiGeometryRejectsAViewObjectIdThatIsNotAView();
  void uiHitTestScopedReportsAMissAsAnError();
  void uiHitTestRejectsAbsentCoordinates();
  void uiHitTestRejectsNonNumericCoordinates();
  void uiHitTestAcceptsFractionalCoordinates();
  void uiHitTestRejectsAMalformedViewObjectId();
  void uiGeometryRejectsAViewObjectIdOnAWidgetTarget();
  void inspectGeometryPartCoversGraphicsObjects();
  void inspectGeometryPartNamesTheCoordinateSpace();
  void uiClickAcceptsAGraphicsObject();
  void uiClickRejectsAViewObjectIdOnAnotherScene();
  void uiClickRejectsAnOffViewportGraphicsObject();
  void uiDoubleClickAcceptsAGraphicsObject();
  void uiSendKeysAcceptsAGraphicsObject();
  void uiWheelOnAGraphicsObjectReachesItsViewportAtTheItem();
  void uiWheelSendsOneEventPerNotchWithSignAndModifiers();
  void uiWheelTrackpadSendsAPhasedGesture();
  void uiWheelRoutesAnEmbeddedViewThroughItsProxy();
  void uiWheelDirectRouteSkipsTheProxy();
  void uiWheelRejectsBadParameters();
  void uiWheelPositionOnAScrollAreaIsInViewportCoordinates();
  void uiWheelRejectsAPointScrolledOutOfTheOuterView();
  void uiWheelRejectsAPointClippedByTheEmbeddingItem();
  void uiWheelDryRunRoutesWithoutSending();

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
  QEXPECT_THAT(geo[QStringLiteral("local")], JsonRectEq(0, 0, 40, 20));

  // Scene is that rect mapped through the item's own transform/position -- the
  // number `pos` already told callers, but as a rect.
  QEXPECT_THAT(geo[QStringLiteral("scene")], JsonRectEq(300, 300, 40, 20));

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
  QEXPECT_THAT(geo[QStringLiteral("viewport")], JsonRectEq(175, 175, 50, 25));

  // Global is the same rect, offset by the viewport's origin on screen.
  const QPoint origin = m_view->viewport()->mapToGlobal(QPoint(0, 0));
  QEXPECT_THAT(global, JsonRectEq(175.0 + origin.x(), 175.0 + origin.y(), 50, 25));
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
  QEXPECT_THAT(geo, AllOf(JsonField("global", JsonRectSize(20, 10)),
                          JsonField("viewport", JsonRectSize(20, 10))));
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
  QEXPECT_THAT(geo[QStringLiteral("viewport")], JsonRectSize(20, 40));
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
  QEXPECT_THAT(geo[QStringLiteral("local")], JsonRectSize(40, 20));
  // Rotated a quarter turn about its origin, then offset by pos(300, 300).
  QEXPECT_THAT(geo[QStringLiteral("scene")], JsonRectEq(280, 300, 20, 40));
}

void TestGraphicsView::geometryComposesNestedItemPositions() {
  // A child's scene position is its own plus every ancestor's. Reporting the
  // raw pos() would put a nested item 300 units from where it really is.
  auto* child = new TestSceneItem(m_item);
  child->setObjectName(QStringLiteral("childItem"));
  child->setPos(10, 5);

  const QJsonObject geo = HitTest::graphicsItemGeometry(child);
  QEXPECT_THAT(geo[QStringLiteral("scene")], JsonRectEq(310, 305, 40, 20));
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

void TestGraphicsView::geometryReportsAnEmptyRectItemOnScreenAsVisible() {
  // A grouping item's rect is null, and QRect::intersects() reports no overlap
  // for a null rect however well placed it is. Testing overlap that way calls
  // such an item invisible wherever it sits, and a caller acting on that would
  // scroll to reveal something already on screen.
  auto* group = new GroupingSceneItem();
  group->setObjectName(QStringLiteral("groupingItem"));
  group->setPos(240, 240);  // 240 * 1.25 - 200 == viewport 100, well inside
  m_scene->addItem(group);

  const QJsonObject geo = HitTest::graphicsItemGeometry(group);
  QEXPECT_THAT(geo[QStringLiteral("scene")], JsonRectEq(240, 240, 0, 0));
  QEXPECT_THAT(geo[QStringLiteral("viewport")], JsonRectEq(100, 100, 0, 0));
  QEXPECT_THAT(geo, JsonField("visible", true));
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

void TestGraphicsView::hitTestSeesPastABareItemStackedOnTop() {
  // Scene decorations -- grid lines, overlays, rubber bands -- are routinely
  // parentless bare QGraphicsItems drawn over the content. itemAt() returns
  // only the topmost, so consulting it alone lets a decoration swallow every
  // addressable item beneath it. The parent-chain walk does not help: the
  // decoration is a sibling, not a child.
  auto* decoration = new QGraphicsRectItem(QRectF(0, 0, 200, 200));
  decoration->setPos(250, 250);  // covers m_item at (300, 300)
  decoration->setZValue(10);
  m_scene->addItem(decoration);

  const QPoint viewportPos = m_view->mapFromScene(QPointF(320, 310));
  QEXPECT_THAT(m_view->itemAt(viewportPos), Eq(static_cast<QGraphicsItem*>(decoration)));

  // The addressable item underneath is what the caller can actually act on.
  QEXPECT_THAT(HitTest::graphicsItemIdAt(m_view, viewportPos),
               QStrEq(ObjectRegistry::instance()->objectId(m_item)));
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

void TestGraphicsView::hitTestIgnoresPointsOutsideTheViewport() {
  // A scroll bar is a child of the view, not of its viewport, so a point on it
  // maps to viewport coordinates beyond the visible extent -- which the view
  // will still happily project into scene space and answer for. Anything
  // outside the viewport is not a hit on the canvas.
  auto* blanket = new TestSceneItem();
  blanket->setObjectName(QStringLiteral("blanketItem"));
  blanket->setPos(0, 0);
  blanket->setScale(100);  // covers the whole scene, so any point would "hit"
  m_scene->addItem(blanket);

  const QRect viewportRect = m_view->viewport()->rect();
  QEXPECT_THAT(HitTest::graphicsItemIdAt(m_view, QPointF(viewportRect.right() + 20, 50)),
               QIsEmpty());
  QEXPECT_THAT(HitTest::graphicsItemIdAt(m_view, QPointF(50, viewportRect.bottom() + 20)),
               QIsEmpty());
  QEXPECT_THAT(HitTest::graphicsItemIdAt(m_view, QPointF(-5, 50)), QIsEmpty());

  // A point inside still resolves, so the guard has not simply disabled it.
  QEXPECT_THAT(HitTest::graphicsItemIdAt(m_view, QPointF(50, 50)), QIsNotEmpty());
}

void TestGraphicsView::widgetIdAtDoesNotDescendFromAScrollBar() {
  // Scroll bars are children of the QGraphicsView, not of its viewport, so a
  // parent-cast alone treats a click on the scroll bar as a click on the
  // canvas. mapFromGlobal then yields a point outside the viewport, which
  // itemAt happily maps into scene space and answers with a real item.
  QScrollBar* bar = m_view->verticalScrollBar();
  if (!bar->isVisible() || bar->width() <= 0) {
    QSKIP("no visible vertical scroll bar to test against");
  }

  // Cover the whole scene so that *any* mapped point would find an item --
  // the assertion then isolates the descent decision rather than the geometry.
  auto* blanket = new TestSceneItem();
  blanket->setObjectName(QStringLiteral("blanketItem"));
  blanket->setPos(0, 0);
  blanket->setScale(100);
  m_scene->addItem(blanket);

  const QPoint globalPos = bar->mapToGlobal(bar->rect().center());
  if (QApplication::widgetAt(globalPos) != bar) {
    QSKIP("platform hit testing unavailable: QApplication::widgetAt missed the scroll bar");
  }

  QEXPECT_THAT(HitTest::widgetIdAt(globalPos), QStrEq(ObjectRegistry::instance()->objectId(bar)));
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
  QEXPECT_THAT(geo, AllOf(JsonField("viewport", JsonRectSize(20, 10)),
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
      IsJsonRpcError(Eq(ErrorCode::kNotGraphicsView)));
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
  // Present on both branches, so it is a usable discriminator rather than a
  // presence test that cannot tell "widget" from "older probe build".
  QEXPECT_THAT(viewResult[QStringLiteral("geometry")], JsonField("coordinateSpace", "parent"));
}

void TestGraphicsView::uiHitTestRejectsAbsentCoordinates() {
  // toInt() yields 0 for an absent key, and viewport (0, 0) is the top-left of
  // a rendered scene -- very often a real item. Without validation the caller
  // gets a confident, successful, wrong answer instead of an error.
  QEXPECT_THAT(call(QStringLiteral("qt.ui.hitTest"),
                    QJsonObject{{QStringLiteral("viewObjectId"),
                                 ObjectRegistry::instance()->objectId(m_view)}}),
               IsJsonRpcError(Eq(JsonRpcError::kInvalidParams)));
}

void TestGraphicsView::uiHitTestRejectsNonNumericCoordinates() {
  QEXPECT_THAT(call(QStringLiteral("qt.ui.hitTest"),
                    QJsonObject{{QStringLiteral("viewObjectId"),
                                 ObjectRegistry::instance()->objectId(m_view)},
                                {QStringLiteral("x"), QStringLiteral("175")},
                                {QStringLiteral("y"), 175}}),
               IsJsonRpcError(Eq(JsonRpcError::kInvalidParams)));
}

void TestGraphicsView::uiHitTestAcceptsFractionalCoordinates() {
  // The geometry this API hands back is fractional by design, so the hit test
  // that consumes it must accept fractional input rather than truncating it to
  // zero. A coordinate round trip has to close.
  const QJsonObject geo =
      callResult(
          QStringLiteral("qt.ui.geometry"),
          QJsonObject{{QStringLiteral("objectId"), ObjectRegistry::instance()->objectId(m_item)}})
          .toObject();
  const QJsonObject viewport = geo[QStringLiteral("viewport")].toObject();
  const double centreX =
      viewport[QStringLiteral("x")].toDouble() + viewport[QStringLiteral("width")].toDouble() / 2.0;
  const double centreY = viewport[QStringLiteral("y")].toDouble() +
                         viewport[QStringLiteral("height")].toDouble() / 2.0;

  const QJsonObject result = callResult(QStringLiteral("qt.ui.hitTest"),
                                        QJsonObject{{QStringLiteral("viewObjectId"),
                                                     ObjectRegistry::instance()->objectId(m_view)},
                                                    {QStringLiteral("x"), centreX + 0.5},
                                                    {QStringLiteral("y"), centreY + 0.5}})
                                 .toObject();

  QEXPECT_THAT(result, JsonField("objectId", QStrEq(ObjectRegistry::instance()->objectId(m_item))));
}

void TestGraphicsView::uiHitTestRejectsAMalformedViewObjectId() {
  // A non-string viewObjectId reads back as an empty QString, which silently
  // selects the global-screen branch -- so x/y quietly change coordinate space
  // and the caller is told about whatever widget sits near the screen origin.
  QEXPECT_THAT(
      call(QStringLiteral("qt.ui.hitTest"), QJsonObject{{QStringLiteral("viewObjectId"), 123},
                                                        {QStringLiteral("x"), 175},
                                                        {QStringLiteral("y"), 175}}),
      IsJsonRpcError(Eq(JsonRpcError::kInvalidParams)));
}

void TestGraphicsView::uiGeometryRejectsAViewObjectIdOnAWidgetTarget() {
  // viewObjectId is meaningless when objectId names a widget. Dropping it
  // silently returns a differently-shaped payload with no hint that half the
  // request was discarded.
  QEXPECT_THAT(
      call(QStringLiteral("qt.ui.geometry"),
           QJsonObject{
               {QStringLiteral("objectId"), ObjectRegistry::instance()->objectId(m_view)},
               {QStringLiteral("viewObjectId"), ObjectRegistry::instance()->objectId(m_view)}}),
      IsJsonRpcError(Eq(JsonRpcError::kInvalidParams)));
}

void TestGraphicsView::uiClickAcceptsAGraphicsObject() {
  QSignalSpy spy(m_item, &TestSceneItem::clicked);

  const QJsonObject result =
      callResult(
          QStringLiteral("qt.ui.click"),
          QJsonObject{
              {QStringLiteral("objectId"), ObjectRegistry::instance()->objectId(m_item)},
              {QStringLiteral("viewObjectId"), ObjectRegistry::instance()->objectId(m_view)}})
          .toObject();
  QApplication::processEvents();
  QApplication::processEvents();

  QEXPECT_THAT(
      result,
      AllOf(JsonField("ok", true), JsonField("target", QStrEq(QStringLiteral("graphicsItem"))),
            JsonField("viewObjectId", QStrEq(ObjectRegistry::instance()->objectId(m_view)))));
  QEXPECT_THAT(spy.size(), Eq(1));
}

void TestGraphicsView::uiClickRejectsAViewObjectIdOnAnotherScene() {
  QGraphicsScene otherScene;
  QGraphicsView strangerView(&otherScene);
  strangerView.setObjectName(QStringLiteral("strangerView"));
  strangerView.resize(200, 200);
  strangerView.show();
  QApplication::processEvents();
  ObjectRegistry::instance()->scanExistingObjects(&strangerView);

  QEXPECT_THAT(
      call(QStringLiteral("qt.ui.click"),
           QJsonObject{{QStringLiteral("objectId"), ObjectRegistry::instance()->objectId(m_item)},
                       {QStringLiteral("viewObjectId"),
                        ObjectRegistry::instance()->objectId(&strangerView)}}),
      IsJsonRpcError(Eq(JsonRpcError::kInvalidParams)));
}

void TestGraphicsView::uiClickRejectsAnOffViewportGraphicsObject() {
  m_item->setPos(5000, 5000);
  QApplication::processEvents();

  QEXPECT_THAT(
      call(QStringLiteral("qt.ui.click"),
           QJsonObject{
               {QStringLiteral("objectId"), ObjectRegistry::instance()->objectId(m_item)},
               {QStringLiteral("viewObjectId"), ObjectRegistry::instance()->objectId(m_view)}}),
      IsJsonRpcError(Eq(ErrorCode::kCoordinateOutOfBounds)));
}

void TestGraphicsView::uiDoubleClickAcceptsAGraphicsObject() {
  QSignalSpy spy(m_item, &TestSceneItem::doubleClicked);

  const QJsonObject result =
      callResult(
          QStringLiteral("qt.ui.doubleClick"),
          QJsonObject{
              {QStringLiteral("objectId"), ObjectRegistry::instance()->objectId(m_item)},
              {QStringLiteral("viewObjectId"), ObjectRegistry::instance()->objectId(m_view)}})
          .toObject();
  QApplication::processEvents();
  QApplication::processEvents();

  QEXPECT_THAT(result, AllOf(JsonField("ok", true),
                             JsonField("target", QStrEq(QStringLiteral("graphicsItem")))));
  QEXPECT_THAT(spy.size(), Eq(1));
}

void TestGraphicsView::uiSendKeysAcceptsAGraphicsObject() {
  QSignalSpy spy(m_item, &TestSceneItem::keyText);

  const QJsonObject result =
      callResult(QStringLiteral("qt.ui.sendKeys"),
                 QJsonObject{
                     {QStringLiteral("objectId"), ObjectRegistry::instance()->objectId(m_item)},
                     {QStringLiteral("viewObjectId"), ObjectRegistry::instance()->objectId(m_view)},
                     {QStringLiteral("text"), QStringLiteral("abc")}})
          .toObject();
  QApplication::processEvents();

  QEXPECT_THAT(result, AllOf(JsonField("ok", true),
                             JsonField("target", QStrEq(QStringLiteral("graphicsItem")))));
  QEXPECT_THAT(spy.size(), Eq(3));
  QEXPECT_THAT(spy.at(0).at(0).toString(), QStrEq(QStringLiteral("a")));
  QEXPECT_THAT(spy.at(1).at(0).toString(), QStrEq(QStringLiteral("b")));
  QEXPECT_THAT(spy.at(2).at(0).toString(), QStrEq(QStringLiteral("c")));
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
               AllOf(JsonRectEq(300, 300, 40, 20), JsonField("visible", true)));
}

void TestGraphicsView::uiWheelOnAGraphicsObjectReachesItsViewportAtTheItem() {
  WheelRecordingView view(m_scene);
  view.setObjectName(QStringLiteral("wheelView"));
  view.resize(1000, 1000);
  view.show();
  QApplication::processEvents();
  ObjectRegistry::instance()->scanExistingObjects(&view);

  const QJsonObject result =
      callResult(QStringLiteral("qt.ui.wheel"),
                 QJsonObject{
                     {QStringLiteral("objectId"), ObjectRegistry::instance()->objectId(m_item)},
                     {QStringLiteral("viewObjectId"), ObjectRegistry::instance()->objectId(&view)}})
          .toObject();
  QApplication::processEvents();

  // The point is chosen the way qt.ui.click chooses one (the centre, or a lattice
  // point when the centre is covered), so it is on the item and in the reply.
  const QJsonObject position = result[QStringLiteral("position")].toObject();
  const QPoint expected(position[QStringLiteral("x")].toInt(),
                        position[QStringLiteral("y")].toInt());
  QEXPECT_THAT(result, AllOf(JsonField("ok", true), JsonField("deferred", true),
                             JsonField("target", QStrEq(QStringLiteral("graphicsItem")))));
  QEXPECT_THAT(view.items(expected).contains(m_item), Eq(true));
  QEXPECT_THAT(view.wheels.size(), Eq(1));
  const auto& wheel = view.wheels.first();
  QEXPECT_THAT(wheel.position, Eq(expected));
  QEXPECT_THAT(wheel.globalPosition, Eq(view.viewport()->mapToGlobal(expected)));
  QEXPECT_THAT(wheel.angleDelta, Eq(QPoint(0, 120)));
  QEXPECT_THAT(wheel.pixelDelta, Eq(QPoint()));
  QEXPECT_THAT(wheel.phase, Eq(Qt::NoScrollPhase));
  QEXPECT_THAT(wheel.source, Eq(Qt::MouseEventNotSynthesized));
}

void TestGraphicsView::uiWheelSendsOneEventPerNotchWithSignAndModifiers() {
  WheelRecordingView view(m_scene);
  view.resize(1000, 1000);
  view.show();
  QApplication::processEvents();
  ObjectRegistry::instance()->scanExistingObjects(&view);

  callResult(QStringLiteral("qt.ui.wheel"),
             QJsonObject{{QStringLiteral("objectId"), ObjectRegistry::instance()->objectId(&view)},
                         {QStringLiteral("notches"), -3},
                         {QStringLiteral("modifiers"), QJsonArray{QStringLiteral("shift")}}});
  QApplication::processEvents();

  QEXPECT_THAT(view.wheels.size(), Eq(3));
  for (const auto& wheel : view.wheels) {
    QEXPECT_THAT(wheel.angleDelta, Eq(QPoint(0, -120)));
    QEXPECT_THAT(wheel.modifiers, Eq(Qt::KeyboardModifiers(Qt::ShiftModifier)));
    QEXPECT_THAT(wheel.position, Eq(view.viewport()->rect().center()));
  }
}

void TestGraphicsView::uiWheelTrackpadSendsAPhasedGesture() {
  WheelRecordingView view(m_scene);
  view.resize(1000, 1000);
  view.show();
  QApplication::processEvents();
  ObjectRegistry::instance()->scanExistingObjects(&view);

  const QJsonObject result =
      callResult(
          QStringLiteral("qt.ui.wheel"),
          QJsonObject{{QStringLiteral("objectId"), ObjectRegistry::instance()->objectId(&view)},
                      {QStringLiteral("device"), QStringLiteral("trackpad")},
                      {QStringLiteral("notches"), 2}})
          .toObject();
  QApplication::processEvents();

  QEXPECT_THAT(result, JsonField("events", 4));
  QEXPECT_THAT(view.wheels.size(), Eq(4));
  QEXPECT_THAT(view.wheels.at(0).phase, Eq(Qt::ScrollBegin));
  QEXPECT_THAT(view.wheels.at(1).phase, Eq(Qt::ScrollUpdate));
  QEXPECT_THAT(view.wheels.at(1).pixelDelta, Eq(QPoint(0, 20)));
  QEXPECT_THAT(view.wheels.at(1).source, Eq(Qt::MouseEventSynthesizedBySystem));
  QEXPECT_THAT(view.wheels.at(2).phase, Eq(Qt::ScrollUpdate));
  QEXPECT_THAT(view.wheels.at(3).phase, Eq(Qt::ScrollEnd));
}

namespace {

/// @brief A view embedded in another view's scene through a QGraphicsProxyWidget,
/// the way a plan or layout editor nests a live view inside a sheet.
struct EmbeddedViewFixture {
  QGraphicsScene outerScene;
  WheelRecordingView outer{&outerScene};
  QGraphicsScene innerScene;
  QWidget* container = new QWidget();
  WheelRecordingView* inner = new WheelRecordingView(&innerScene, container);
  QGraphicsProxyWidget* proxy = nullptr;
  TestSceneItem* item = new TestSceneItem();

  EmbeddedViewFixture() {
    outer.setObjectName(QStringLiteral("sheetView"));
    inner->setObjectName(QStringLiteral("nestedView"));
    outerScene.setSceneRect(0, 0, 800, 600);
    innerScene.setSceneRect(0, 0, 200, 150);
    innerScene.addItem(item);
    item->setObjectName(QStringLiteral("nestedItem"));
    item->setPos(80, 60);
    container->resize(240, 190);
    inner->setGeometry(20, 20, 200, 150);
    proxy = outerScene.addWidget(container);
    proxy->setPos(150, 100);
    outer.resize(800, 600);
    outer.show();
    QApplication::processEvents();
    ObjectRegistry::instance()->scanExistingObjects(&outer);
    ObjectRegistry::instance()->scanExistingObjects(container);
  }

  /// Where a point on @p item is drawn in the outer view's viewport.
  QPoint outerPointOfItemCentre() const {
    const QPoint innerPoint = inner->mapFromScene(item->sceneBoundingRect().center());
    const QPoint inContainer = inner->viewport()->mapTo(container, innerPoint);
    return outer.mapFromScene(proxy->mapToScene(QPointF(inContainer)));
  }
};

}  // namespace

void TestGraphicsView::uiWheelRoutesAnEmbeddedViewThroughItsProxy() {
  // A real wheel over a nested view lands on the viewport of the view drawing
  // it; the application routes it inward. Delivering to the nested view would
  // skip exactly the routing a test of it wants to exercise.
  EmbeddedViewFixture f;

  const QJsonObject result =
      callResult(
          QStringLiteral("qt.ui.wheel"),
          QJsonObject{{QStringLiteral("objectId"), ObjectRegistry::instance()->objectId(f.item)}})
          .toObject();
  QApplication::processEvents();

  QEXPECT_THAT(result,
               AllOf(JsonField("route", QStrEq(QStringLiteral("window"))),
                     JsonField("deliveredTo",
                               QStrEq(ObjectRegistry::instance()->objectId(f.outer.viewport())))));
  QEXPECT_THAT(result[QStringLiteral("chain")].toArray().size(), Eq(2));
  QEXPECT_THAT(f.outer.wheels.size(), Eq(1));
  QEXPECT_THAT(f.outer.wheels.first().position, Eq(f.outerPointOfItemCentre()));
}

void TestGraphicsView::uiWheelDirectRouteSkipsTheProxy() {
  EmbeddedViewFixture f;

  callResult(QStringLiteral("qt.ui.wheel"),
             QJsonObject{{QStringLiteral("objectId"), ObjectRegistry::instance()->objectId(f.item)},
                         {QStringLiteral("route"), QStringLiteral("direct")}});
  QApplication::processEvents();

  QEXPECT_THAT(f.outer.wheels.size(), Eq(0));
  QEXPECT_THAT(f.inner->wheels.size(), Eq(1));
  QEXPECT_THAT(f.inner->wheels.first().position,
               Eq(f.inner->mapFromScene(f.item->sceneBoundingRect().center())));
}

void TestGraphicsView::uiWheelRejectsBadParameters() {
  const QString view = ObjectRegistry::instance()->objectId(m_view);
  for (const QJsonObject& bad :
       {QJsonObject{{QStringLiteral("objectId"), view},
                    {QStringLiteral("device"), QStringLiteral("pen")}},
        QJsonObject{{QStringLiteral("objectId"), view},
                    {QStringLiteral("route"), QStringLiteral("x")}},
        QJsonObject{{QStringLiteral("objectId"), view}, {QStringLiteral("notches"), 0}},
        QJsonObject{{QStringLiteral("objectId"), view},
                    {QStringLiteral("notches"), QStringLiteral("2")}},
        QJsonObject{{QStringLiteral("objectId"), view}, {QStringLiteral("angleDelta"), 120}}}) {
    QEXPECT_THAT(call(QStringLiteral("qt.ui.wheel"), bad),
                 IsJsonRpcError(Eq(JsonRpcError::kInvalidParams)));
  }
  QEXPECT_THAT(
      call(QStringLiteral("qt.ui.wheel"),
           QJsonObject{{QStringLiteral("objectId"), view},
                       {QStringLiteral("position"),
                        QJsonObject{{QStringLiteral("x"), 9000}, {QStringLiteral("y"), 5}}}}),
      IsJsonRpcError(Eq(ErrorCode::kCoordinateOutOfBounds)));
}

void TestGraphicsView::uiWheelPositionOnAScrollAreaIsInViewportCoordinates() {
  // Item geometry and the reply's position are viewport coordinates, so a caller can
  // feed a reply's position straight back to hold the pointer still.
  WheelRecordingView view(m_scene);
  view.resize(1000, 1000);
  view.show();
  QApplication::processEvents();
  ObjectRegistry::instance()->scanExistingObjects(&view);

  const QJsonObject result =
      callResult(
          QStringLiteral("qt.ui.wheel"),
          QJsonObject{{QStringLiteral("objectId"), ObjectRegistry::instance()->objectId(&view)},
                      {QStringLiteral("position"),
                       QJsonObject{{QStringLiteral("x"), 40}, {QStringLiteral("y"), 50}}}})
          .toObject();
  QApplication::processEvents();

  QEXPECT_THAT(view.wheels.size(), Eq(1));
  QEXPECT_THAT(view.wheels.first().position, Eq(QPoint(40, 50)));
  QEXPECT_THAT(result[QStringLiteral("position")].toObject(),
               AllOf(JsonField("x", 40), JsonField("y", 50)));
}

void TestGraphicsView::uiWheelRejectsAPointScrolledOutOfTheOuterView() {
  // The nested view is fully inside its own viewport, but the outer view has been
  // scrolled so the nested view is out of sight: no pointer can be there.
  EmbeddedViewFixture f;
  f.outer.resize(120, 90);
  f.outer.setSceneRect(0, 0, 2000, 2000);
  f.outer.horizontalScrollBar()->setValue(1500);
  f.outer.verticalScrollBar()->setValue(1500);
  QApplication::processEvents();

  QEXPECT_THAT(
      call(QStringLiteral("qt.ui.wheel"),
           QJsonObject{{QStringLiteral("objectId"), ObjectRegistry::instance()->objectId(f.item)}}),
      IsJsonRpcError(Eq(ErrorCode::kCoordinateOutOfBounds)));
  QEXPECT_THAT(f.outer.wheels.size(), Eq(0));
}

void TestGraphicsView::uiWheelRejectsAPointClippedByTheEmbeddingItem() {
  // A frame item that clips its children to a small window over the nested view:
  // the nested item sits outside that window, so no pointer can reach it there.
  EmbeddedViewFixture f;
  auto* frame = new QGraphicsRectItem(0, 0, 40, 40);
  frame->setFlag(QGraphicsItem::ItemClipsChildrenToShape);
  f.outerScene.addItem(frame);
  frame->setPos(f.proxy->pos());
  f.proxy->setParentItem(frame);
  f.proxy->setPos(0, 0);
  QApplication::processEvents();

  QEXPECT_THAT(
      call(QStringLiteral("qt.ui.wheel"),
           QJsonObject{{QStringLiteral("objectId"), ObjectRegistry::instance()->objectId(f.item)}}),
      IsJsonRpcError(Eq(ErrorCode::kCoordinateOutOfBounds)));
  QEXPECT_THAT(f.outer.wheels.size(), Eq(0));
}

void TestGraphicsView::uiWheelDryRunRoutesWithoutSending() {
  EmbeddedViewFixture f;

  const QJsonObject result =
      callResult(
          QStringLiteral("qt.ui.wheel"),
          QJsonObject{{QStringLiteral("objectId"), ObjectRegistry::instance()->objectId(f.item)},
                      {QStringLiteral("dryRun"), true}})
          .toObject();
  QApplication::processEvents();

  QEXPECT_THAT(result, AllOf(JsonField("dryRun", true), JsonField("deferred", false),
                             JsonField("events", 0)));
  const QJsonObject position = result[QStringLiteral("position")].toObject();
  QEXPECT_THAT(QPoint(position[QStringLiteral("x")].toInt(), position[QStringLiteral("y")].toInt()),
               Eq(f.outerPointOfItemCentre()));
  QEXPECT_THAT(f.outer.wheels.size(), Eq(0));
  QEXPECT_THAT(f.inner->wheels.size(), Eq(0));
}

QTEST_MAIN(TestGraphicsView)
#include "test_graphics_view.moc"
