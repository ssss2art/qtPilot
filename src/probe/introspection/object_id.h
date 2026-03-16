// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "core/probe.h"  // For QTPILOT_EXPORT

#include <QJsonObject>
#include <QObject>
#include <QString>

namespace qtPilot {

/// @brief Generate a hierarchical ID for a QObject.
///
/// ID format: "segment/segment/segment" where each segment is:
///   - objectName (if set and non-empty)
///   - text_<sanitized> (if "text" property exists and non-empty)
///   - ClassName or ClassName#N (for disambiguation among siblings)
///
/// The path is built from root to object, creating a unique hierarchical
/// address that can be used for lookup.
///
/// @param obj The object to generate an ID for.
/// @return The hierarchical ID string (e.g., "mainWindow/central/submitBtn").
QTPILOT_EXPORT QString generateObjectId(QObject* obj);

/// @brief Find an object by its hierarchical ID.
///
/// Traverses the object tree to find the object matching the given path.
/// Path segments are matched according to the same rules used by generateObjectId().
///
/// @param id The hierarchical ID (e.g., "mainWindow/central/submitBtn").
/// @param root Starting point for search (nullptr = search all top-level objects).
/// @return The object, or nullptr if not found.
QTPILOT_EXPORT QObject* findByObjectId(const QString& id, QObject* root = nullptr);

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
