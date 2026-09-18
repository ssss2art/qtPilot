// Copyright (c) 2024-2026 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "core/probe.h"  // For QTPILOT_EXPORT

#include <QJsonValue>
#include <QString>
#include <QStringList>

namespace qtPilot {

/// @brief Parses the `modifiers` parameter shared by the input-dispatching APIs.
///
/// Every method that synthesizes a mouse event accepts the same optional
/// `modifiers` parameter, so the spelling is defined once here rather than at
/// each call site.
///
/// Accepted shapes, all case-insensitive:
/// - absent, `null`, an empty string or an empty array -> Qt::NoModifier
/// - a single name: `"ctrl"`
/// - a `+`-joined string: `"ctrl+shift"`
/// - an array of names: `["ctrl", "shift"]`
///
/// @note The names map onto Qt's enum, not onto the keycaps. On macOS Qt
///       reports the Command key as Qt::ControlModifier and the Control key as
///       Qt::MetaModifier, so `"ctrl"` is Command there. That is what an app's
///       own `Qt::ControlModifier` checks compare against, which is what a
///       caller driving the app actually wants; `"meta"` reaches the physical
///       Control key. Pass `"cmd"` to say Command explicitly on either
///       platform.
class QTPILOT_EXPORT ModifierParser {
 public:
  /// @brief Convert a JSON `modifiers` value into Qt modifier flags.
  /// @param value The raw parameter value (may be undefined or null).
  /// @param methodName The JSON-RPC method, used in the error payload.
  /// @return The parsed flags; Qt::NoModifier when nothing was supplied.
  /// @throws JsonRpcException with kInvalidParams when @p value is not a
  ///         string or array of strings, or names a modifier that is not
  ///         recognised. The error data carries the offending value and the
  ///         list of accepted names.
  static Qt::KeyboardModifiers parse(const QJsonValue& value, const QString& methodName);

  /// @brief The modifier names this parser accepts, lowercase and sorted.
  /// @return The accepted spellings, for error payloads and documentation.
  static QStringList acceptedNames();
};

}  // namespace qtPilot
