// Copyright (c) 2024-2026 qtPilot Contributors
// SPDX-License-Identifier: MIT
//
// Complexity benchmarks for graphics-item targeting.
//
// The question these answer: does resolving where to click an item stay cheap
// as the scene grows? Targeting verifies that the point it picked actually
// reaches the item, which costs a scene hit-test, and when the preferred point
// does not reach it a lattice is sampled. So the cost of a call is
// (points tried) x (cost of one hit-test), and both factors matter:
//
//   - a hit somewhere reasonable is one hit-test, whatever the scene size
//   - a miss is bounded by candidatePointCount(), currently 82
//
// The scan is bounded and constant, so the shape against scene size should be
// whatever Qt's own hit-test is (BSP-indexed, sub-linear), not something that
// grows with the number of points tried. If a change makes the fallback scale
// with N differently from the direct hit, that is the regression to catch.

#include "interaction/item_targeting.h"

#include <benchmark/benchmark.h>
#include <memory>
#include <vector>

#include <QApplication>
#include <QGraphicsItem>
#include <QGraphicsObject>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QJsonObject>
#include <QPainter>
#include <QPainterPath>

using qtPilot::ItemTargeting;

namespace {

/// A plain filled item.
class FilledItem : public QGraphicsObject {
 public:
  explicit FilledItem(const QRectF& rect) : m_rect(rect) {}
  QRectF boundingRect() const override { return m_rect; }
  void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override {
    painter->fillRect(m_rect, Qt::blue);
  }

 private:
  QRectF m_rect;
};

/// An item whose interior is click-through, so its centre never reaches it and
/// the fallback lattice always runs.
class HollowItem : public QGraphicsObject {
 public:
  HollowItem(const QRectF& rect, qreal band) : m_rect(rect), m_band(band) {}
  QRectF boundingRect() const override { return m_rect; }
  void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override {
    painter->drawRect(m_rect);
  }
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

/// A scene holding @p filler background items plus whatever the case adds.
struct Fixture {
  std::unique_ptr<QGraphicsScene> scene;
  std::unique_ptr<QGraphicsView> view;

  explicit Fixture(int filler) {
    scene = std::make_unique<QGraphicsScene>();
    scene->setSceneRect(0, 0, 4000, 4000);
    // Spread filler items across the scene so the BSP index has real work,
    // while leaving the target's own area clear.
    for (int i = 0; i < filler; ++i) {
      auto* item = new FilledItem(QRectF(0, 0, 10, 10));
      item->setPos(200 + (i % 300) * 12, 200 + (i / 300) * 12);
      scene->addItem(item);
    }
    view = std::make_unique<QGraphicsView>(scene.get());
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->setFrameStyle(0);
    view->resize(1200, 900);
    view->show();
  }
};

/// The behaviour this replaced: map the centre into the viewport and dispatch
/// there, trusting that whatever is at that point is the item asked for. Kept as
/// the baseline so the cost of actually verifying stays a measured number rather
/// than an assertion.
void BM_MapCentreOnly_NoVerification(benchmark::State& state) {
  const int filler = static_cast<int>(state.range(0));
  Fixture fixture(filler);
  auto* target = new FilledItem(QRectF(0, 0, 80, 60));
  target->setPos(20, 20);
  fixture.scene->addItem(target);
  fixture.view->centerOn(target);
  QCoreApplication::processEvents();

  for (auto _ : state) {
    const QPoint point =
        fixture.view->mapFromScene(target->mapToScene(target->boundingRect().center()));
    benchmark::DoNotOptimize(point);
  }
  state.SetComplexityN(filler);
}
BENCHMARK(BM_MapCentreOnly_NoVerification)->RangeMultiplier(2)->Range(8, 2048)->Complexity();

/// The common case: nothing over the item, centre reaches it -- one hit-test.
void BM_ResolveTarget_CentreHits(benchmark::State& state) {
  const int filler = static_cast<int>(state.range(0));
  Fixture fixture(filler);
  auto* target = new FilledItem(QRectF(0, 0, 80, 60));
  target->setPos(20, 20);
  fixture.scene->addItem(target);
  // The view centres on a large scene by default, which would leave the target
  // off-screen and turn every call into an out-of-bounds refusal.
  fixture.view->centerOn(target);
  QCoreApplication::processEvents();

  const QJsonObject params;
  for (auto _ : state) {
    auto result =
        ItemTargeting::resolve(target, fixture.view.get(), params, QStringLiteral("bench"));
    benchmark::DoNotOptimize(result);
  }
  state.SetComplexityN(filler);
}
BENCHMARK(BM_ResolveTarget_CentreHits)->RangeMultiplier(2)->Range(8, 2048)->Complexity();

/// The worst realistic case: the centre does not reach the item, so the lattice
/// is scanned until a point does. Bounded by candidatePointCount().
void BM_ResolveTarget_FallbackScan(benchmark::State& state) {
  const int filler = static_cast<int>(state.range(0));
  Fixture fixture(filler);
  auto* target = new HollowItem(QRectF(0, 0, 160, 120), 10);
  target->setPos(20, 20);
  fixture.scene->addItem(target);
  // The view centres on a large scene by default, which would leave the target
  // off-screen and turn every call into an out-of-bounds refusal.
  fixture.view->centerOn(target);
  QCoreApplication::processEvents();

  const QJsonObject params;
  for (auto _ : state) {
    auto result =
        ItemTargeting::resolve(target, fixture.view.get(), params, QStringLiteral("bench"));
    benchmark::DoNotOptimize(result);
  }
  state.SetComplexityN(filler);
}
BENCHMARK(BM_ResolveTarget_FallbackScan)->RangeMultiplier(2)->Range(8, 2048)->Complexity();

/// The refusal path: every candidate is covered, so all of them are tried and
/// the call throws. This is the ceiling on what a single call can cost.
void BM_ResolveTarget_FullyOccluded(benchmark::State& state) {
  const int filler = static_cast<int>(state.range(0));
  Fixture fixture(filler);
  auto* target = new FilledItem(QRectF(0, 0, 80, 60));
  target->setPos(20, 20);
  fixture.scene->addItem(target);

  auto* lid = new FilledItem(QRectF(0, 0, 120, 100));
  lid->setPos(10, 10);
  lid->setZValue(100);
  fixture.scene->addItem(lid);
  fixture.view->centerOn(target);
  QCoreApplication::processEvents();

  const QJsonObject params;
  for (auto _ : state) {
    try {
      auto result =
          ItemTargeting::resolve(target, fixture.view.get(), params, QStringLiteral("bench"));
      benchmark::DoNotOptimize(result);
    } catch (const std::exception& ex) {
      benchmark::DoNotOptimize(ex.what());
    }
  }
  state.SetComplexityN(filler);
}
BENCHMARK(BM_ResolveTarget_FullyOccluded)->RangeMultiplier(2)->Range(8, 2048)->Complexity();

}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
