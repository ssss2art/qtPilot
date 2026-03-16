// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "accessibility_tree_walker.h"

#include "../core/object_registry.h"
#include "role_mapper.h"

#include <QJsonArray>
#include <QMetaObject>

namespace qtPilot {

WalkResult AccessibilityTreeWalker::walk(QWidget* rootWidget, const WalkOptions& opts) {
  WalkResult result;

  if (!rootWidget)
    return result;

  // Ensure accessibility framework is active (critical on Linux/AT-SPI)
  QAccessible::setActive(true);

  QAccessibleInterface* rootIface = QAccessible::queryAccessibleInterface(rootWidget);
  if (!rootIface)
    return result;

  // If scopeRef is provided, we would need to resolve it against a
  // previously stored ref map. Since refs are ephemeral (rebuilt each walk),
  // scopeRef resolution is handled by the caller (ChromeModeApi) which
  // maintains the current ref map. For now, walk from root.
  // The caller can pass a scoped widget directly as rootWidget.

  int refCounter = 0;
  int charCount = 0;

  result.tree =
      walkNode(rootIface, 0, opts, refCounter, result.refMap, charCount, result.truncated);
  result.totalNodes = refCounter;  // Total refs assigned

  return result;
}

QJsonObject AccessibilityTreeWalker::walkNode(QAccessibleInterface* iface, int depth,
                                              const WalkOptions& opts, int& refCounter,
                                              QHash<QString, QAccessibleInterface*>& refMap,
                                              int& charCount, bool& truncated) {
  if (!iface || !iface->isValid() || depth > opts.maxDepth)
    return {};

  if (truncated)
    return {};

  QAccessible::Role role = iface->role();
  QAccessible::State state = iface->state();

  // Skip invisible elements in "interactive" mode
  // In "all" mode, include everything
  if (state.invisible && opts.filter != QStringLiteral("all"))
    return {};

  bool isInteractive = RoleMapper::isInteractive(role);

  QJsonObject node;

  // Assign ref: in "all" mode, all elements get refs;
  // in "interactive" mode, only interactive elements get refs
  bool assignRef = (opts.filter == QStringLiteral("all")) || isInteractive;
  if (assignRef) {
    ++refCounter;
    QString ref = QStringLiteral("ref_%1").arg(refCounter);
    node[QStringLiteral("ref")] = ref;
    refMap.insert(ref, iface);
    charCount += ref.size() + 8;  // "ref":"ref_N"
  }

  // Role
  QString roleName = RoleMapper::toChromeName(role);
  node[QStringLiteral("role")] = roleName;
  charCount += roleName.size() + 10;

  // Name with fallback chain:
  //   1. QAccessible::Name text
  //   2. QObject::objectName()
  //   3. QMetaObject::className()
  QString name = iface->text(QAccessible::Name);
  QObject* obj = iface->object();
  if (name.isEmpty() && obj) {
    name = obj->objectName();
    if (name.isEmpty())
      name = QString::fromUtf8(obj->metaObject()->className());
  }
  if (!name.isEmpty()) {
    node[QStringLiteral("name")] = name;
    charCount += name.size() + 10;
  }

  // States - only include truthy values
  QJsonObject states;
  if (state.focused)
    states[QStringLiteral("focused")] = true;
  if (state.disabled)
    states[QStringLiteral("disabled")] = true;
  if (state.checked)
    states[QStringLiteral("checked")] = true;
  if (state.expanded)
    states[QStringLiteral("expanded")] = true;
  if (state.collapsed)
    states[QStringLiteral("expanded")] = false;
  if (state.selected)
    states[QStringLiteral("selected")] = true;
  if (state.readOnly)
    states[QStringLiteral("readonly")] = true;
  if (state.pressed)
    states[QStringLiteral("pressed")] = true;
  if (state.hasPopup)
    states[QStringLiteral("hasPopup")] = true;
  if (state.modal)
    states[QStringLiteral("modal")] = true;
  if (state.editable)
    states[QStringLiteral("editable")] = true;
  if (state.multiLine)
    states[QStringLiteral("multiline")] = true;
  if (state.passwordEdit)
    states[QStringLiteral("password")] = true;
  // Note: "required" is not a standard QAccessible::State field in all Qt versions
  // Include it if available via the state struct

  if (!states.isEmpty()) {
    node[QStringLiteral("states")] = states;
    charCount += states.count() * 15;  // approximate
  }

  // Bounding rect
  QRect rect = iface->rect();
  if (rect.isValid()) {
    node[QStringLiteral("bounds")] = QJsonObject{{QStringLiteral("x"), rect.x()},
                                                 {QStringLiteral("y"), rect.y()},
                                                 {QStringLiteral("width"), rect.width()},
                                                 {QStringLiteral("height"), rect.height()}};
    charCount += 50;  // approximate
  }

  // Qt extras from the underlying QObject
  if (obj) {
    QString objName = obj->objectName();
    if (!objName.isEmpty()) {
      node[QStringLiteral("objectName")] = objName;
      charCount += objName.size() + 15;
    }

    QString className = QString::fromUtf8(obj->metaObject()->className());
    node[QStringLiteral("className")] = className;
    charCount += className.size() + 15;

    // Hierarchical ID from ObjectRegistry
    QString objectId = ObjectRegistry::instance()->objectId(obj);
    if (!objectId.isEmpty()) {
      node[QStringLiteral("objectId")] = objectId;
      charCount += objectId.size() + 15;
    }
  }

  // Check if we exceeded maxChars before recursing into children
  if (charCount > opts.maxChars) {
    truncated = true;
    return node;
  }

  // Recurse into children
  QJsonArray children;
  int childCount = iface->childCount();
  for (int i = 0; i < childCount; ++i) {
    if (truncated)
      break;

    QAccessibleInterface* child = iface->child(i);
    if (!child)
      continue;

    QJsonObject childNode =
        walkNode(child, depth + 1, opts, refCounter, refMap, charCount, truncated);
    if (!childNode.isEmpty())
      children.append(childNode);
  }

  if (!children.isEmpty())
    node[QStringLiteral("children")] = children;

  return node;
}

}  // namespace qtPilot
