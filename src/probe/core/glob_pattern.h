// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <optional>
#include <utility>
#include <vector>

#include <QString>
#include <QVector>

namespace qtPilot {

/// @brief A glob matched against a whole name, the same on every platform and Qt version.
///
/// `*` is any run of characters (`/` and `\` included), `?` any one character, `[...]` a set of
/// characters and `a-z` ranges, `[!...]` its complement, and everything else is literal; a `]`
/// first in a set is a member, so `[]]` matches `]` and `[*]` a literal `*`. Characters are whole
/// code points, so an emoji is one character.
///
/// Matched directly rather than through a regular expression: a translated regex split
/// surrogate pairs, read `[:a:]` as a POSIX class, and backtracked for a hundred milliseconds per
/// object on a hostile pattern. This takes at most name-length x pattern-length steps.
class GlobPattern {
 public:
  /// An inactive pattern, which matches every name.
  GlobPattern() = default;

  /// @brief Parse @p glob; nullopt when a set is left open or a range runs backwards.
  static std::optional<GlobPattern> parse(const QString& glob) {
    GlobPattern pattern;
    pattern.m_active = true;
    const auto cps = glob.toUcs4();
    const int size = static_cast<int>(cps.size());
    for (int i = 0; i < size; ++i) {
      const char32_t c = cps.at(i);
      if (c == U'*') {
        if (pattern.m_tokens.empty() || pattern.m_tokens.back().kind != Token::Star) {
          pattern.m_tokens.push_back({Token::Star, 0, false, {}});
        }
      } else if (c == U'?') {
        pattern.m_tokens.push_back({Token::Any, 0, false, {}});
      } else if (c == U'[') {
        Token set{Token::Set, 0, false, {}};
        int j = i + 1;
        if (j < size && cps.at(j) == U'!') {
          set.negated = true;
          ++j;
        }
        bool first = true;
        for (; j < size && (first || cps.at(j) != U']'); ++j) {
          first = false;
          const char32_t lo = cps.at(j);
          if (j + 2 < size && cps.at(j + 1) == U'-' && cps.at(j + 2) != U']') {
            const char32_t hi = cps.at(j + 2);
            if (hi < lo) {
              return std::nullopt;
            }
            set.ranges.emplace_back(lo, hi);
            j += 2;
          } else {
            set.ranges.emplace_back(lo, lo);
          }
        }
        if (j >= size) {
          return std::nullopt;  // the set was never closed
        }
        pattern.m_tokens.push_back(std::move(set));
        i = j;
      } else {
        pattern.m_tokens.push_back({Token::Literal, c, false, {}});
      }
    }
    return pattern;
  }

  /// Whether this filters at all; a default-constructed pattern does not.
  bool isActive() const { return m_active; }

  /// @brief Whether the whole of @p name matches; always true when inactive.
  bool matches(const QString& name) const {
    if (!m_active) {
      return true;
    }
    const auto cps = name.toUcs4();
    const size_t len = static_cast<size_t>(cps.size());
    const size_t count = m_tokens.size();
    constexpr size_t kNone = static_cast<size_t>(-1);
    size_t p = 0;
    size_t n = 0;
    size_t starP = kNone;
    size_t starN = 0;
    // Greedy with one restart point: on a mismatch, let the last `*` take one more character.
    while (n < len) {
      if (p < count && m_tokens[p].kind == Token::Star) {
        starP = p++;
        starN = n;
      } else if (p < count && m_tokens[p].matches(cps.at(static_cast<int>(n)))) {
        ++p;
        ++n;
      } else if (starP != kNone) {
        p = starP + 1;
        n = ++starN;
      } else {
        return false;
      }
    }
    while (p < count && m_tokens[p].kind == Token::Star) {
      ++p;
    }
    return p == count;
  }

 private:
  struct Token {
    enum Kind { Literal, Any, Star, Set } kind;
    char32_t ch;
    bool negated;
    std::vector<std::pair<char32_t, char32_t>> ranges;

    bool matches(char32_t c) const {
      switch (kind) {
        case Literal:
          return c == ch;
        case Any:
          return true;
        case Set: {
          bool in = false;
          for (const auto& [lo, hi] : ranges) {
            if (c >= lo && c <= hi) {
              in = true;
              break;
            }
          }
          return in != negated;
        }
        default:  // Star is matched by matches(), never one character at a time
          return false;
      }
    }
  };

  std::vector<Token> m_tokens;
  bool m_active = false;
};

}  // namespace qtPilot
