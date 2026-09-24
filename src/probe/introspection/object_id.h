// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "core/probe.h"  // For QTPILOT_EXPORT

#include <expected>

#include <QJsonObject>
#include <QObject>
#include <QString>

namespace qtPilot {

/// @brief Hard ceiling on how deep any walk of the effective hierarchy may go.
///
/// The QObject parent axis is acyclic by construction, and
/// QQuickItem::setParentItem() rejects cycles within the VISUAL axis -- but
/// nothing checks the UNION of the two, which is what effectiveParent() and
/// effectiveChildren() traverse. Qt silently accepts an item whose QObject parent
/// is null, whose visual parent is B, where B is a QObject child of that item;
/// effectiveParent() then oscillates between the two forever. Real scenes do not
/// look like that, but a probe must never hang or overflow the stack of the
/// application it is inspecting, so every walk over this hierarchy is bounded.
///
/// Shared by object_id.cpp and ObjectRegistry so the bound cannot drift apart.
constexpr int kMaxEffectiveDepth = 512;

/// @brief Makes ID generation linear in group size for one traversal.
///
/// Generating a segment requires knowing whether it is unique among the object's
/// effective siblings, and answering that per object is a scan of the sibling list --
/// O(N) each, so O(N^2) to walk a group of N. That is the dominant cost of
/// qt.objects.tree and of the startup scan on a Repeater over many rows.
///
/// While a scope is alive, the answer is computed once per PARENT instead: one pass
/// over its effective children buckets them by segment and assigns every child its
/// suffix, so each lookup afterwards is a hash hit. The group becomes O(N) and a
/// tree walk linear in node count.
///
/// The results are identical to the unscoped path by construction -- same
/// enumeration order, same equality, and an object the cached pass did not see falls
/// back to the direct scan. It is purely a memo, never a different answer.
///
/// Scope it to a single synchronous traversal and nothing longer. The cache assumes
/// the tree does not change underneath it, which holds inside one walk on the thread
/// that owns the objects, and does not hold across client requests. Nesting is fine:
/// an inner scope shares the outer cache and only the outermost release clears it.
///
/// Thread-local, so a scope on one thread never affects another.
class QTPILOT_EXPORT IdGenerationScope {
 public:
  IdGenerationScope();
  ~IdGenerationScope();

  IdGenerationScope(const IdGenerationScope&) = delete;
  IdGenerationScope& operator=(const IdGenerationScope&) = delete;
  IdGenerationScope(IdGenerationScope&&) = delete;
  IdGenerationScope& operator=(IdGenerationScope&&) = delete;
};

/// @brief Record that a QObject is being destroyed on the calling thread.
///
/// Called from the registry's removal hook. ID generation reads the `text`
/// property of objects it walks, and that getter is application code: it can
/// destroy the object being identified, a sibling, or an ancestor. Generation
/// uses this to notice and stop before it touches a pointer that may be dead.
QTPILOT_EXPORT void noteObjectDestroyed();

/// @brief A control's label as it reads, without Qt's presentation markup.
///
/// Drops the mnemonic marker ("&Delete" reads "Delete"; "&&" is a literal "&")
/// and a menu entry's tab-separated shortcut hint ("Delete\tDel" reads
/// "Delete"). Both are how a label is drawn, not what it says, and the hint
/// changes whenever a user rebinds the key. IDs and label matching share this
/// one definition so they cannot disagree about what a label is. It applies to
/// the labels of actions and buttons; other text is content and is left alone.
/// Qt's CJK mnemonic form "(&F)" keeps its parentheses: "File(&F)" reads "File(F)".
QTPILOT_EXPORT QString normalizeLabel(const QString& label);

/// @brief Generate a hierarchical ID for a QObject.
///
/// ID format: "segment/segment/segment" where each segment is:
///   - objectName (if set and non-empty)
///   - text_<sanitized> (if the "text" property is non-empty; for an action or
///     a button, without its mnemonic marker or shortcut hint -- see normalizeLabel())
///   - ClassName or ClassName#N (for disambiguation among siblings)
///
/// The path is built from root to object, creating a unique hierarchical
/// address that can be used for lookup.
///
/// @param obj The object to generate an ID for.
/// @return The hierarchical ID string (e.g., "mainWindow/central/submitBtn").
/// @return The ID, or an empty string if an object was destroyed while it was
///         being built (see noteObjectDestroyed()).
QTPILOT_EXPORT QString generateObjectId(QObject* obj);

/// @brief The parent an ID path steps up to.
///
/// The QObject parent, falling back to the VISUAL parent for a QQuickItem that
/// has none. QML items created by a Repeater or ListView delegate are owned by
/// the engine rather than by the item above them, so their QObject parent is
/// null and a QObject-only walk cannot see them or anything beneath them.
///
/// @param obj The object to find the parent of.
/// @return The parent, or nullptr if this object is a root.
QTPILOT_EXPORT QObject* effectiveParent(QObject* obj);

/// @brief The children a tree walk descends into. The inverse of
/// effectiveParent(), so IDs built by walking up match traversals walking down.
///
/// Every object is listed exactly once: visual children that already have a
/// QObject parent are left to that parent rather than being listed twice.
///
/// @param obj The object to find the children of.
/// @return The children, in QObject order followed by parentless visual ones.
QTPILOT_EXPORT QList<QObject*> effectiveChildren(QObject* obj);

/// @brief Find an object by its hierarchical ID.
///
/// Traverses the object tree to find the object matching the given path.
/// Path segments are matched according to the same rules used by generateObjectId().
///
/// @param id The hierarchical ID (e.g., "mainWindow/central/submitBtn").
/// @param root Starting point for search (nullptr = search all top-level objects).
/// @return The object, or nullptr if not found.
QTPILOT_EXPORT QObject* findByObjectId(const QString& id, QObject* root = nullptr);

/// @brief Monadic lookup of an object by its hierarchical ID.
///
/// Traverses the object tree to find the object matching the given path.
///
/// @param id The hierarchical ID.
/// @param root Starting point for search (nullptr = search all top-level objects).
/// @return The object, or an error description.
QTPILOT_EXPORT std::expected<QObject*, QString> findByObjectIdExpected(const QString& id,
                                                                       QObject* root = nullptr);

/// @brief Serialize an object tree to JSON.
///
/// Creates a hierarchical JSON representation of the object tree.
/// Each node contains: id, className, objectName, and children[].
/// For QWidgets, also includes: visible, geometry.
///
/// @param root Root object to serialize (nullptr = all top-level objects).
/// @param maxDepth Maximum depth to recurse (-1 = unlimited).
/// @return JSON object representing the tree.
QTPILOT_EXPORT QJsonObject serializeObjectTree(QObject* root, int maxDepth = -1);

/// @brief Serialize a single object's basic info (no children).
///
/// Returns a flat JSON object with the object's properties but no recursive
/// children traversal.
///
/// @param obj The object to serialize.
/// @return JSON object with id, className, objectName, etc.
QTPILOT_EXPORT QJsonObject serializeObjectInfo(QObject* obj);

/// @brief Generate a single ID segment for an object.
///
/// This is the building block for full IDs. Returns the segment that
/// represents this object at its level in the hierarchy:
///   - objectName if set
///   - text_<sanitized> if text property exists
///   - ClassName#N with disambiguation suffix
///
/// @param obj The object to generate a segment for.
/// @return The ID segment string.
QTPILOT_EXPORT QString generateIdSegment(QObject* obj);

}  // namespace qtPilot
