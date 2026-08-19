// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT
//
// Complexity benchmarks for object-ID generation and tree traversal.
//
// The point of these is NOT absolute timings -- those depend on the machine and are
// not worth defending. The point is the EXPONENT. Every benchmark here declares
// SetComplexityN() and Complexity(), so google/benchmark fits the measured curve
// against N and prints the inferred Big-O plus an RMS goodness-of-fit. That turns
// "did this change make a hot path quadratic?" into a number you can read off a
// diff instead of a judgement call.
//
// Why this file exists: a sibling-disambiguation optimization was once added to
// getSiblingIndex() to escape exactly this quadratic, and it bought the speed by
// emitting duplicate IDs. Correctness won, the quadratic came back, and the
// measurement went into a commit message where nobody would look at it again. That
// is the gap this closes.
//
// Run:
//   cmake -B build -DQTPILOT_BUILD_BENCHMARKS=ON
//   cmake --build build --target qtPilot_bench_object_id
//   ./build/bin/qtPilot_bench_object_id
//
// Read the `_BigO` and `_RMS` rows, not the per-N times. An RMS above roughly 10%
// means the fit is poor and the label should not be trusted -- rerun on a quiet
// machine before concluding anything.

#include "core/object_registry.h"
#include "introspection/object_id.h"

#include <benchmark/benchmark.h>
#include <memory>

#include <QByteArray>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QUrl>

using namespace qtPilot;

namespace {

/// @brief A container holding `count` sibling delegates, built once per N.
///
/// Repeater rather than ListView on purpose: a Repeater instantiates every row
/// eagerly, which is the shape that actually exercises a large sibling group. A
/// recycling ListView only ever creates its visible delegates, so it would measure
/// a constant no matter what N says.
struct DelegateScene {
  QQmlEngine engine;
  QQmlComponent component;
  std::unique_ptr<QObject> root;
  QQuickItem* container = nullptr;

  DelegateScene(int count, const char* delegateBody) : component(&engine) {
    const QByteArray qml = QByteArray(
                               "import QtQuick 2.0\n"
                               "Item {\n"
                               "  objectName: \"root\"\n"
                               "  Item {\n"
                               "    objectName: \"strip\"\n"
                               "    Repeater { model: ") +
                           QByteArray::number(count) + "; delegate: " + delegateBody +
                           " }\n"
                           "  }\n"
                           "}\n";
    component.setData(qml, QUrl());
    root.reset(component.create());
    if (root) {
      auto* rootItem = qobject_cast<QQuickItem*>(root.get());
      container = rootItem ? rootItem->childItems().value(0) : nullptr;
    }
  }

  bool valid() const { return root != nullptr && container != nullptr; }

  /// Delegates only -- the Repeater itself is also a child of the container.
  QList<QObject*> delegates() const {
    QList<QObject*> result;
    const QList<QObject*> children = effectiveChildren(container);
    for (QObject* child : children) {
      if (child && child->parent() == nullptr) {
        result.append(child);
      }
    }
    return result;
  }
};

// Unnamed delegates: the segment falls through to the type name, so every sibling
// collides and disambiguation has to do its full work. This is the worst realistic
// case, and the one to watch.
constexpr const char* kUnnamedDelegate = "Rectangle { width: 4; height: 4 }";

}  // namespace

/// @brief One generateObjectId() call, with N siblings present.
///
/// The per-call cost. Expected O(N): one pass over the sibling list.
static void BM_GenerateOneId_WithNSiblings(benchmark::State& state) {
  const int n = static_cast<int>(state.range(0));
  DelegateScene scene(n, kUnnamedDelegate);
  if (!scene.valid()) {
    state.SkipWithError("QML scene failed to build");
    return;
  }
  const QList<QObject*> delegates = scene.delegates();
  if (delegates.isEmpty()) {
    state.SkipWithError("no delegates instantiated");
    return;
  }
  QObject* target = delegates.at(delegates.size() / 2);

  for (auto _ : state) {
    benchmark::DoNotOptimize(generateObjectId(target));
  }
  state.SetComplexityN(n);
}
BENCHMARK(BM_GenerateOneId_WithNSiblings)->RangeMultiplier(2)->Range(8, 2048)->Complexity();

/// @brief generateObjectId() for EVERY sibling in the group.
///
/// What a full tree walk costs, and the number that matters: this is the shape
/// ObjectRegistry::scanExistingObjects() hits at probe startup, on the host
/// application's main thread. Expected O(N^2) today. If a future change collapses
/// the per-object sibling scan into one grouping pass per parent, this row should
/// drop to O(N) -- that is the whole point of measuring it.
static void BM_GenerateAllIds_ForGroup(benchmark::State& state) {
  const int n = static_cast<int>(state.range(0));
  DelegateScene scene(n, kUnnamedDelegate);
  if (!scene.valid()) {
    state.SkipWithError("QML scene failed to build");
    return;
  }
  const QList<QObject*> delegates = scene.delegates();
  if (delegates.isEmpty()) {
    state.SkipWithError("no delegates instantiated");
    return;
  }

  for (auto _ : state) {
    for (QObject* delegate : delegates) {
      benchmark::DoNotOptimize(generateObjectId(delegate));
    }
  }
  state.SetComplexityN(n);
}
BENCHMARK(BM_GenerateAllIds_ForGroup)->RangeMultiplier(2)->Range(8, 1024)->Complexity();

/// @brief serializeObjectTree() over a scene of N delegates.
///
/// The client-facing cost of qt.objects.tree. Inherits whatever complexity ID
/// generation has, so it tracks the row above; measured separately because it also
/// carries the per-node QJsonObject construction, which is the part a reader would
/// otherwise blame.
static void BM_SerializeTree_NDelegates(benchmark::State& state) {
  const int n = static_cast<int>(state.range(0));
  DelegateScene scene(n, kUnnamedDelegate);
  if (!scene.valid()) {
    state.SkipWithError("QML scene failed to build");
    return;
  }

  for (auto _ : state) {
    benchmark::DoNotOptimize(serializeObjectTree(scene.root.get(), -1));
  }
  state.SetComplexityN(n);
}
BENCHMARK(BM_SerializeTree_NDelegates)->RangeMultiplier(2)->Range(8, 512)->Complexity();

/// @brief findByObjectId() resolving the LAST delegate in a group of N.
///
/// The resolution side of the same contract. Worst case on purpose: findBySegments
/// scans candidates in order, so the last sibling walks the whole list.
///
/// Measures O(N^2), not the O(N) a single scan would suggest -- and that is the
/// value of measuring rather than reasoning. matchesSegment() compares against a
/// FRESHLY GENERATED segment, so resolving one path scans N candidates and pays an
/// O(N) sibling scan inside each. Every id-addressed operation on a large list
/// (qt_ui_click, qt_properties_get) carries this.
static void BM_ResolveId_LastOfNSiblings(benchmark::State& state) {
  const int n = static_cast<int>(state.range(0));
  DelegateScene scene(n, kUnnamedDelegate);
  if (!scene.valid()) {
    state.SkipWithError("QML scene failed to build");
    return;
  }
  const QList<QObject*> delegates = scene.delegates();
  if (delegates.isEmpty()) {
    state.SkipWithError("no delegates instantiated");
    return;
  }
  const QString id = generateObjectId(delegates.last());

  for (auto _ : state) {
    benchmark::DoNotOptimize(findByObjectId(id, scene.root.get()));
  }
  state.SetComplexityN(n);
}
BENCHMARK(BM_ResolveId_LastOfNSiblings)->RangeMultiplier(2)->Range(8, 512)->Complexity();

int main(int argc, char** argv) {
  // Headless, and with the probe's own auto-start suppressed: benchmarking the
  // traversal, not the WebSocket server.
  qputenv("QT_QPA_PLATFORM", "minimal");
  qputenv("QTPILOT_ENABLED", "0");
  QGuiApplication app(argc, argv);

  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
