// Copyright (c) 2026 qtPilot Contributors
// SPDX-License-Identifier: MIT
//
// GMock matchers over screenshot payloads, for tests that need to know what a
// capture *shows* rather than only that it is a PNG.
//
// Kept apart from qt_matchers.h because decoding needs QtGui (QImage), and
// several tests that include qt_matchers.h link QtCore alone.
//
// Usage:
// @code
//   QEXPECT_THAT(result, DecodesToImage(AllOf(ImageSizeIs(QSize(60, 40)),
//                                             PixelAt(QPoint(30, 20), ColorNear(Qt::red)))));
// @endcode

#pragma once

#include "common/qt_matchers.h"

#include <cstdlib>
#include <ostream>

#include <QBuffer>
#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QJsonValue>
#include <QPoint>
#include <QSize>
#include <QString>

/// @brief Prints a QImage by its size and format -- never its pixels.
///
/// In the global namespace, QImage's own, so argument-dependent lookup finds it.
inline void PrintTo(const QImage& image, std::ostream* os) {
  *os << "QImage(" << image.width() << "x" << image.height() << ", format "
      << static_cast<int>(image.format()) << ")";
}

namespace qtPilot {
namespace test {

namespace internal {

/// @brief Decode a base64 PNG string into an image.
/// @return A null image if @p base64 is not base64-encoded image data.
inline QImage decodeBase64Image(const QString& base64) {
  return QImage::fromData(QByteArray::fromBase64(base64.toLatin1()), "PNG");
}

/// @brief Find the base64 payload in whatever shape a screenshot came back as.
///
/// Accepts the bare base64 string, a JSON string value, or a result object
/// carrying it under `image` (qt.ui.screenshot, qtpilot.screenshot) or `data`.
inline bool extractImagePayload(const QJsonValue& value, QString& out,
                                ::testing::MatchResultListener* listener) {
  if (value.isString()) {
    out = value.toString();
    return true;
  }
  if (value.isObject()) {
    const QJsonObject obj = value.toObject();
    for (const QString& key : {QStringLiteral("image"), QStringLiteral("data")}) {
      if (obj.value(key).isString()) {
        out = obj.value(key).toString();
        return true;
      }
    }
    *listener << "object has no 'image' or 'data' string field (was: "
              << ::testing::PrintToString(obj) << ")";
    return false;
  }
  *listener << "value is neither a base64 string nor a screenshot result object";
  return false;
}

inline bool extractImagePayload(const QJsonObject& obj, QString& out,
                                ::testing::MatchResultListener* listener) {
  return extractImagePayload(QJsonValue(obj), out, listener);
}

inline bool extractImagePayload(const QString& str, QString& out,
                                ::testing::MatchResultListener* /*listener*/) {
  out = str;
  return true;
}

}  // namespace internal

/// @brief Matches a screenshot payload that decodes to a QImage satisfying an inner matcher.
template <typename ImageMatcher>
class DecodesToImageMatcher {
 public:
  explicit DecodesToImageMatcher(ImageMatcher inner) : inner_(std::move(inner)) {}

  template <typename Payload>
  bool MatchAndExplain(const Payload& arg, ::testing::MatchResultListener* listener) const {
    QString base64;
    if (!internal::extractImagePayload(arg, base64, listener)) {
      return false;
    }
    const QImage image = internal::decodeBase64Image(base64);
    if (image.isNull()) {
      *listener << "payload does not decode to a PNG image";
      return false;
    }
    const ::testing::Matcher<const QImage&> matcher(inner_);
    ::testing::StringMatchResultListener inner;
    const bool ok = matcher.MatchAndExplain(image, &inner);
    *listener << "decodes to " << ::testing::PrintToString(image);
    if (!inner.str().empty()) {
      *listener << ", " << inner.str();
    }
    return ok;
  }

  void DescribeTo(std::ostream* os) const {
    *os << "decodes to an image that ";
    ::testing::Matcher<const QImage&>(inner_).DescribeTo(os);
  }

  void DescribeNegationTo(std::ostream* os) const {
    *os << "does not decode to an image that ";
    ::testing::Matcher<const QImage&>(inner_).DescribeTo(os);
  }

 private:
  ImageMatcher inner_;
};

/// @brief `DecodesToImage(m)` -- the base64 PNG payload decodes to an image matching `m`.
template <typename ImageMatcher>
inline auto DecodesToImage(ImageMatcher&& inner) {
  return ::testing::MakePolymorphicMatcher(
      DecodesToImageMatcher<std::decay_t<ImageMatcher>>(std::forward<ImageMatcher>(inner)));
}

/// @brief The image's size in pixels equals @p expected.
MATCHER_P(ImageSizeIs, expected,
          std::string(negation ? "is not " : "is ") + std::to_string(QSize(expected).width()) +
              "x" + std::to_string(QSize(expected).height()) + " pixels") {
  const QSize want(expected);
  if (arg.size() != want) {
    *result_listener << "is " << arg.width() << "x" << arg.height() << " pixels";
    return false;
  }
  return true;
}

/// @brief A colour within @p tolerance of @p expected on every RGB channel.
///
/// Alpha is ignored: a capture's alpha depends on the backend's surface format,
/// not on what was drawn.
MATCHER_P2(ColorNear, expected, tolerance,
           std::string(negation ? "is not " : "is ") + "within " + std::to_string(tolerance) +
               " of " + QColor(expected).name().toStdString()) {
  const QColor want(expected);
  const QColor got(arg);
  const bool near = std::abs(got.red() - want.red()) <= tolerance &&
                    std::abs(got.green() - want.green()) <= tolerance &&
                    std::abs(got.blue() - want.blue()) <= tolerance;
  if (!near) {
    *result_listener << "is " << got.name().toStdString();
  }
  return near;
}

/// @brief `ColorNear(c)` with a tolerance absorbing backend colour-space rounding.
inline auto ColorNear(const QColor& expected) {
  return ColorNear(expected, 8);
}

/// @brief The pixel at @p point (image coordinates) satisfies @p colorMatcher.
MATCHER_P2(PixelAt, point, colorMatcher,
           std::string("has a pixel at (") + std::to_string(QPoint(point).x()) + ", " +
               std::to_string(QPoint(point).y()) + ") that " +
               ::testing::DescribeMatcher<QColor>(colorMatcher, negation)) {
  const QPoint at(point);
  if (!QRect(QPoint(0, 0), arg.size()).contains(at)) {
    *result_listener << "(" << at.x() << ", " << at.y() << ") lies outside the " << arg.width()
                     << "x" << arg.height() << " image";
    return false;
  }
  const QColor pixel = arg.pixelColor(at);
  ::testing::StringMatchResultListener inner;
  const bool ok = ::testing::Matcher<QColor>(colorMatcher).MatchAndExplain(pixel, &inner);
  *result_listener << "pixel (" << at.x() << ", " << at.y() << ") " << inner.str();
  return ok;
}

/// @brief Every pixel of the image satisfies @p colorMatcher.
///
/// For asserting a capture is cropped to exactly one solid item: a crop that
/// is off by even a pixel row picks up the background along that edge.
MATCHER_P(AllPixels, colorMatcher,
          std::string("has every pixel that ") +
              ::testing::DescribeMatcher<QColor>(colorMatcher, negation)) {
  const ::testing::Matcher<QColor> matcher(colorMatcher);
  for (int y = 0; y < arg.height(); ++y) {
    for (int x = 0; x < arg.width(); ++x) {
      ::testing::StringMatchResultListener inner;
      if (!matcher.MatchAndExplain(arg.pixelColor(x, y), &inner)) {
        *result_listener << "pixel (" << x << ", " << y << ") " << inner.str();
        return false;
      }
    }
  }
  return true;
}

}  // namespace test
}  // namespace qtPilot
