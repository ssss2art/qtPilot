// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#pragma once

namespace qtPilot {
namespace ErrorCode {

// Object errors (-32001 to -32009)
constexpr int kObjectNotFound = -32001;
constexpr int kObjectStale = -32002;
constexpr int kObjectNotWidget = -32003;

// Property errors (-32010 to -32019)
constexpr int kPropertyNotFound = -32010;
constexpr int kPropertyReadOnly = -32011;
constexpr int kPropertyTypeMismatch = -32012;

// Method errors (-32020 to -32029)
constexpr int kMethodNotFound = -32020;
constexpr int kMethodInvocationFailed = -32021;
constexpr int kMethodArgumentMismatch = -32022;

// Signal errors (-32030 to -32039)
constexpr int kSignalNotFound = -32030;
constexpr int kSubscriptionNotFound = -32031;

// UI errors (-32040 to -32049)
constexpr int kWidgetNotVisible = -32040;
constexpr int kWidgetNotEnabled = -32041;
constexpr int kScreenCaptureError = -32042;

// Name map errors (-32050 to -32059)
constexpr int kNameNotFound = -32050;
constexpr int kNameAlreadyExists = -32051;
constexpr int kNameMapLoadError = -32052;

// Computer Use errors (-32060 to -32069)
constexpr int kNoActiveWindow = -32060;
constexpr int kCoordinateOutOfBounds = -32061;
constexpr int kNoFocusedWidget = -32062;
constexpr int kKeyParseError = -32063;

// Chrome Mode errors (-32070 to -32079)
constexpr int kRefNotFound = -32070;
constexpr int kRefStale = -32071;
constexpr int kFormInputUnsupported = -32072;
constexpr int kTreeTooLarge = -32073;
constexpr int kFindTooManyResults = -32074;
constexpr int kNavigateInvalid = -32075;
constexpr int kConsoleNotAvailable = -32076;

// QML errors (-32080 to -32089)
constexpr int kQmlNotAvailable = -32080;
constexpr int kQmlContextNotFound = -32081;
constexpr int kNotQmlItem = -32082;

// Model/View errors (-32090 to -32099)
constexpr int kModelNotFound = -32090;
constexpr int kModelIndexOutOfBounds = -32091;
constexpr int kModelRoleNotFound = -32092;
constexpr int kNotAModel = -32093;

}  // namespace ErrorCode
}  // namespace qtPilot
