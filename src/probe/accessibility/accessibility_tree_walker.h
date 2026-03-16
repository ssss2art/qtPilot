// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <QAccessible>
#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QWidget>

namespace qtPilot {

/// @brief Options controlling accessibility tree traversal.
struct WalkOptions {
  QString filter = QStringLiteral("all");  ///< "interactive" or "all"
  int maxDepth = 15;                       ///< Maximum recursion depth
  int maxChars = 50000;                    ///< Approximate max JSON output size
  QString scopeRef;                        ///< Ref to scope subtree (empty = whole window)
};

/// @brief Result of an accessibility tree walk.
struct WalkResult {
  QJsonObject tree;                              ///< The accessibility tree as JSON
  QHash<QString, QAccessibleInterface*> refMap;  ///< ref_N -> interface mapping
  int totalNodes = 0;                            ///< Total nodes visited
  bool truncated = false;                        ///< True if maxChars was exceeded
};

/// @brief Recursively walks the QAccessible tree producing JSON with ref identifiers.
///
/// Static utility class. Traverses the accessibility interface tree starting
/// from a QWidget, building a JSON representation with Chrome-compatible
/// role names, states, bounds, and Qt extras (objectName, className, objectId).
///
/// Interactive elements receive ref_N identifiers for subsequent interaction.
/// In "interactive" filter mode, only interactive elements get refs but
/// structural parents are included for context.
///
/// Usage:
///   WalkOptions opts;
///   opts.filter = "interactive";
///   WalkResult result = AccessibilityTreeWalker::walk(myWidget, opts);
///   // result.tree contains the JSON tree
///   // result.refMap maps ref strings to QAccessibleInterface*
class AccessibilityTreeWalker {
 public:
  /// @brief Walk the accessibility tree starting from a widget.
  /// @param rootWidget The widget to start traversal from.
  /// @param opts Options controlling traversal behavior.
  /// @return WalkResult with JSON tree and ref map.
  static WalkResult walk(QWidget* rootWidget, const WalkOptions& opts = WalkOptions());

 private:
  AccessibilityTreeWalker() = delete;  // Purely static

  /// @brief Recursively walk a single node and its children.
  /// @param iface The accessibility interface to walk.
  /// @param depth Current recursion depth.
  /// @param opts Walk options.
  /// @param refCounter Running counter for ref assignment (modified in place).
  /// @param refMap Map being built from ref strings to interfaces.
  /// @param charCount Running approximate character count (modified in place).
  /// @param truncated Set to true if charCount exceeds maxChars.
  /// @return JSON object for this node and its subtree.
  static QJsonObject walkNode(QAccessibleInterface* iface, int depth, const WalkOptions& opts,
                              int& refCounter, QHash<QString, QAccessibleInterface*>& refMap,
                              int& charCount, bool& truncated);
};

}  // namespace qtPilot
