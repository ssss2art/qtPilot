// Copyright (c) 2024-2026 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <QByteArray>
#include <QColor>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMetaObject>
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QTest>
#include <QVariant>

#include <iostream>
#include <string>
#include <type_traits>

// ============================================================================
// GoogleTest Pretty-Printers for Qt Types
// ============================================================================

namespace qtPilot {
namespace test {

inline void PrintTo(const QString& s, std::ostream* os) {
  *os << "\"" << s.toStdString() << "\"";
}

inline void PrintTo(const QByteArray& b, std::ostream* os) {
  *os << "\"" << b.toStdString() << "\"";
}

inline void PrintTo(const QJsonObject& obj, std::ostream* os) {
  *os << QJsonDocument(obj).toJson(QJsonDocument::Indented).toStdString();
}

inline void PrintTo(const QJsonArray& arr, std::ostream* os) {
  *os << QJsonDocument(arr).toJson(QJsonDocument::Indented).toStdString();
}

inline void PrintTo(const QJsonValue& val, std::ostream* os) {
  if (val.isObject()) {
    PrintTo(val.toObject(), os);
  } else if (val.isArray()) {
    PrintTo(val.toArray(), os);
  } else if (val.isString()) {
    PrintTo(val.toString(), os);
  } else if (val.isDouble()) {
    *os << val.toDouble();
  } else if (val.isBool()) {
    *os << (val.toBool() ? "true" : "false");
  } else if (val.isNull()) {
    *os << "null";
  } else {
    *os << "<undefined>";
  }
}

inline void PrintTo(const QHostAddress& addr, std::ostream* os) {
  *os << addr.toString().toStdString();
}

inline void PrintTo(const QPoint& pt, std::ostream* os) {
  *os << "QPoint(" << pt.x() << ", " << pt.y() << ")";
}

inline void PrintTo(const QSize& sz, std::ostream* os) {
  *os << "QSize(" << sz.width() << ", " << sz.height() << ")";
}

inline void PrintTo(const QRect& r, std::ostream* os) {
  *os << "QRect(" << r.x() << ", " << r.y() << ", " << r.width() << ", " << r.height() << ")";
}

inline void PrintTo(const QColor& c, std::ostream* os) {
  *os << "QColor(" << c.red() << ", " << c.green() << ", " << c.blue() << ", " << c.alpha() << ")";
}

inline void PrintTo(const QVariant& v, std::ostream* os) {
  *os << "QVariant(" << (v.typeName() ? v.typeName() : "invalid") << ", "
      << v.toString().toStdString() << ")";
}

}  // namespace test
}  // namespace qtPilot

// Support ADL in global namespace
inline void PrintTo(const QString& s, std::ostream* os) {
  qtPilot::test::PrintTo(s, os);
}
inline void PrintTo(const QByteArray& b, std::ostream* os) {
  qtPilot::test::PrintTo(b, os);
}
inline void PrintTo(const QJsonObject& obj, std::ostream* os) {
  qtPilot::test::PrintTo(obj, os);
}
inline void PrintTo(const QJsonArray& arr, std::ostream* os) {
  qtPilot::test::PrintTo(arr, os);
}
inline void PrintTo(const QJsonValue& val, std::ostream* os) {
  qtPilot::test::PrintTo(val, os);
}
inline void PrintTo(const QHostAddress& addr, std::ostream* os) {
  qtPilot::test::PrintTo(addr, os);
}
inline void PrintTo(const QPoint& pt, std::ostream* os) {
  qtPilot::test::PrintTo(pt, os);
}
inline void PrintTo(const QSize& sz, std::ostream* os) {
  qtPilot::test::PrintTo(sz, os);
}
inline void PrintTo(const QRect& r, std::ostream* os) {
  qtPilot::test::PrintTo(r, os);
}
inline void PrintTo(const QColor& c, std::ostream* os) {
  qtPilot::test::PrintTo(c, os);
}
inline void PrintTo(const QVariant& v, std::ostream* os) {
  qtPilot::test::PrintTo(v, os);
}

// ============================================================================
// Comparison operators for QJsonValue with numeric types (enables Gt, Lt, etc.)
// ============================================================================

inline bool operator>(const QJsonValue& lhs, int rhs) { return lhs.isDouble() && lhs.toInt() > rhs; }
inline bool operator>=(const QJsonValue& lhs, int rhs) { return lhs.isDouble() && lhs.toInt() >= rhs; }
inline bool operator<(const QJsonValue& lhs, int rhs) { return lhs.isDouble() && lhs.toInt() < rhs; }
inline bool operator<=(const QJsonValue& lhs, int rhs) { return lhs.isDouble() && lhs.toInt() <= rhs; }

inline bool operator>(const QJsonValue& lhs, qint64 rhs) { return lhs.isDouble() && static_cast<qint64>(lhs.toDouble()) > rhs; }
inline bool operator>=(const QJsonValue& lhs, qint64 rhs) { return lhs.isDouble() && static_cast<qint64>(lhs.toDouble()) >= rhs; }
inline bool operator<(const QJsonValue& lhs, qint64 rhs) { return lhs.isDouble() && static_cast<qint64>(lhs.toDouble()) < rhs; }
inline bool operator<=(const QJsonValue& lhs, qint64 rhs) { return lhs.isDouble() && static_cast<qint64>(lhs.toDouble()) <= rhs; }

inline bool operator>(const QJsonValue& lhs, double rhs) { return lhs.isDouble() && lhs.toDouble() > rhs; }
inline bool operator>=(const QJsonValue& lhs, double rhs) { return lhs.isDouble() && lhs.toDouble() >= rhs; }
inline bool operator<(const QJsonValue& lhs, double rhs) { return lhs.isDouble() && lhs.toDouble() < rhs; }
inline bool operator<=(const QJsonValue& lhs, double rhs) { return lhs.isDouble() && lhs.toDouble() <= rhs; }

inline bool operator>(int lhs, const QJsonValue& rhs) { return rhs < lhs; }
inline bool operator>=(int lhs, const QJsonValue& rhs) { return rhs <= lhs; }
inline bool operator<(int lhs, const QJsonValue& rhs) { return rhs > lhs; }
inline bool operator<=(int lhs, const QJsonValue& rhs) { return rhs >= lhs; }

inline bool operator>(double lhs, const QJsonValue& rhs) { return rhs < lhs; }
inline bool operator>=(double lhs, const QJsonValue& rhs) { return rhs <= lhs; }
inline bool operator<(double lhs, const QJsonValue& rhs) { return rhs > lhs; }
inline bool operator<=(double lhs, const QJsonValue& rhs) { return rhs >= lhs; }

// ============================================================================
// Assertion Macros: QEXPECT_THAT / QCHECK_THAT
// ============================================================================

/// @brief Fatal assertion combining GMock matchers with QTest.
/// When the matcher fails, prints the formatted failure details and immediately fails the test slot.
#define QEXPECT_THAT(actual, matcher)                                                        \
  do {                                                                                      \
    const auto& _actual_val = (actual);                                                     \
    const auto& _m = (matcher);                                                             \
    ::testing::StringMatchResultListener _listener;                                         \
    if (!::testing::ExplainMatchResult(_m, _actual_val, &_listener)) {                      \
      std::string _expected = ::testing::DescribeMatcher<decltype(_actual_val)>(_m);       \
      std::string _actual_str = ::testing::PrintToString(_actual_val);                      \
      std::string _details = _listener.str();                                               \
      QString _fail_msg = QStringLiteral("Value of: %1\nExpected: %2\n  Actual: %3")        \
                              .arg(QStringLiteral(#actual),                                 \
                                   QString::fromStdString(_expected),                       \
                                   QString::fromStdString(_actual_str));                    \
      if (!_details.empty()) {                                                              \
        _fail_msg += QStringLiteral("\n Details: ") + QString::fromStdString(_details);     \
      }                                                                                     \
      QTest::qFail(qPrintable(_fail_msg), __FILE__, __LINE__);                               \
      return;                                                                               \
    }                                                                                       \
  } while (0)

/// @brief Non-fatal assertion: records verification failure in QTest but does not return immediately.
#define QCHECK_THAT(actual, matcher)                                                         \
  do {                                                                                      \
    const auto& _actual_val = (actual);                                                     \
    const auto& _m = (matcher);                                                             \
    ::testing::StringMatchResultListener _listener;                                         \
    if (!::testing::ExplainMatchResult(_m, _actual_val, &_listener)) {                      \
      std::string _expected = ::testing::DescribeMatcher<decltype(_actual_val)>(_m);       \
      std::string _actual_str = ::testing::PrintToString(_actual_val);                      \
      std::string _details = _listener.str();                                               \
      QString _fail_msg = QStringLiteral("Value of: %1\nExpected: %2\n  Actual: %3")        \
                              .arg(QStringLiteral(#actual),                                 \
                                   QString::fromStdString(_expected),                       \
                                   QString::fromStdString(_actual_str));                    \
      if (!_details.empty()) {                                                              \
        _fail_msg += QStringLiteral("\n Details: ") + QString::fromStdString(_details);     \
      }                                                                                     \
      QTest::qVerify(false, qPrintable(_fail_msg), "", __FILE__, __LINE__);                 \
    }                                                                                       \
  } while (0)

// ============================================================================
// Matchers: QString & QJsonValue strings
// ============================================================================

namespace qtPilot {
namespace test {
namespace internal {

inline bool toQString(const QString& s, QString& out) {
  out = s;
  return true;
}

inline bool toQString(const QJsonValue& v, QString& out) {
  if (!v.isString()) {
    return false;
  }
  out = v.toString();
  return true;
}

inline bool toQString(const std::string& s, QString& out) {
  out = QString::fromStdString(s);
  return true;
}

inline bool toQString(const char* s, QString& out) {
  if (!s) return false;
  out = QString::fromUtf8(s);
  return true;
}

}  // namespace internal
}  // namespace test
}  // namespace qtPilot

MATCHER_P(QStrEq, expected,
          std::string("is equal to \"") + QString(expected).toStdString() + "\"") {
  QString actual;
  if (!qtPilot::test::internal::toQString(arg, actual)) {
    *result_listener << "is not a string (value: " << ::testing::PrintToString(arg) << ")";
    return false;
  }
  return actual == QString(expected);
}

MATCHER_P(QStrContains, substr,
          std::string("contains \"") + QString(substr).toStdString() + "\"") {
  QString actual;
  if (!qtPilot::test::internal::toQString(arg, actual)) {
    *result_listener << "is not a string (value: " << ::testing::PrintToString(arg) << ")";
    return false;
  }
  return actual.contains(QString(substr));
}

MATCHER_P(QStrStartsWith, prefix,
          std::string("starts with \"") + QString(prefix).toStdString() + "\"") {
  QString actual;
  if (!qtPilot::test::internal::toQString(arg, actual)) {
    *result_listener << "is not a string (value: " << ::testing::PrintToString(arg) << ")";
    return false;
  }
  return actual.startsWith(QString(prefix));
}

MATCHER_P(QStrEndsWith, suffix,
          std::string("ends with \"") + QString(suffix).toStdString() + "\"") {
  QString actual;
  if (!qtPilot::test::internal::toQString(arg, actual)) {
    *result_listener << "is not a string (value: " << ::testing::PrintToString(arg) << ")";
    return false;
  }
  return actual.endsWith(QString(suffix));
}

MATCHER_P(QStrNe, expected,
          std::string("is not equal to \"") + QString(expected).toStdString() + "\"") {
  QString actual;
  if (!qtPilot::test::internal::toQString(arg, actual)) {
    *result_listener << "is not a string (value: " << ::testing::PrintToString(arg) << ")";
    return false;
  }
  return actual != QString(expected);
}

/// @brief Matches any Qt container, QJsonValue, or string that is empty.
MATCHER(QIsEmpty, "is empty") {
  if constexpr (std::is_same_v<std::decay_t<decltype(arg)>, QJsonValue>) {
    if (arg.isString()) return arg.toString().isEmpty();
    if (arg.isArray()) return arg.toArray().isEmpty();
    if (arg.isObject()) return arg.toObject().isEmpty();
    return arg.isNull() || arg.isUndefined();
  } else {
    return arg.isEmpty();
  }
}

/// @brief Matches any Qt container, QJsonValue, or string that is not empty.
MATCHER(QIsNotEmpty, "is not empty") {
  if constexpr (std::is_same_v<std::decay_t<decltype(arg)>, QJsonValue>) {
    if (arg.isString()) return !arg.toString().isEmpty();
    if (arg.isArray()) return !arg.toArray().isEmpty();
    if (arg.isObject()) return !arg.toObject().isEmpty();
    return !arg.isNull() && !arg.isUndefined();
  } else {
    return !arg.isEmpty();
  }
}

/// @brief Matches any boolean value, pointer, or boolean QJsonValue that is true.
MATCHER(QIsTrue, "is true") {
  if constexpr (std::is_same_v<std::decay_t<decltype(arg)>, QJsonValue>) {
    if (!arg.isBool()) {
      *result_listener << "is not a boolean (got: " << ::testing::PrintToString(arg) << ")";
      return false;
    }
    return arg.toBool();
  } else {
    return static_cast<bool>(arg);
  }
}

/// @brief Matches any boolean value, pointer, or boolean QJsonValue that is false.
MATCHER(QIsFalse, "is false") {
  if constexpr (std::is_same_v<std::decay_t<decltype(arg)>, QJsonValue>) {
    if (!arg.isBool()) {
      *result_listener << "is not a boolean (got: " << ::testing::PrintToString(arg) << ")";
      return false;
    }
    return !arg.toBool();
  } else {
    return !static_cast<bool>(arg);
  }
}

// ============================================================================
// Matchers: QJsonObject / QJsonValue
// ============================================================================

namespace qtPilot {
namespace test {
namespace internal {

inline bool extractJsonObject(const QJsonObject& obj, QJsonObject& out,
                             ::testing::MatchResultListener*) {
  out = obj;
  return true;
}

inline bool extractJsonObject(const QJsonValue& val, QJsonObject& out,
                             ::testing::MatchResultListener* listener) {
  if (!val.isObject()) {
    *listener << "is not a JSON object";
    return false;
  }
  out = val.toObject();
  return true;
}

inline bool extractJsonArray(const QJsonArray& arr, QJsonArray& out,
                            ::testing::MatchResultListener*) {
  out = arr;
  return true;
}

inline bool extractJsonArray(const QJsonValue& val, QJsonArray& out,
                            ::testing::MatchResultListener* listener) {
  if (!val.isArray()) {
    *listener << "is not a JSON array";
    return false;
  }
  out = val.toArray();
  return true;
}

template <typename M>
bool matchJsonValue(const QJsonValue& val, const M& m,
                    ::testing::MatchResultListener* listener) {
  if constexpr (std::is_same_v<std::decay_t<M>, const char*> ||
                std::is_same_v<std::decay_t<M>, char*> ||
                std::is_same_v<std::decay_t<M>, std::string> ||
                std::is_same_v<std::decay_t<M>, QString>) {
    QString expectedStr = QString(m);
    if (!val.isString()) {
      *listener << "field is not a string (was " << ::testing::PrintToString(val) << ")";
      return false;
    }
    if (val.toString() != expectedStr) {
      *listener << "expected \"" << expectedStr.toStdString() << "\", got "
                << ::testing::PrintToString(val);
      return false;
    }
    return true;
  } else if constexpr (std::is_integral_v<std::decay_t<M>> && !std::is_same_v<std::decay_t<M>, bool>) {
    int expectedInt = static_cast<int>(m);
    if (!val.isDouble() || val.toInt() != expectedInt) {
      *listener << "expected integer " << expectedInt << ", got "
                << ::testing::PrintToString(val);
      return false;
    }
    return true;
  } else if constexpr (std::is_floating_point_v<std::decay_t<M>>) {
    double expectedDouble = static_cast<double>(m);
    if (!val.isDouble() || val.toDouble() != expectedDouble) {
      *listener << "expected double " << expectedDouble << ", got "
                << ::testing::PrintToString(val);
      return false;
    }
    return true;
  } else if constexpr (std::is_same_v<std::decay_t<M>, bool>) {
    if (!val.isBool() || val.toBool() != m) {
      *listener << "expected bool " << (m ? "true" : "false") << ", got "
                << ::testing::PrintToString(val);
      return false;
    }
    return true;
  } else {
    return ::testing::ExplainMatchResult(m, val, listener);
  }
}

}  // namespace internal
}  // namespace test
}  // namespace qtPilot

/// @brief Matches a QJsonObject or QJsonValue (object) having the specified key.
MATCHER_P(HasJsonField, key,
          std::string("has field '") + QString(key).toStdString() + "'") {
  QJsonObject obj;
  if (!qtPilot::test::internal::extractJsonObject(arg, obj, result_listener)) {
    return false;
  }
  QString qKey = QString(key);
  if (!obj.contains(qKey)) {
    *result_listener << "missing field '" << qKey.toStdString() << "'";
    return false;
  }
  return true;
}

/// @brief Matches a QJsonObject or QJsonValue (object) not having the specified key.
MATCHER_P(DoesNotHaveJsonField, key,
          std::string("does not have field '") + QString(key).toStdString() + "'") {
  QJsonObject obj;
  if (!qtPilot::test::internal::extractJsonObject(arg, obj, result_listener)) {
    return false;
  }
  QString qKey = QString(key);
  if (obj.contains(qKey)) {
    *result_listener << "unexpectedly contains field '" << qKey.toStdString() << "'";
    return false;
  }
  return true;
}

/// @brief Matches a field value in a QJsonObject or QJsonValue with another matcher or primitive value.
template <typename KeyType, typename ValueMatcher>
class HasJsonFieldValueMatcher {
 public:
  HasJsonFieldValueMatcher(KeyType key, ValueMatcher vm)
      : key_(QString(key)), valueMatcher_(std::move(vm)) {}

  template <typename JsonContainer>
  bool MatchAndExplain(const JsonContainer& arg,
                       ::testing::MatchResultListener* listener) const {
    QJsonObject obj;
    if (!qtPilot::test::internal::extractJsonObject(arg, obj, listener)) {
      return false;
    }
    if (!obj.contains(key_)) {
      *listener << "missing field '" << key_.toStdString() << "'";
      return false;
    }

    QJsonValue val = obj.value(key_);
    return qtPilot::test::internal::matchJsonValue(val, valueMatcher_, listener);
  }

  void DescribeTo(std::ostream* os) const {
    *os << "has field '" << key_.toStdString() << "'";
  }

  void DescribeNegationTo(std::ostream* os) const {
    *os << "does not have field '" << key_.toStdString() << "'";
  }

  template <typename T>
  operator ::testing::Matcher<T>() const {
    return ::testing::MakePolymorphicMatcher(*this);
  }

 private:
  QString key_;
  ValueMatcher valueMatcher_;
};

/// @brief Factory function for HasJsonField with a value matcher or literal.
template <typename KeyType, typename ValueMatcher>
inline auto HasJsonField(KeyType&& key, ValueMatcher&& valMatcher) {
  return ::testing::MakePolymorphicMatcher(
      HasJsonFieldValueMatcher<std::decay_t<KeyType>, std::decay_t<ValueMatcher>>(
          std::forward<KeyType>(key), std::forward<ValueMatcher>(valMatcher)));
}

/// @brief Alias for HasJsonField(key, val)
template <typename KeyType, typename ValueMatcher>
inline auto JsonField(KeyType&& key, ValueMatcher&& valMatcher) {
  return HasJsonField(std::forward<KeyType>(key), std::forward<ValueMatcher>(valMatcher));
}

// ============================================================================
// Matchers: QJsonArray
// ============================================================================

/// @brief Matches a QJsonArray or QJsonValue (array) containing at least one element matching elemMatcher.
template <typename ElemMatcher>
class JsonArrayContainsMatcher {
 public:
  explicit JsonArrayContainsMatcher(ElemMatcher matcher)
      : elemMatcher_(std::move(matcher)) {}

  template <typename JsonContainer>
  bool MatchAndExplain(const JsonContainer& arg,
                       ::testing::MatchResultListener* listener) const {
    QJsonArray arr;
    if (!qtPilot::test::internal::extractJsonArray(arg, arr, listener)) {
      return false;
    }

    for (int i = 0; i < arr.size(); ++i) {
      ::testing::StringMatchResultListener inner;
      if (qtPilot::test::internal::matchJsonValue(arr[i], elemMatcher_, &inner)) {
        return true;
      }
    }

    *listener << "no element matched in array of size " << arr.size() << ": "
              << ::testing::PrintToString(arr);
    return false;
  }

  void DescribeTo(std::ostream* os) const {
    *os << "array contains element";
  }

  void DescribeNegationTo(std::ostream* os) const {
    *os << "array does not contain element";
  }

  template <typename T>
  operator ::testing::Matcher<T>() const {
    return ::testing::MakePolymorphicMatcher(*this);
  }

 private:
  ElemMatcher elemMatcher_;
};

template <typename ElemMatcher>
inline auto JsonArrayContains(ElemMatcher&& matcher) {
  return ::testing::MakePolymorphicMatcher(
      JsonArrayContainsMatcher<std::decay_t<ElemMatcher>>(
          std::forward<ElemMatcher>(matcher)));
}

/// @brief Matches the size of a QJsonArray or QJsonValue (array).
MATCHER_P(JsonArraySize, sizeMatcher,
          std::string("array size matches ") + ::testing::DescribeMatcher<int>(sizeMatcher)) {
  QJsonArray arr;
  if (!qtPilot::test::internal::extractJsonArray(arg, arr, result_listener)) {
    return false;
  }
  return ::testing::ExplainMatchResult(sizeMatcher, arr.size(), result_listener);
}

// ============================================================================
// Domain Matchers: JSON-RPC 2.0 Protocol
// ============================================================================

namespace qtPilot {
namespace test {

template <typename ResultMatcher>
class IsJsonRpcSuccessMatcher {
 public:
  explicit IsJsonRpcSuccessMatcher(ResultMatcher rm) : resultMatcher_(std::move(rm)) {}

  template <typename JsonContainer>
  bool MatchAndExplain(const JsonContainer& arg,
                       ::testing::MatchResultListener* listener) const {
    QJsonObject obj;
    if (!qtPilot::test::internal::extractJsonObject(arg, obj, listener)) {
      return false;
    }
    if (obj.value(QStringLiteral("jsonrpc")).toString() != QStringLiteral("2.0")) {
      *listener << "jsonrpc is not '2.0' (got: "
                << ::testing::PrintToString(obj.value(QStringLiteral("jsonrpc"))) << ")";
      return false;
    }
    if (obj.contains(QStringLiteral("error"))) {
      *listener << "response contains error: "
                << ::testing::PrintToString(obj.value(QStringLiteral("error")));
      return false;
    }
    if (!obj.contains(QStringLiteral("result"))) {
      *listener << "response missing 'result' field";
      return false;
    }
    QJsonValue resultVal = obj.value(QStringLiteral("result"));
    ::testing::StringMatchResultListener innerListener;
    bool matched = qtPilot::test::internal::matchJsonValue(resultVal, resultMatcher_, &innerListener);
    if (!matched) {
      std::string innerDetails = innerListener.str();
      *listener << "result does not match";
      if (!innerDetails.empty()) {
        *listener << " (" << innerDetails << ")";
      }
      return false;
    }
    return true;
  }

  void DescribeTo(std::ostream* os) const {
    *os << "is a JSON-RPC 2.0 success response with matching result";
  }

  void DescribeNegationTo(std::ostream* os) const {
    *os << "is not a JSON-RPC 2.0 success response with matching result";
  }

  template <typename T>
  operator ::testing::Matcher<T>() const {
    return ::testing::MakePolymorphicMatcher(*this);
  }

 private:
  ResultMatcher resultMatcher_;
};

class IsJsonRpcSuccessAnyMatcher {
 public:
  template <typename JsonContainer>
  bool MatchAndExplain(const JsonContainer& arg,
                       ::testing::MatchResultListener* listener) const {
    QJsonObject obj;
    if (!qtPilot::test::internal::extractJsonObject(arg, obj, listener)) {
      return false;
    }
    if (obj.value(QStringLiteral("jsonrpc")).toString() != QStringLiteral("2.0")) {
      *listener << "jsonrpc is not '2.0' (got: "
                << ::testing::PrintToString(obj.value(QStringLiteral("jsonrpc"))) << ")";
      return false;
    }
    if (obj.contains(QStringLiteral("error"))) {
      *listener << "response contains error: "
                << ::testing::PrintToString(obj.value(QStringLiteral("error")));
      return false;
    }
    if (!obj.contains(QStringLiteral("result"))) {
      *listener << "response missing 'result' field";
      return false;
    }
    return true;
  }

  void DescribeTo(std::ostream* os) const {
    *os << "is a JSON-RPC 2.0 success response";
  }

  void DescribeNegationTo(std::ostream* os) const {
    *os << "is not a JSON-RPC 2.0 success response";
  }

  template <typename T>
  operator ::testing::Matcher<T>() const {
    return ::testing::MakePolymorphicMatcher(*this);
  }
};

inline auto IsJsonRpcSuccess() {
  return ::testing::MakePolymorphicMatcher(IsJsonRpcSuccessAnyMatcher{});
}

template <typename ResultMatcher>
inline auto IsJsonRpcSuccess(ResultMatcher&& rm) {
  return ::testing::MakePolymorphicMatcher(
      IsJsonRpcSuccessMatcher<std::decay_t<ResultMatcher>>(
          std::forward<ResultMatcher>(rm)));
}

template <typename CodeMatcher, typename MessageMatcher>
class IsJsonRpcErrorMatcher {
 public:
  IsJsonRpcErrorMatcher(CodeMatcher cm, MessageMatcher mm)
      : codeMatcher_(std::move(cm)), messageMatcher_(std::move(mm)) {}

  template <typename JsonContainer>
  bool MatchAndExplain(const JsonContainer& arg,
                       ::testing::MatchResultListener* listener) const {
    QJsonObject obj;
    if (!qtPilot::test::internal::extractJsonObject(arg, obj, listener)) {
      return false;
    }
    if (!obj.contains(QStringLiteral("error"))) {
      *listener << "response has no 'error' field (was: "
                << ::testing::PrintToString(obj) << ")";
      return false;
    }
    QJsonObject errObj = obj.value(QStringLiteral("error")).toObject();
    if (errObj.isEmpty() && !obj.value(QStringLiteral("error")).isObject()) {
      *listener << "error field is not an object";
      return false;
    }
    QJsonValue codeVal = errObj.value(QStringLiteral("code"));
    if (!qtPilot::test::internal::matchJsonValue(codeVal, codeMatcher_, listener)) {
      *listener << " (in error.code)";
      return false;
    }
    QJsonValue msgVal = errObj.value(QStringLiteral("message"));
    if (!qtPilot::test::internal::matchJsonValue(msgVal, messageMatcher_, listener)) {
      *listener << " (in error.message)";
      return false;
    }
    return true;
  }

  void DescribeTo(std::ostream* os) const {
    *os << "is a JSON-RPC 2.0 error response with matching code and message";
  }

  void DescribeNegationTo(std::ostream* os) const {
    *os << "is not a matching JSON-RPC 2.0 error response";
  }

  template <typename T>
  operator ::testing::Matcher<T>() const {
    return ::testing::MakePolymorphicMatcher(*this);
  }

 private:
  CodeMatcher codeMatcher_;
  MessageMatcher messageMatcher_;
};

template <typename CodeMatcher, typename MessageMatcher>
inline auto IsJsonRpcError(CodeMatcher&& cm, MessageMatcher&& mm) {
  return ::testing::MakePolymorphicMatcher(
      IsJsonRpcErrorMatcher<std::decay_t<CodeMatcher>, std::decay_t<MessageMatcher>>(
          std::forward<CodeMatcher>(cm), std::forward<MessageMatcher>(mm)));
}

template <typename CodeMatcher>
inline auto IsJsonRpcError(CodeMatcher&& cm) {
  return IsJsonRpcError(std::forward<CodeMatcher>(cm), ::testing::_);
}

inline auto IsJsonRpcError() {
  return IsJsonRpcError(::testing::_, ::testing::_);
}

template <typename IdMatcher>
inline auto HasJsonRpcId(IdMatcher&& idm) {
  return HasJsonField(QStringLiteral("id"), std::forward<IdMatcher>(idm));
}

template <typename MethodMatcher, typename ParamsMatcher>
class IsJsonRpcNotificationMatcher {
 public:
  IsJsonRpcNotificationMatcher(MethodMatcher mm, ParamsMatcher pm)
      : methodMatcher_(std::move(mm)), paramsMatcher_(std::move(pm)) {}

  template <typename JsonContainer>
  bool MatchAndExplain(const JsonContainer& arg,
                       ::testing::MatchResultListener* listener) const {
    QJsonObject obj;
    if (!qtPilot::test::internal::extractJsonObject(arg, obj, listener)) {
      return false;
    }
    if (obj.value(QStringLiteral("jsonrpc")).toString() != QStringLiteral("2.0")) {
      *listener << "jsonrpc is not '2.0'";
      return false;
    }
    if (obj.contains(QStringLiteral("id"))) {
      *listener << "notification must not contain 'id'";
      return false;
    }
    if (!obj.contains(QStringLiteral("method"))) {
      *listener << "missing 'method' field";
      return false;
    }
    QJsonValue methodVal = obj.value(QStringLiteral("method"));
    if (!qtPilot::test::internal::matchJsonValue(methodVal, methodMatcher_, listener)) {
      *listener << " (in notification method)";
      return false;
    }
    if (obj.contains(QStringLiteral("params"))) {
      QJsonValue paramsVal = obj.value(QStringLiteral("params"));
      if (!qtPilot::test::internal::matchJsonValue(paramsVal, paramsMatcher_, listener)) {
        *listener << " (in notification params)";
        return false;
      }
    }
    return true;
  }

  void DescribeTo(std::ostream* os) const {
    *os << "is a JSON-RPC 2.0 notification";
  }

  void DescribeNegationTo(std::ostream* os) const {
    *os << "is not a JSON-RPC 2.0 notification";
  }

  template <typename T>
  operator ::testing::Matcher<T>() const {
    return ::testing::MakePolymorphicMatcher(*this);
  }

 private:
  MethodMatcher methodMatcher_;
  ParamsMatcher paramsMatcher_;
};

template <typename MethodMatcher, typename ParamsMatcher>
inline auto IsJsonRpcNotification(MethodMatcher&& mm, ParamsMatcher&& pm) {
  return ::testing::MakePolymorphicMatcher(
      IsJsonRpcNotificationMatcher<std::decay_t<MethodMatcher>, std::decay_t<ParamsMatcher>>(
          std::forward<MethodMatcher>(mm), std::forward<ParamsMatcher>(pm)));
}

template <typename MethodMatcher>
inline auto IsJsonRpcNotification(MethodMatcher&& mm) {
  return IsJsonRpcNotification(std::forward<MethodMatcher>(mm), ::testing::_);
}

// ============================================================================
// Domain Matchers: Qt Geometry (QPoint, QSize, QRect) & JSON coordinates
// ============================================================================

class PointEqMatcher {
 public:
  PointEqMatcher(int x, int y) : x_(x), y_(y) {}
  explicit PointEqMatcher(const QPoint& pt) : x_(pt.x()), y_(pt.y()) {}

  bool MatchAndExplain(const QPoint& pt, ::testing::MatchResultListener* listener) const {
    if (pt.x() != x_ || pt.y() != y_) {
      *listener << "has coordinates (" << pt.x() << ", " << pt.y() << ")";
      return false;
    }
    return true;
  }

  template <typename JsonContainer>
  bool MatchAndExplain(const JsonContainer& arg,
                       ::testing::MatchResultListener* listener) const {
    QJsonObject obj;
    if (!qtPilot::test::internal::extractJsonObject(arg, obj, listener)) {
      return false;
    }
    int actualX = obj.value(QStringLiteral("x")).toInt();
    int actualY = obj.value(QStringLiteral("y")).toInt();
    if (actualX != x_ || actualY != y_) {
      *listener << "has coordinates (x: " << actualX << ", y: " << actualY << ")";
      return false;
    }
    return true;
  }

  void DescribeTo(std::ostream* os) const {
    *os << "has point coordinates (" << x_ << ", " << y_ << ")";
  }

  void DescribeNegationTo(std::ostream* os) const {
    *os << "does not have point coordinates (" << x_ << ", " << y_ << ")";
  }

  template <typename T>
  operator ::testing::Matcher<T>() const {
    return ::testing::MakePolymorphicMatcher(*this);
  }

 private:
  int x_;
  int y_;
};

inline auto PointEq(int x, int y) {
  return ::testing::MakePolymorphicMatcher(PointEqMatcher(x, y));
}
inline auto PointEq(const QPoint& pt) {
  return ::testing::MakePolymorphicMatcher(PointEqMatcher(pt));
}

class SizeEqMatcher {
 public:
  SizeEqMatcher(int w, int h) : width_(w), height_(h) {}
  explicit SizeEqMatcher(const QSize& sz) : width_(sz.width()), height_(sz.height()) {}

  bool MatchAndExplain(const QSize& sz, ::testing::MatchResultListener* listener) const {
    if (sz.width() != width_ || sz.height() != height_) {
      *listener << "has size (" << sz.width() << ", " << sz.height() << ")";
      return false;
    }
    return true;
  }

  template <typename JsonContainer>
  bool MatchAndExplain(const JsonContainer& arg,
                       ::testing::MatchResultListener* listener) const {
    QJsonObject obj;
    if (!qtPilot::test::internal::extractJsonObject(arg, obj, listener)) {
      return false;
    }
    int actualW = obj.value(QStringLiteral("width")).toInt();
    int actualH = obj.value(QStringLiteral("height")).toInt();
    if (actualW != width_ || actualH != height_) {
      *listener << "has size (width: " << actualW << ", height: " << actualH << ")";
      return false;
    }
    return true;
  }

  void DescribeTo(std::ostream* os) const {
    *os << "has size (" << width_ << ", " << height_ << ")";
  }

  void DescribeNegationTo(std::ostream* os) const {
    *os << "does not have size (" << width_ << ", " << height_ << ")";
  }

  template <typename T>
  operator ::testing::Matcher<T>() const {
    return ::testing::MakePolymorphicMatcher(*this);
  }

 private:
  int width_;
  int height_;
};

inline auto SizeEq(int w, int h) {
  return ::testing::MakePolymorphicMatcher(SizeEqMatcher(w, h));
}
inline auto SizeEq(const QSize& sz) {
  return ::testing::MakePolymorphicMatcher(SizeEqMatcher(sz));
}

class RectEqMatcher {
 public:
  RectEqMatcher(int x, int y, int w, int h) : x_(x), y_(y), width_(w), height_(h) {}
  explicit RectEqMatcher(const QRect& r)
      : x_(r.x()), y_(r.y()), width_(r.width()), height_(r.height()) {}

  bool MatchAndExplain(const QRect& r, ::testing::MatchResultListener* listener) const {
    if (r.x() != x_ || r.y() != y_ || r.width() != width_ || r.height() != height_) {
      *listener << "has rect (x: " << r.x() << ", y: " << r.y()
                << ", width: " << r.width() << ", height: " << r.height() << ")";
      return false;
    }
    return true;
  }

  template <typename JsonContainer>
  bool MatchAndExplain(const JsonContainer& arg,
                       ::testing::MatchResultListener* listener) const {
    QJsonObject obj;
    if (!qtPilot::test::internal::extractJsonObject(arg, obj, listener)) {
      return false;
    }
    int actualX = obj.value(QStringLiteral("x")).toInt();
    int actualY = obj.value(QStringLiteral("y")).toInt();
    int actualW = obj.value(QStringLiteral("width")).toInt();
    int actualH = obj.value(QStringLiteral("height")).toInt();
    if (actualX != x_ || actualY != y_ || actualW != width_ || actualH != height_) {
      *listener << "has rect (x: " << actualX << ", y: " << actualY
                << ", width: " << actualW << ", height: " << actualH << ")";
      return false;
    }
    return true;
  }

  void DescribeTo(std::ostream* os) const {
    *os << "has rect (x: " << x_ << ", y: " << y_
        << ", width: " << width_ << ", height: " << height_ << ")";
  }

  void DescribeNegationTo(std::ostream* os) const {
    *os << "does not have matching rect";
  }

  template <typename T>
  operator ::testing::Matcher<T>() const {
    return ::testing::MakePolymorphicMatcher(*this);
  }

 private:
  int x_;
  int y_;
  int width_;
  int height_;
};

inline auto RectEq(int x, int y, int w, int h) {
  return ::testing::MakePolymorphicMatcher(RectEqMatcher(x, y, w, h));
}
inline auto RectEq(const QRect& r) {
  return ::testing::MakePolymorphicMatcher(RectEqMatcher(r));
}

class RectContainsPointMatcher {
 public:
  explicit RectContainsPointMatcher(const QPoint& pt) : pt_(pt) {}
  RectContainsPointMatcher(int x, int y) : pt_(x, y) {}

  bool MatchAndExplain(const QRect& r, ::testing::MatchResultListener* listener) const {
    if (!r.contains(pt_)) {
      *listener << "rect " << ::testing::PrintToString(r) << " does not contain point ("
                << pt_.x() << ", " << pt_.y() << ")";
      return false;
    }
    return true;
  }

  void DescribeTo(std::ostream* os) const {
    *os << "contains point (" << pt_.x() << ", " << pt_.y() << ")";
  }

  void DescribeNegationTo(std::ostream* os) const {
    *os << "does not contain point (" << pt_.x() << ", " << pt_.y() << ")";
  }

  template <typename T>
  operator ::testing::Matcher<T>() const {
    return ::testing::MakePolymorphicMatcher(*this);
  }

 private:
  QPoint pt_;
};

inline auto RectContains(const QPoint& pt) {
  return ::testing::MakePolymorphicMatcher(RectContainsPointMatcher(pt));
}
inline auto RectContains(int x, int y) {
  return ::testing::MakePolymorphicMatcher(RectContainsPointMatcher(x, y));
}

// ============================================================================
// Domain Matchers: QObject Introspection
// ============================================================================

template <typename NameMatcher>
class HasObjectNameMatcher {
 public:
  explicit HasObjectNameMatcher(NameMatcher nm) : nameMatcher_(std::move(nm)) {}

  template <typename QObjPtr>
  bool MatchAndExplain(QObjPtr* obj, ::testing::MatchResultListener* listener) const {
    if (obj == nullptr) {
      *listener << "object is null";
      return false;
    }
    QString actualName = obj->objectName();
    ::testing::StringMatchResultListener innerListener;
    bool matched = qtPilot::test::internal::matchJsonValue(actualName, nameMatcher_, &innerListener);
    if (!matched) {
      *listener << "objectName is \"" << actualName.toStdString() << "\"";
      std::string details = innerListener.str();
      if (!details.empty()) {
        *listener << " (" << details << ")";
      }
      return false;
    }
    return true;
  }

  void DescribeTo(std::ostream* os) const {
    *os << "has matching objectName";
  }

  void DescribeNegationTo(std::ostream* os) const {
    *os << "does not have matching objectName";
  }

  template <typename T>
  operator ::testing::Matcher<T>() const {
    return ::testing::MakePolymorphicMatcher(*this);
  }

 private:
  NameMatcher nameMatcher_;
};

template <typename NameMatcher>
inline auto HasObjectName(NameMatcher&& nm) {
  return ::testing::MakePolymorphicMatcher(
      HasObjectNameMatcher<std::decay_t<NameMatcher>>(std::forward<NameMatcher>(nm)));
}

template <typename ClassMatcher>
class HasClassNameMatcher {
 public:
  explicit HasClassNameMatcher(ClassMatcher cm) : classMatcher_(std::move(cm)) {}

  template <typename QObjPtr>
  bool MatchAndExplain(QObjPtr* obj, ::testing::MatchResultListener* listener) const {
    if (obj == nullptr) {
      *listener << "object is null";
      return false;
    }
    QString actualClass = QString::fromLatin1(obj->metaObject()->className());
    ::testing::StringMatchResultListener innerListener;
    bool matched = qtPilot::test::internal::matchJsonValue(actualClass, classMatcher_, &innerListener);
    if (!matched) {
      *listener << "className is \"" << actualClass.toStdString() << "\"";
      std::string details = innerListener.str();
      if (!details.empty()) {
        *listener << " (" << details << ")";
      }
      return false;
    }
    return true;
  }

  void DescribeTo(std::ostream* os) const {
    *os << "has matching className";
  }

  void DescribeNegationTo(std::ostream* os) const {
    *os << "does not have matching className";
  }

  template <typename T>
  operator ::testing::Matcher<T>() const {
    return ::testing::MakePolymorphicMatcher(*this);
  }

 private:
  ClassMatcher classMatcher_;
};

template <typename ClassMatcher>
inline auto HasClassName(ClassMatcher&& cm) {
  return ::testing::MakePolymorphicMatcher(
      HasClassNameMatcher<std::decay_t<ClassMatcher>>(std::forward<ClassMatcher>(cm)));
}

template <typename NameType, typename ValueMatcher>
class HasPropertyMatcher {
 public:
  HasPropertyMatcher(NameType name, ValueMatcher vm)
      : name_(QString(name)), valueMatcher_(std::move(vm)) {}

  template <typename QObjPtr>
  bool MatchAndExplain(QObjPtr* obj, ::testing::MatchResultListener* listener) const {
    if (obj == nullptr) {
      *listener << "object is null";
      return false;
    }
    QVariant prop = obj->property(qPrintable(name_));
    if (!prop.isValid()) {
      *listener << "property '" << name_.toStdString() << "' is not valid or does not exist";
      return false;
    }
    QJsonValue propVal = QJsonValue::fromVariant(prop);
    ::testing::StringMatchResultListener innerListener;
    bool matched = qtPilot::test::internal::matchJsonValue(propVal, valueMatcher_, &innerListener);
    if (!matched) {
      *listener << "property '" << name_.toStdString() << "' has value "
                << ::testing::PrintToString(prop);
      std::string details = innerListener.str();
      if (!details.empty()) {
        *listener << " (" << details << ")";
      }
      return false;
    }
    return true;
  }

  template <typename JsonContainer>
  bool MatchAndExplain(const JsonContainer& arg,
                       ::testing::MatchResultListener* listener) const {
    QJsonObject obj;
    if (!qtPilot::test::internal::extractJsonObject(arg, obj, listener)) {
      return false;
    }
    QJsonObject props;
    if (obj.contains(QStringLiteral("properties"))) {
      props = obj.value(QStringLiteral("properties")).toObject();
    } else {
      props = obj;
    }
    if (!props.contains(name_)) {
      *listener << "missing property '" << name_.toStdString() << "'";
      return false;
    }
    QJsonValue val = props.value(name_);
    return qtPilot::test::internal::matchJsonValue(val, valueMatcher_, listener);
  }

  void DescribeTo(std::ostream* os) const {
    *os << "has property '" << name_.toStdString() << "'";
  }

  void DescribeNegationTo(std::ostream* os) const {
    *os << "does not have property '" << name_.toStdString() << "'";
  }

  template <typename T>
  operator ::testing::Matcher<T>() const {
    return ::testing::MakePolymorphicMatcher(*this);
  }

 private:
  QString name_;
  ValueMatcher valueMatcher_;
};

template <typename NameType, typename ValueMatcher>
inline auto HasProperty(NameType&& name, ValueMatcher&& vm) {
  return ::testing::MakePolymorphicMatcher(
      HasPropertyMatcher<std::decay_t<NameType>, std::decay_t<ValueMatcher>>(
          std::forward<NameType>(name), std::forward<ValueMatcher>(vm)));
}

// ============================================================================
// Domain Matchers: Payloads & Binary (PNG, Base64)
// ============================================================================

class IsValidPngMatcher {
 public:
  bool MatchAndExplain(const QByteArray& bytes,
                       ::testing::MatchResultListener* listener) const {
    static const char kPngMagic[] = "\x89PNG\r\n\x1a\n";
    if (bytes.size() < 8) {
      *listener << "byte array is only " << bytes.size() << " bytes (too short for PNG)";
      return false;
    }
    if (memcmp(bytes.constData(), kPngMagic, 8) != 0) {
      *listener << "header does not match PNG signature (starts with: "
                << bytes.left(8).toHex().toStdString() << ")";
      return false;
    }
    return true;
  }

  bool MatchAndExplain(const QString& str,
                       ::testing::MatchResultListener* listener) const {
    QByteArray decoded = QByteArray::fromBase64(str.toLatin1());
    if (decoded.isEmpty()) {
      *listener << "string could not be decoded as base64";
      return false;
    }
    return MatchAndExplain(decoded, listener);
  }

  template <typename JsonContainer>
  bool MatchAndExplain(const JsonContainer& arg,
                       ::testing::MatchResultListener* listener) const {
    QJsonObject obj;
    if (qtPilot::test::internal::extractJsonObject(arg, obj, listener)) {
      if (obj.contains(QStringLiteral("data"))) {
        return MatchAndExplain(obj.value(QStringLiteral("data")).toString(), listener);
      }
      if (obj.contains(QStringLiteral("result"))) {
        QJsonValue res = obj.value(QStringLiteral("result"));
        if (res.isObject() && res.toObject().contains(QStringLiteral("data"))) {
          return MatchAndExplain(res.toObject().value(QStringLiteral("data")).toString(), listener);
        }
      }
    }
    if constexpr (std::is_same_v<std::decay_t<JsonContainer>, QJsonValue>) {
      if (arg.isString()) {
        return MatchAndExplain(arg.toString(), listener);
      }
    }
    *listener << "value is not a valid PNG container";
    return false;
  }

  void DescribeTo(std::ostream* os) const {
    *os << "is valid PNG data or base64 PNG string";
  }

  void DescribeNegationTo(std::ostream* os) const {
    *os << "is not valid PNG data";
  }

  template <typename T>
  operator ::testing::Matcher<T>() const {
    return ::testing::MakePolymorphicMatcher(*this);
  }
};

inline auto IsValidPng() {
  return ::testing::MakePolymorphicMatcher(IsValidPngMatcher{});
}

class IsValidBase64Matcher {
 public:
  bool MatchAndExplain(const QString& str,
                       ::testing::MatchResultListener* listener) const {
    if (str.isEmpty()) {
      *listener << "string is empty";
      return false;
    }
    QByteArray decoded = QByteArray::fromBase64(str.toLatin1());
    if (decoded.isEmpty()) {
      *listener << "string could not be decoded as base64";
      return false;
    }
    return true;
  }

  bool MatchAndExplain(const QByteArray& bytes,
                       ::testing::MatchResultListener* listener) const {
    if (bytes.isEmpty()) {
      *listener << "byte array is empty";
      return false;
    }
    QByteArray decoded = QByteArray::fromBase64(bytes);
    if (decoded.isEmpty()) {
      *listener << "byte array could not be decoded as base64";
      return false;
    }
    return true;
  }

  void DescribeTo(std::ostream* os) const {
    *os << "is non-empty valid base64 data";
  }

  void DescribeNegationTo(std::ostream* os) const {
    *os << "is not valid base64 data";
  }

  template <typename T>
  operator ::testing::Matcher<T>() const {
    return ::testing::MakePolymorphicMatcher(*this);
  }
};

inline auto IsValidBase64() {
  return ::testing::MakePolymorphicMatcher(IsValidBase64Matcher{});
}

}  // namespace test
}  // namespace qtPilot

// ============================================================================
// Common Usings for Test Cleanliness
// ============================================================================

namespace qtPilot {
namespace test {

using ::testing::AllOf;
using ::testing::AnyOf;
using ::testing::Contains;
using ::testing::Each;
using ::testing::ElementsAre;
using ::testing::EndsWith;
using ::testing::Eq;
using ::testing::Ge;
using ::testing::Gt;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsNull;
using ::testing::IsTrue;
using ::testing::Le;
using ::testing::Lt;
using ::testing::Ne;
using ::testing::Not;
using ::testing::NotNull;
using ::testing::Pointee;
using ::testing::Property;
using ::testing::SizeIs;
using ::testing::StartsWith;
using ::testing::StrEq;
using ::testing::UnorderedElementsAre;

}  // namespace test
}  // namespace qtPilot
