// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <QAccessible>
#include <QString>

namespace qtPilot {

/// @brief Maps QAccessible::Role to Chrome/ARIA role name strings.
///
/// Purely static utility class. Provides Chrome-compatible role names
/// for Qt accessibility roles, and identifies which roles are interactive
/// (i.e., elements an agent can interact with).
///
/// The mapping covers ~55 Qt roles to their closest ARIA equivalents.
/// Unknown roles fall back to "generic".
class RoleMapper {
 public:
  /// @brief Convert a QAccessible::Role to its Chrome/ARIA role name.
  /// @param role The Qt accessibility role.
  /// @return The Chrome-compatible role name string (e.g., "button", "textbox").
  ///         Returns "generic" for unmapped roles.
  static QString toChromeName(QAccessible::Role role);

  /// @brief Check if a role represents an interactive element.
  /// @param role The Qt accessibility role.
  /// @return true if the role is interactive (buttons, inputs, links, etc.).
  static bool isInteractive(QAccessible::Role role);

 private:
  RoleMapper() = delete;  // Purely static, no instantiation
};

}  // namespace qtPilot
