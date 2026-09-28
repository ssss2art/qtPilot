// Copyright (c) 2024-2026 qtPilot Contributors
// SPDX-License-Identifier: MIT

// Held-button gestures: a press, any number of moves with the button still down, then a release.
//
// A drag is only a drag if the app sees it as one. A move made while a button is held has to
// arrive carrying that button (a move with no buttons is a hover), and it has to arrive at the
// widget that took the press even once the pointer has left it, because that widget keeps the
// mouse until the button is released. These tests assert on what the widgets and scene items
// were actually handed, not on what the API echoed back.

#include "api/computer_use_mode_api.h"
#include "api/error_codes.h"
#include "api/native_mode_api.h"
#include "common/qt_matchers.h"
#include "core/object_registry.h"
#include "core/object_resolver.h"
#include "transport/jsonrpc_handler.h"

#include <memory>
#include <vector>

#include <QApplication>
#include <QGraphicsObject>
#include <QGraphicsProxyWidget>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMainWindow>
#include <QMouseEvent>
#include <QWidget>
#include <QtTest>

using namespace qtPilot;
using namespace qtPilot::test;
using ::testing::ElementsAre;
using ::testing::IsEmpty;

namespace {

/// @brief One mouse event as a widget received it.
struct MouseRecord {
  QEvent::Type type;
  Qt::MouseButton button;
  Qt::MouseButtons buttons;
  QPoint pos;
};

void PrintTo(const MouseRecord& r, std::ostream* os) {
  const char* kind = r.type == QEvent::MouseMove             ? "move"
                     : r.type == QEvent::MouseButtonPress    ? "press"
                     : r.type == QEvent::MouseButtonRelease  ? "release"
                     : r.type == QEvent::MouseButtonDblClick ? "dblclick"
                                                             : "other";
  *os << kind << " button=" << int(r.button) << " buttons=" << int(r.buttons) << " at ("
      << r.pos.x() << ", " << r.pos.y() << ")";
}

/// @brief A widget that keeps every mouse event it is handed, in order.
class GestureRecorder : public QWidget {
  Q_OBJECT

 public:
  using QWidget::QWidget;

  std::vector<MouseRecord> events;

  std::vector<MouseRecord> moves() const { return ofType(QEvent::MouseMove); }
  std::vector<MouseRecord> presses() const { return ofType(QEvent::MouseButtonPress); }
  std::vector<MouseRecord> releases() const { return ofType(QEvent::MouseButtonRelease); }

 protected:
  void mousePressEvent(QMouseEvent* e) override { record(e); }
  void mouseReleaseEvent(QMouseEvent* e) override { record(e); }
  void mouseMoveEvent(QMouseEvent* e) override { record(e); }

 private:
  void record(QMouseEvent* e) {
    events.push_back({e->type(), e->button(), e->buttons(), e->pos()});
  }
  std::vector<MouseRecord> ofType(QEvent::Type type) const {
    std::vector<MouseRecord> out;
    for (const MouseRecord& r : events) {
      if (r.type == type)
        out.push_back(r);
    }
    return out;
  }
};

/// @brief A scene item the user can pick up and drag, as in a plan or diagram editor.
class MovableSceneItem : public QGraphicsObject {
  Q_OBJECT

 public:
  MovableSceneItem() { setFlag(QGraphicsItem::ItemIsMovable); }
  QRectF boundingRect() const override { return QRectF(0, 0, 40, 20); }
  void paint(QPainter*, const QStyleOptionGraphicsItem*, QWidget*) override {}
};

}  // namespace

// ============================================================================
// Matchers
// ============================================================================

/// @brief A move event with exactly @a held buttons down (Qt::NoButton: a hover).
MATCHER_P(IsMoveHolding, held, "") {
  const MouseRecord& r = arg;
  if (r.type != QEvent::MouseMove) {
    *result_listener << "is not a move";
    return false;
  }
  if (r.buttons != Qt::MouseButtons(held)) {
    *result_listener << "held buttons were " << int(r.buttons) << ", expected "
                     << int(Qt::MouseButtons(held));
    return false;
  }
  return true;
}

/// @brief A move event with @a held buttons down, at widget-local @a at.
MATCHER_P2(IsMoveHoldingAt, held, at, "") {
  const MouseRecord& r = arg;
  if (!::testing::ExplainMatchResult(IsMoveHolding(held), r, result_listener))
    return false;
  if (r.pos != at) {
    *result_listener << "at (" << r.pos.x() << ", " << r.pos.y() << ")";
    return false;
  }
  return true;
}

/// @brief A release of @a button.
MATCHER_P(IsReleaseOf, button, "") {
  const MouseRecord& r = arg;
  if (r.type != QEvent::MouseButtonRelease || r.button != button) {
    *result_listener << "was not a release of button " << int(button);
    return false;
  }
  return true;
}

class TestHeldMouseGestures : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase();
  void cleanupTestCase();
  void init();
  void cleanup();

  // --- cu.mouseDown / cu.mouseMove / cu.mouseUp ---
  void cuMoveWhileLeftHeldCarriesTheLeftButton();
  void cuMoveWhileRightHeldCarriesTheRightButton();
  void cuMoveAfterReleaseIsAHoverAgain();
  void cuScreenAbsoluteMoveWhileHeldReachesTheApp();
  void cuMoveWhileHeldFollowsThePressedWidgetOffIt();
  void cuReleaseAfterDraggingOffReachesThePressedWidget();
  void cuDragPastTheWindowEdgeStaysWithThePressedWidget();
  void cuReleasePastTheWindowEdgeEndsTheGesture();
  void cuSecondPressWhileHeldGoesToThePressedWidget();
  void cuHoldIsDroppedWhenThePressedWidgetIsDestroyed();
  void cuReleasingHeldButtonsReleasesThemAtThePressedWidget();
  void cuWithNoActiveWindowTargetsTheRealWindowNotASceneEmbeddedWidget();
  void cuWithNoActiveWindowPrefersAMainWindow();

  // --- qt.ui.mouseDown / qt.ui.mouseMove / qt.ui.mouseUp ---
  void uiHeldGestureDragsAnItemBeforeTheRelease();
  void uiHeldGestureOnAWidgetKeepsDeliveringOffIt();
  void uiMouseUpWithoutADeltaReleasesWhereThePointerLastWas();
  void uiMouseMoveWithoutAHeldButtonIsRejected();
  void uiMouseUpWithoutAHeldButtonIsRejected();
  void uiMouseDownWhileHeldIsRejected();
  void uiMouseMoveRequiresADelta();
  void uiMouseDownRejectsAnItemOutsideTheViewport();
  void uiMouseDownRejectsAMalformedPosition();
  void uiHoldIsDroppedWhenItsTargetIsDestroyed();
  void uiReleasingTheHeldMouseReleasesItAtItsTarget();

 private:
  QJsonObject call(const QString& method, const QJsonObject& params);
  QJsonObject callOk(const QString& method, const QJsonObject& params);
  static void pump();

  /// @brief Window-relative CU params for @a widget-local @a local.
  QJsonObject cuAt(QWidget* widget, QPoint local, const QString& button = QString()) const;
  QString idOf(QObject* obj) const { return ObjectRegistry::instance()->objectId(obj); }
  /// @brief Leave the app with no active window, as when it sits behind another app.
  static void sendAppToBackground();

  JsonRpcHandler* m_handler = nullptr;
  NativeModeApi* m_nativeApi = nullptr;
  ComputerUseModeApi* m_cuApi = nullptr;
  QWidget* m_window = nullptr;
  GestureRecorder* m_pad = nullptr;
  GestureRecorder* m_neighbour = nullptr;
  QGraphicsScene* m_scene = nullptr;
  QGraphicsView* m_view = nullptr;
  MovableSceneItem* m_item = nullptr;
  int m_requestId = 1;
};

void TestHeldMouseGestures::initTestCase() {
  installObjectHooks();
}

void TestHeldMouseGestures::cleanupTestCase() {
  uninstallObjectHooks();
}

void TestHeldMouseGestures::init() {
  m_window = new QWidget();
  m_window->setObjectName(QStringLiteral("gestureWindow"));
  m_window->setGeometry(100, 100, 700, 300);
  auto* layout = new QHBoxLayout(m_window);

  m_pad = new GestureRecorder(m_window);
  m_pad->setObjectName(QStringLiteral("pad"));
  m_pad->setMinimumSize(150, 150);
  // Tracking on, so a hover reaches the widget too and a lost button shows up as a hover rather
  // than as no event at all.
  m_pad->setMouseTracking(true);
  layout->addWidget(m_pad);

  m_neighbour = new GestureRecorder(m_window);
  m_neighbour->setObjectName(QStringLiteral("neighbour"));
  m_neighbour->setMinimumSize(150, 150);
  m_neighbour->setMouseTracking(true);
  layout->addWidget(m_neighbour);

  m_scene = new QGraphicsScene(0, 0, 200, 200);
  m_item = new MovableSceneItem();
  m_item->setObjectName(QStringLiteral("movableItem"));
  m_item->setPos(50, 50);
  m_scene->addItem(m_item);
  m_view = new QGraphicsView(m_scene, m_window);
  m_view->setObjectName(QStringLiteral("planView"));
  // A scaled view, so a delta that skipped the view transform would land the item elsewhere.
  m_view->setTransform(QTransform::fromScale(2.0, 2.0));
  m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_view->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  m_view->setMinimumSize(300, 280);
  layout->addWidget(m_view);

  m_window->show();
  QVERIFY(QTest::qWaitForWindowExposed(m_window));
  m_window->activateWindow();
  pump();

  m_handler = new JsonRpcHandler(this);
  m_nativeApi = new NativeModeApi(m_handler, this);
  m_cuApi = new ComputerUseModeApi(m_handler, this);
  ObjectRegistry::instance()->scanExistingObjects(m_window);
  ObjectRegistry::instance()->scanExistingObjects(m_scene);
}

void TestHeldMouseGestures::cleanup() {
  // The held state is process-wide, as it is for a probe serving one client after another:
  // never let one test's press leak into the next.
  NativeModeApi::releaseHeldMouse();
  ComputerUseModeApi::releaseHeldButtons();
  pump();

  ObjectResolver::clearNumericIds();
  delete m_window;  // owns the view
  m_window = nullptr;
  delete m_scene;  // owns the item
  m_scene = nullptr;
  delete m_cuApi;
  m_cuApi = nullptr;
  delete m_nativeApi;
  m_nativeApi = nullptr;
  delete m_handler;
  m_handler = nullptr;
}

QJsonObject TestHeldMouseGestures::call(const QString& method, const QJsonObject& params) {
  QJsonObject request{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                      {QStringLiteral("method"), method},
                      {QStringLiteral("params"), params},
                      {QStringLiteral("id"), m_requestId++}};
  const QString response = m_handler->HandleMessage(
      QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact)));
  pump();
  return QJsonDocument::fromJson(response.toUtf8()).object();
}

QJsonObject TestHeldMouseGestures::callOk(const QString& method, const QJsonObject& params) {
  const QJsonObject response = call(method, params);
  QCHECK_THAT(response, IsJsonRpcSuccess());
  return response[QStringLiteral("result")].toObject()[QStringLiteral("result")].toObject();
}

/// qt.ui.* queue their events so the RPC returns first; let them land.
void TestHeldMouseGestures::pump() {
  QCoreApplication::processEvents();
  QTest::qWait(10);
  QCoreApplication::processEvents();
}

QJsonObject TestHeldMouseGestures::cuAt(QWidget* widget, QPoint local,
                                        const QString& button) const {
  const QPoint p = widget->mapTo(m_window, local);
  QJsonObject params{{QStringLiteral("x"), p.x()}, {QStringLiteral("y"), p.y()}};
  if (!button.isEmpty())
    params[QStringLiteral("button")] = button;
  return params;
}

void TestHeldMouseGestures::sendAppToBackground() {
  QWidget transient;
  transient.show();
  QVERIFY(QTest::qWaitForWindowExposed(&transient));
  transient.activateWindow();
  pump();
  transient.hide();
  pump();
  QCOMPARE(QApplication::activeWindow(), nullptr);
}

// ============================================================================
// cu.* held-button gestures
// ============================================================================

void TestHeldMouseGestures::cuMoveWhileLeftHeldCarriesTheLeftButton() {
  callOk(QStringLiteral("cu.mouseDown"), cuAt(m_pad, QPoint(20, 20)));
  callOk(QStringLiteral("cu.mouseMove"), cuAt(m_pad, QPoint(60, 40)));

  QEXPECT_THAT(m_pad->moves(), ElementsAre(IsMoveHoldingAt(Qt::LeftButton, QPoint(60, 40))));
}

void TestHeldMouseGestures::cuMoveWhileRightHeldCarriesTheRightButton() {
  callOk(QStringLiteral("cu.mouseDown"), cuAt(m_pad, QPoint(20, 20), QStringLiteral("right")));
  callOk(QStringLiteral("cu.mouseMove"), cuAt(m_pad, QPoint(60, 40)));

  QEXPECT_THAT(m_pad->moves(), ElementsAre(IsMoveHolding(Qt::RightButton)));
}

void TestHeldMouseGestures::cuMoveAfterReleaseIsAHoverAgain() {
  callOk(QStringLiteral("cu.mouseDown"), cuAt(m_pad, QPoint(20, 20)));
  callOk(QStringLiteral("cu.mouseUp"), cuAt(m_pad, QPoint(20, 20)));
  m_pad->events.clear();

  callOk(QStringLiteral("cu.mouseMove"), cuAt(m_pad, QPoint(60, 40)));

  QEXPECT_THAT(m_pad->moves(), ElementsAre(IsMoveHolding(Qt::NoButton)));
}

void TestHeldMouseGestures::cuScreenAbsoluteMoveWhileHeldReachesTheApp() {
  // Warping the cursor is fine for a hover, but mid-drag the app has to be told the pointer
  // moved with the button down, or the drag never progresses.
  callOk(QStringLiteral("cu.mouseDown"), cuAt(m_pad, QPoint(20, 20)));
  const QPoint global = m_pad->mapToGlobal(QPoint(60, 40));
  callOk(QStringLiteral("cu.mouseMove"), QJsonObject{{QStringLiteral("x"), global.x()},
                                                     {QStringLiteral("y"), global.y()},
                                                     {QStringLiteral("screenAbsolute"), true}});

  QEXPECT_THAT(m_pad->moves(), ElementsAre(IsMoveHoldingAt(Qt::LeftButton, QPoint(60, 40))));
}

void TestHeldMouseGestures::cuMoveWhileHeldFollowsThePressedWidgetOffIt() {
  // Dragging a slider handle past the end of its groove, or a splitter past its neighbour: the
  // pressed widget keeps getting the moves, and the widget now under the pointer gets none.
  callOk(QStringLiteral("cu.mouseDown"), cuAt(m_pad, QPoint(20, 20)));
  callOk(QStringLiteral("cu.mouseMove"), cuAt(m_neighbour, QPoint(30, 30)));

  const QPoint expected = m_pad->mapFrom(m_window, m_neighbour->mapTo(m_window, QPoint(30, 30)));
  QEXPECT_THAT(m_pad->moves(), ElementsAre(IsMoveHoldingAt(Qt::LeftButton, expected)));
  QEXPECT_THAT(m_neighbour->events, IsEmpty());
}

void TestHeldMouseGestures::cuReleaseAfterDraggingOffReachesThePressedWidget() {
  callOk(QStringLiteral("cu.mouseDown"), cuAt(m_pad, QPoint(20, 20)));
  callOk(QStringLiteral("cu.mouseMove"), cuAt(m_neighbour, QPoint(30, 30)));
  callOk(QStringLiteral("cu.mouseUp"), cuAt(m_neighbour, QPoint(30, 30)));

  QEXPECT_THAT(m_pad->releases(), ElementsAre(IsReleaseOf(Qt::LeftButton)));
  QEXPECT_THAT(m_neighbour->events, IsEmpty());
}

void TestHeldMouseGestures::cuDragPastTheWindowEdgeStaysWithThePressedWidget() {
  // A drag may leave the window (a splitter or a scrollbar pulled past the edge): the pressed
  // widget still has the mouse, so the move reaches it rather than failing out of bounds.
  callOk(QStringLiteral("cu.mouseDown"), cuAt(m_pad, QPoint(20, 20)));
  callOk(QStringLiteral("cu.mouseMove"),
         QJsonObject{{QStringLiteral("x"), -40}, {QStringLiteral("y"), 30}});

  const QPoint expected = m_pad->mapFrom(m_window, QPoint(-40, 30));
  QEXPECT_THAT(m_pad->moves(), ElementsAre(IsMoveHoldingAt(Qt::LeftButton, expected)));
}

void TestHeldMouseGestures::cuReleasePastTheWindowEdgeEndsTheGesture() {
  callOk(QStringLiteral("cu.mouseDown"), cuAt(m_pad, QPoint(20, 20)));
  callOk(QStringLiteral("cu.mouseUp"),
         QJsonObject{{QStringLiteral("x"), -40}, {QStringLiteral("y"), 30}});
  callOk(QStringLiteral("cu.mouseMove"), cuAt(m_pad, QPoint(60, 40)));

  // The pressed widget is let go, and the probe no longer thinks a button is down.
  QEXPECT_THAT(m_pad->releases(), ElementsAre(IsReleaseOf(Qt::LeftButton)));
  QEXPECT_THAT(m_pad->moves(), ElementsAre(IsMoveHolding(Qt::NoButton)));
}

void TestHeldMouseGestures::cuSecondPressWhileHeldGoesToThePressedWidget() {
  callOk(QStringLiteral("cu.mouseDown"), cuAt(m_pad, QPoint(20, 20)));
  callOk(QStringLiteral("cu.mouseDown"),
         cuAt(m_neighbour, QPoint(30, 30), QStringLiteral("right")));

  QEXPECT_THAT(m_pad->presses(), ::testing::SizeIs(2));
  QEXPECT_THAT(m_neighbour->events, IsEmpty());
}

void TestHeldMouseGestures::cuHoldIsDroppedWhenThePressedWidgetIsDestroyed() {
  // Pressing a "close" control can delete the very widget that took the press.
  auto doomed = std::make_unique<GestureRecorder>(m_window);
  doomed->setGeometry(0, 0, 40, 40);
  doomed->show();
  doomed->raise();
  pump();
  callOk(QStringLiteral("cu.mouseDown"), cuAt(doomed.get(), QPoint(10, 10)));
  doomed.reset();
  pump();

  callOk(QStringLiteral("cu.mouseMove"), cuAt(m_pad, QPoint(60, 40)));
  QEXPECT_THAT(m_pad->moves(), ElementsAre(IsMoveHolding(Qt::NoButton)));

  // And the next gesture grabs as usual.
  callOk(QStringLiteral("cu.mouseDown"), cuAt(m_pad, QPoint(60, 40)));
  callOk(QStringLiteral("cu.mouseMove"), cuAt(m_neighbour, QPoint(30, 30)));
  QEXPECT_THAT(m_pad->moves(), ::testing::Contains(IsMoveHolding(Qt::LeftButton)));
  QEXPECT_THAT(m_neighbour->events, IsEmpty());
}

void TestHeldMouseGestures::cuReleasingHeldButtonsReleasesThemAtThePressedWidget() {
  // What the probe does when the client that pressed goes away mid-drag.
  callOk(QStringLiteral("cu.mouseDown"), cuAt(m_pad, QPoint(20, 20)));
  ComputerUseModeApi::releaseHeldButtons();
  pump();

  QEXPECT_THAT(m_pad->releases(), ElementsAre(IsReleaseOf(Qt::LeftButton)));
  m_pad->events.clear();
  callOk(QStringLiteral("cu.mouseMove"), cuAt(m_pad, QPoint(60, 40)));
  QEXPECT_THAT(m_pad->moves(), ElementsAre(IsMoveHolding(Qt::NoButton)));
}

void TestHeldMouseGestures::cuWithNoActiveWindowTargetsTheRealWindowNotASceneEmbeddedWidget() {
  // Widgets embedded in a QGraphicsScene are visible top-levels too, but they are drawn into the
  // scene and never shown on screen. topLevelWidgets() comes from a hash, so its order is not
  // fixed: with 63 of them the real window comes first, and the old code passes by luck, one run
  // in 64.
  QGraphicsScene embeddingScene;
  for (int i = 0; i < 63; ++i) {
    auto* embedded = new GestureRecorder();
    embedded->resize(700, 300);
    embeddingScene.addWidget(embedded);
  }
  sendAppToBackground();

  callOk(QStringLiteral("cu.click"), cuAt(m_pad, QPoint(20, 20)));

  QEXPECT_THAT(m_pad->presses().size(), ::testing::Eq(1u));
}

void TestHeldMouseGestures::cuWithNoActiveWindowPrefersAMainWindow() {
  // A tool window or a detached panel is as visible as the main window; a user who has not said
  // otherwise is looking at the main one. As above, enough decoys that hash order can't hide a
  // regression, bar one run in 16.
  std::vector<std::unique_ptr<QWidget>> panels;
  for (int i = 0; i < 15; ++i) {
    panels.push_back(std::make_unique<QWidget>());
    panels.back()->resize(700, 300);
    panels.back()->show();
  }
  QMainWindow main;
  auto* canvas = new GestureRecorder();
  main.setCentralWidget(canvas);
  main.resize(400, 300);
  main.show();
  QVERIFY(QTest::qWaitForWindowExposed(&main));
  ObjectRegistry::instance()->scanExistingObjects(&main);
  sendAppToBackground();

  const QPoint p = canvas->mapTo(&main, QPoint(20, 20));
  callOk(QStringLiteral("cu.click"),
         QJsonObject{{QStringLiteral("x"), p.x()}, {QStringLiteral("y"), p.y()}});

  QEXPECT_THAT(canvas->presses(), ::testing::SizeIs(1));
}

// ============================================================================
// qt.ui.* held-button gestures
// ============================================================================

void TestHeldMouseGestures::uiHeldGestureDragsAnItemBeforeTheRelease() {
  const QJsonObject down = callOk(QStringLiteral("qt.ui.mouseDown"),
                                  QJsonObject{{QStringLiteral("objectId"), idOf(m_item)}});
  QEXPECT_THAT(down, AllOf(JsonField("ok", true),
                           JsonField("targetObjectId", QStrEq(idOf(m_view->viewport())))));

  // 40x20 viewport pixels at 2x is 20x10 scene units.
  callOk(QStringLiteral("qt.ui.mouseMove"),
         QJsonObject{{QStringLiteral("delta"),
                      QJsonObject{{QStringLiteral("x"), 40}, {QStringLiteral("y"), 20}}}});

  // Observable mid-gesture: the item has moved and the scene still has it grabbed.
  QEXPECT_THAT(m_item->pos().toPoint(), ::testing::Eq(QPoint(70, 60)));
  QEXPECT_THAT(m_scene->mouseGrabberItem(), ::testing::Eq(m_item));

  callOk(QStringLiteral("qt.ui.mouseUp"), QJsonObject{});

  QEXPECT_THAT(m_item->pos().toPoint(), ::testing::Eq(QPoint(70, 60)));
  QEXPECT_THAT(m_scene->mouseGrabberItem(), ::testing::IsNull());
}

void TestHeldMouseGestures::uiHeldGestureOnAWidgetKeepsDeliveringOffIt() {
  callOk(QStringLiteral("qt.ui.mouseDown"),
         QJsonObject{{QStringLiteral("objectId"), idOf(m_pad)},
                     {QStringLiteral("position"),
                      QJsonObject{{QStringLiteral("x"), 20}, {QStringLiteral("y"), 20}}}});
  const int pastTheEdge = m_pad->width() + 30;
  callOk(QStringLiteral("qt.ui.mouseMove"),
         QJsonObject{{QStringLiteral("delta"),
                      QJsonObject{{QStringLiteral("x"), pastTheEdge}, {QStringLiteral("y"), 0}}}});
  callOk(QStringLiteral("qt.ui.mouseUp"), QJsonObject{});

  QEXPECT_THAT(m_pad->moves(),
               ElementsAre(IsMoveHoldingAt(Qt::LeftButton, QPoint(20 + pastTheEdge, 20))));
  QEXPECT_THAT(m_pad->releases(), ElementsAre(IsReleaseOf(Qt::LeftButton)));
  QEXPECT_THAT(m_neighbour->events, IsEmpty());
}

void TestHeldMouseGestures::uiMouseUpWithoutADeltaReleasesWhereThePointerLastWas() {
  callOk(QStringLiteral("qt.ui.mouseDown"),
         QJsonObject{{QStringLiteral("objectId"), idOf(m_pad)},
                     {QStringLiteral("position"),
                      QJsonObject{{QStringLiteral("x"), 20}, {QStringLiteral("y"), 20}}}});
  callOk(QStringLiteral("qt.ui.mouseMove"),
         QJsonObject{{QStringLiteral("delta"),
                      QJsonObject{{QStringLiteral("x"), 15}, {QStringLiteral("y"), 25}}}});
  const QJsonObject up = callOk(QStringLiteral("qt.ui.mouseUp"), QJsonObject{});

  QEXPECT_THAT(up, JsonField("position", AllOf(JsonField("x", 35), JsonField("y", 45))));
  QEXPECT_THAT(m_pad->releases(), ElementsAre(::testing::Field(&MouseRecord::pos, QPoint(35, 45))));
}

void TestHeldMouseGestures::uiMouseMoveWithoutAHeldButtonIsRejected() {
  QEXPECT_THAT(call(QStringLiteral("qt.ui.mouseMove"),
                    QJsonObject{{QStringLiteral("delta"),
                                 QJsonObject{{QStringLiteral("x"), 1}, {QStringLiteral("y"), 1}}}}),
               IsJsonRpcError(static_cast<int>(JsonRpcError::kInvalidParams)));
}

void TestHeldMouseGestures::uiMouseUpWithoutAHeldButtonIsRejected() {
  QEXPECT_THAT(call(QStringLiteral("qt.ui.mouseUp"), QJsonObject{}),
               IsJsonRpcError(static_cast<int>(JsonRpcError::kInvalidParams)));
}

void TestHeldMouseGestures::uiMouseDownWhileHeldIsRejected() {
  callOk(QStringLiteral("qt.ui.mouseDown"), QJsonObject{{QStringLiteral("objectId"), idOf(m_pad)}});

  QEXPECT_THAT(call(QStringLiteral("qt.ui.mouseDown"),
                    QJsonObject{{QStringLiteral("objectId"), idOf(m_neighbour)}}),
               IsJsonRpcError(static_cast<int>(JsonRpcError::kInvalidParams)));
  QEXPECT_THAT(m_neighbour->events, IsEmpty());
}

void TestHeldMouseGestures::uiMouseMoveRequiresADelta() {
  callOk(QStringLiteral("qt.ui.mouseDown"), QJsonObject{{QStringLiteral("objectId"), idOf(m_pad)}});

  QEXPECT_THAT(call(QStringLiteral("qt.ui.mouseMove"), QJsonObject{}),
               IsJsonRpcError(static_cast<int>(JsonRpcError::kInvalidParams)));
  QEXPECT_THAT(m_pad->moves(), IsEmpty());
}

void TestHeldMouseGestures::uiMouseDownRejectsAnItemOutsideTheViewport() {
  m_item->setPos(5000, 5000);
  pump();

  QEXPECT_THAT(call(QStringLiteral("qt.ui.mouseDown"),
                    QJsonObject{{QStringLiteral("objectId"), idOf(m_item)}}),
               IsJsonRpcError(Eq(ErrorCode::kCoordinateOutOfBounds)));
  // Nothing is left held by the failed press.
  QEXPECT_THAT(call(QStringLiteral("qt.ui.mouseUp"), QJsonObject{}),
               IsJsonRpcError(static_cast<int>(JsonRpcError::kInvalidParams)));
}

void TestHeldMouseGestures::uiMouseDownRejectsAMalformedPosition() {
  // As qt.ui.click does: a position that is not {x, y} is an error, not a press at the centre
  // from which every later delta would then be measured.
  QEXPECT_THAT(call(QStringLiteral("qt.ui.mouseDown"),
                    QJsonObject{{QStringLiteral("objectId"), idOf(m_pad)},
                                {QStringLiteral("position"), QJsonArray{10, 10}}}),
               IsJsonRpcError(static_cast<int>(JsonRpcError::kInvalidParams)));
  QEXPECT_THAT(m_pad->events, IsEmpty());
}

void TestHeldMouseGestures::uiReleasingTheHeldMouseReleasesItAtItsTarget() {
  // What the probe does when the client that pressed goes away mid-drag.
  callOk(QStringLiteral("qt.ui.mouseDown"), QJsonObject{{QStringLiteral("objectId"), idOf(m_pad)}});
  NativeModeApi::releaseHeldMouse();
  pump();

  QEXPECT_THAT(m_pad->releases(), ElementsAre(IsReleaseOf(Qt::LeftButton)));
  // The next client can start a gesture of its own.
  callOk(QStringLiteral("qt.ui.mouseDown"),
         QJsonObject{{QStringLiteral("objectId"), idOf(m_neighbour)}});
}

void TestHeldMouseGestures::uiHoldIsDroppedWhenItsTargetIsDestroyed() {
  auto doomed = std::make_unique<GestureRecorder>(m_window);
  doomed->setObjectName(QStringLiteral("doomed"));
  doomed->resize(50, 50);
  doomed->show();
  pump();
  ObjectRegistry::instance()->scanExistingObjects(doomed.get());
  callOk(QStringLiteral("qt.ui.mouseDown"),
         QJsonObject{{QStringLiteral("objectId"), idOf(doomed.get())}});

  doomed.reset();
  pump();

  // A new gesture can start: the dead press does not wedge the probe.
  callOk(QStringLiteral("qt.ui.mouseDown"), QJsonObject{{QStringLiteral("objectId"), idOf(m_pad)}});
  QEXPECT_THAT(m_pad->presses(), ::testing::SizeIs(1));
}

QTEST_MAIN(TestHeldMouseGestures)
#include "test_held_mouse_gestures.moc"
