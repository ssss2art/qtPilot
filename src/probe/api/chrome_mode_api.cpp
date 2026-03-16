// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "api/chrome_mode_api.h"

#include "accessibility/accessibility_tree_walker.h"
#include "accessibility/console_message_capture.h"
#include "accessibility/role_mapper.h"
#include "api/error_codes.h"
#include "api/response_envelope.h"
#include "interaction/input_simulator.h"

#include <QAccessible>
#include <QAccessibleActionInterface>
#include <QAccessibleTextInterface>
#include <QAccessibleValueInterface>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QWidget>

namespace qtPilot {

// ============================================================================
// Internal helpers (file-scope, not in header)
// ============================================================================

namespace {

// Ephemeral ref map - rebuilt on each readPage call
static QHash<QString, QAccessibleInterface*> s_refToAccessible;
static QHash<QString, QPointer<QObject>> s_refToObject;  // validity check

/// @brief Clear all ephemeral ref mappings.
void clearRefsInternal() {
  s_refToAccessible.clear();
  s_refToObject.clear();
}

/// @brief Parse JSON params string into QJsonObject.
QJsonObject parseParams(const QString& params) {
  return QJsonDocument::fromJson(params.toUtf8()).object();
}

/// @brief Serialize a response envelope to compact JSON string.
QString envelopeToString(const QJsonObject& envelope) {
  return QString::fromUtf8(QJsonDocument(envelope).toJson(QJsonDocument::Compact));
}

/// @brief Resolve a ref string to a QAccessibleInterface*.
/// @throws JsonRpcException if ref not found or stale.
QAccessibleInterface* resolveRef(const QString& ref) {
  auto it = s_refToAccessible.find(ref);
  if (it == s_refToAccessible.end()) {
    throw JsonRpcException(
        ErrorCode::kRefNotFound,
        QStringLiteral("Element ref not found: %1. Call chr.readPage to get fresh refs.").arg(ref),
        QJsonObject{{QStringLiteral("ref"), ref}});
  }
  // Check if underlying QObject is still alive
  auto objIt = s_refToObject.find(ref);
  if (objIt != s_refToObject.end() && objIt.value().isNull()) {
    throw JsonRpcException(ErrorCode::kRefStale,
                           QStringLiteral("Element was destroyed since last readPage: %1").arg(ref),
                           QJsonObject{{QStringLiteral("ref"), ref}});
  }
  if (!it.value()->isValid()) {
    throw JsonRpcException(ErrorCode::kRefStale,
                           QStringLiteral("Element ref is stale: %1").arg(ref),
                           QJsonObject{{QStringLiteral("ref"), ref}});
  }
  return it.value();
}

/// @brief Get the active window, falling back to first visible top-level widget.
/// @throws JsonRpcException if no active window found.
QWidget* getActiveWindowWidget() {
  QWidget* window = QApplication::activeWindow();
  if (window)
    return window;

  // Fall back to first visible top-level widget
  const auto topLevels = QApplication::topLevelWidgets();
  for (QWidget* w : topLevels) {
    if (w->isVisible()) {
      return w;
    }
  }

  throw JsonRpcException(
      ErrorCode::kNoActiveWindow, QStringLiteral("No active Qt window found"),
      QJsonObject{
          {QStringLiteral("hint"), QStringLiteral("Ensure the application has a visible window")}});
}

/// @brief Store ref mappings from a WalkResult into the file-scope maps.
void storeRefMap(const QHash<QString, QAccessibleInterface*>& refMap) {
  for (auto it = refMap.constBegin(); it != refMap.constEnd(); ++it) {
    s_refToAccessible.insert(it.key(), it.value());
    QObject* obj = it.value()->object();
    if (obj) {
      s_refToObject.insert(it.key(), QPointer<QObject>(obj));
    }
  }
}

/// @brief Convert QtMsgType to string.
QString msgTypeToString(QtMsgType type) {
  switch (type) {
    case QtDebugMsg:
      return QStringLiteral("debug");
    case QtWarningMsg:
      return QStringLiteral("warning");
    case QtCriticalMsg:
      return QStringLiteral("error");
    case QtInfoMsg:
      return QStringLiteral("info");
    case QtFatalMsg:
      return QStringLiteral("fatal");
    default:
      return QStringLiteral("unknown");
  }
}

/// @brief Collect all visible text from an accessibility interface recursively.
void collectPageText(QAccessibleInterface* iface, QStringList& texts, int depth, int maxDepth) {
  if (!iface || !iface->isValid() || depth > maxDepth)
    return;

  QAccessible::State state = iface->state();
  if (state.invisible)
    return;

  QAccessible::Role role = iface->role();

  // Text-bearing roles
  switch (role) {
    case QAccessible::StaticText:
    case QAccessible::EditableText:
    case QAccessible::Heading:
    case QAccessible::Paragraph:
    case QAccessible::PushButton:  // Same as QAccessible::Button
    case QAccessible::Link:
    case QAccessible::Label:
    case QAccessible::ListItem:
    case QAccessible::TreeItem:
    case QAccessible::Cell:
    case QAccessible::MenuItem: {
      QString name = iface->text(QAccessible::Name);
      if (!name.isEmpty())
        texts.append(name);
      QString value = iface->text(QAccessible::Value);
      if (!value.isEmpty() && value != name)
        texts.append(value);
      break;
    }
    default:
      break;
  }

  // Recurse into children
  int childCount = iface->childCount();
  for (int i = 0; i < childCount; ++i) {
    QAccessibleInterface* child = iface->child(i);
    if (child)
      collectPageText(child, texts, depth + 1, maxDepth);
  }
}

/// @brief Build a JSON node for a find match result.
QJsonObject buildFindMatchNode(QAccessibleInterface* iface, const QString& ref) {
  QJsonObject node;
  node[QStringLiteral("ref")] = ref;
  node[QStringLiteral("role")] = RoleMapper::toChromeName(iface->role());

  QObject* obj = iface->object();

  // 3-step name fallback (matches AccessibilityTreeWalker::walkNode):
  // accessible name -> objectName -> className
  QString name = iface->text(QAccessible::Name);
  if (name.isEmpty() && obj) {
    name = obj->objectName();
    if (name.isEmpty())
      name = QString::fromUtf8(obj->metaObject()->className());
  }
  if (!name.isEmpty())
    node[QStringLiteral("name")] = name;

  if (obj) {
    QString objName = obj->objectName();
    if (!objName.isEmpty())
      node[QStringLiteral("objectName")] = objName;
    node[QStringLiteral("className")] = QString::fromUtf8(obj->metaObject()->className());
  }

  QRect rect = iface->rect();
  if (rect.isValid()) {
    node[QStringLiteral("bounds")] = QJsonObject{{QStringLiteral("x"), rect.x()},
                                                 {QStringLiteral("y"), rect.y()},
                                                 {QStringLiteral("width"), rect.width()},
                                                 {QStringLiteral("height"), rect.height()}};
  }

  // States
  QAccessible::State state = iface->state();
  QJsonObject states;
  if (state.focused)
    states[QStringLiteral("focused")] = true;
  if (state.disabled)
    states[QStringLiteral("disabled")] = true;
  if (state.checked)
    states[QStringLiteral("checked")] = true;
  if (!states.isEmpty())
    node[QStringLiteral("states")] = states;

  return node;
}

/// @brief Recursively search accessibility tree for elements matching a query.
/// @param iface Current node interface.
/// @param query Lowercase search query.
/// @param refCounter Running counter for ref assignment.
/// @param matches Output array of matching nodes.
/// @param depth Current depth.
/// @param maxDepth Maximum recursion depth.
void findMatchingNodes(QAccessibleInterface* iface, const QString& query, int& refCounter,
                       QJsonArray& matches, int depth, int maxDepth) {
  if (!iface || !iface->isValid() || depth > maxDepth)
    return;

  QAccessible::State state = iface->state();
  if (state.invisible)
    return;

  // Build search corpus for this node
  QString name = iface->text(QAccessible::Name).toLower();
  QString roleName = RoleMapper::toChromeName(iface->role()).toLower();
  QString description = iface->text(QAccessible::Description).toLower();  // tooltip
  QString objName;
  QString className;
  QObject* obj = iface->object();
  if (obj) {
    objName = obj->objectName().toLower();
    className = QString::fromUtf8(obj->metaObject()->className()).toLower();
  }

  // Check case-insensitive substring match
  bool matched = false;
  if (!name.isEmpty() && name.contains(query))
    matched = true;
  else if (roleName.contains(query))
    matched = true;
  else if (!description.isEmpty() && description.contains(query))
    matched = true;
  else if (!objName.isEmpty() && objName.contains(query))
    matched = true;
  else if (!className.isEmpty() && className.contains(query))
    matched = true;

  if (matched) {
    ++refCounter;
    QString ref = QStringLiteral("ref_%1").arg(refCounter);
    s_refToAccessible.insert(ref, iface);
    if (obj)
      s_refToObject.insert(ref, QPointer<QObject>(obj));
    matches.append(buildFindMatchNode(iface, ref));
  }

  // Recurse into children
  int childCount = iface->childCount();
  for (int i = 0; i < childCount; ++i) {
    QAccessibleInterface* child = iface->child(i);
    if (child)
      findMatchingNodes(child, query, refCounter, matches, depth + 1, maxDepth);
  }
}

}  // anonymous namespace

// ============================================================================
// Constructor - register all methods
// ============================================================================

ChromeModeApi::ChromeModeApi(JsonRpcHandler* handler, QObject* parent)
    : QObject(parent), m_handler(handler) {
  registerReadPageMethod();
  registerClickMethod();
  registerFormInputMethod();
  registerGetPageTextMethod();
  registerFindMethod();
  registerNavigateMethod();
  registerTabsContextMethod();
  registerReadConsoleMessagesMethod();
}

void ChromeModeApi::clearRefs() {
  clearRefsInternal();
}

// ============================================================================
// chr.readPage - Read accessibility tree with numbered refs
// ============================================================================

void ChromeModeApi::registerReadPageMethod() {
  m_handler->RegisterMethod(QStringLiteral("chr.readPage"), [](const QString& params) -> QString {
    auto p = parseParams(params);

    // If ref_id provided, resolve BEFORE clearing (to scope subtree)
    QWidget* scopeWidget = nullptr;
    QString refId = p[QStringLiteral("ref_id")].toString();
    if (!refId.isEmpty()) {
      QAccessibleInterface* scopeIface = resolveRef(refId);
      QObject* obj = scopeIface->object();
      if (obj)
        scopeWidget = qobject_cast<QWidget*>(obj);
    }

    clearRefsInternal();

    QWidget* rootWidget = scopeWidget ? scopeWidget : getActiveWindowWidget();

    // Build walk options from params
    WalkOptions opts;
    opts.filter = p[QStringLiteral("filter")].toString(QStringLiteral("all"));
    opts.maxDepth = p[QStringLiteral("depth")].toInt(15);
    opts.maxChars = p[QStringLiteral("max_chars")].toInt(50000);

    WalkResult result = AccessibilityTreeWalker::walk(rootWidget, opts);

    // Store refs
    storeRefMap(result.refMap);

    QJsonObject response;
    response[QStringLiteral("tree")] = result.tree;
    response[QStringLiteral("totalNodes")] = result.totalNodes;
    response[QStringLiteral("truncated")] = result.truncated;

    return envelopeToString(ResponseEnvelope::wrap(response));
  });
}

// ============================================================================
// chr.click - Click element by ref
// ============================================================================

void ChromeModeApi::registerClickMethod() {
  m_handler->RegisterMethod(QStringLiteral("chr.click"), [](const QString& params) -> QString {
    auto p = parseParams(params);
    QString ref = p[QStringLiteral("ref")].toString();

    if (ref.isEmpty()) {
      throw JsonRpcException(JsonRpcError::kInvalidParams,
                             QStringLiteral("Missing required parameter: ref"),
                             QJsonObject{{QStringLiteral("method"), QStringLiteral("chr.click")}});
    }

    QAccessibleInterface* iface = resolveRef(ref);
    QString roleName = RoleMapper::toChromeName(iface->role());

    // Try accessibility action first
    QAccessibleActionInterface* actionIface = iface->actionInterface();
    if (actionIface) {
      QStringList actions = actionIface->actionNames();
      if (actions.contains(QAccessibleActionInterface::pressAction())) {
        actionIface->doAction(QAccessibleActionInterface::pressAction());

        QJsonObject result;
        result[QStringLiteral("clicked")] = true;
        result[QStringLiteral("ref")] = ref;
        result[QStringLiteral("role")] = roleName;
        result[QStringLiteral("method")] = QStringLiteral("accessibilityAction");
        return envelopeToString(ResponseEnvelope::wrap(result));
      }
    }

    // Fall back to mouse click at widget center
    QObject* obj = iface->object();
    QWidget* widget = obj ? qobject_cast<QWidget*>(obj) : nullptr;
    if (widget) {
      QRect rect = iface->rect();
      QPoint center = rect.center();
      QPoint localPos = widget->mapFromGlobal(center);
      InputSimulator::mouseClick(widget, InputSimulator::MouseButton::Left, localPos);

      QJsonObject result;
      result[QStringLiteral("clicked")] = true;
      result[QStringLiteral("ref")] = ref;
      result[QStringLiteral("role")] = roleName;
      result[QStringLiteral("method")] = QStringLiteral("mouseClick");
      return envelopeToString(ResponseEnvelope::wrap(result));
    }

    // If no widget, just try press action anyway
    if (actionIface) {
      actionIface->doAction(QAccessibleActionInterface::pressAction());

      QJsonObject result;
      result[QStringLiteral("clicked")] = true;
      result[QStringLiteral("ref")] = ref;
      result[QStringLiteral("role")] = roleName;
      result[QStringLiteral("method")] = QStringLiteral("accessibilityAction");
      return envelopeToString(ResponseEnvelope::wrap(result));
    }

    throw JsonRpcException(
        ErrorCode::kRefNotFound, QStringLiteral("Element %1 has no clickable interface").arg(ref),
        QJsonObject{{QStringLiteral("ref"), ref}, {QStringLiteral("role"), roleName}});
  });
}

// ============================================================================
// chr.formInput - Set form input value by ref
// ============================================================================

void ChromeModeApi::registerFormInputMethod() {
  m_handler->RegisterMethod(QStringLiteral("chr.formInput"), [](const QString& params) -> QString {
    auto p = parseParams(params);
    QString ref = p[QStringLiteral("ref")].toString();
    QJsonValue value = p[QStringLiteral("value")];

    if (ref.isEmpty()) {
      throw JsonRpcException(
          JsonRpcError::kInvalidParams, QStringLiteral("Missing required parameter: ref"),
          QJsonObject{{QStringLiteral("method"), QStringLiteral("chr.formInput")}});
    }

    QAccessibleInterface* iface = resolveRef(ref);
    QObject* obj = iface->object();
    QString roleName = RoleMapper::toChromeName(iface->role());

    // Strategy 1: QComboBox special case (must come before value interface)
    if (obj) {
      QComboBox* combo = qobject_cast<QComboBox*>(obj);
      if (combo) {
        QString text = value.toString();
        int idx = combo->findText(text);
        if (idx >= 0) {
          combo->setCurrentIndex(idx);
        } else {
          // Try setting as editable text
          if (combo->isEditable()) {
            combo->setCurrentText(text);
          } else {
            throw JsonRpcException(
                ErrorCode::kFormInputUnsupported,
                QStringLiteral("ComboBox value not found: %1").arg(text),
                QJsonObject{{QStringLiteral("ref"), ref}, {QStringLiteral("value"), text}});
          }
        }

        QJsonObject result;
        result[QStringLiteral("success")] = true;
        result[QStringLiteral("ref")] = ref;
        result[QStringLiteral("strategy")] = QStringLiteral("comboBox");
        return envelopeToString(ResponseEnvelope::wrap(result));
      }
    }

    // Strategy 2: Boolean value -> toggle action (checkboxes/radios)
    if (value.isBool()) {
      QAccessibleActionInterface* actionIface = iface->actionInterface();
      if (actionIface) {
        QStringList actions = actionIface->actionNames();
        if (actions.contains(QAccessibleActionInterface::toggleAction())) {
          // Check current state
          QAccessible::State state = iface->state();
          bool currentlyChecked = state.checked;
          bool desired = value.toBool();
          if (currentlyChecked != desired) {
            actionIface->doAction(QAccessibleActionInterface::toggleAction());
          }

          QJsonObject result;
          result[QStringLiteral("success")] = true;
          result[QStringLiteral("ref")] = ref;
          result[QStringLiteral("strategy")] = QStringLiteral("toggleAction");
          return envelopeToString(ResponseEnvelope::wrap(result));
        }
      }
    }

    // Strategy 3: Numeric value -> QAccessibleValueInterface
    if (value.isDouble()) {
      QAccessibleValueInterface* valueIface = iface->valueInterface();
      if (valueIface) {
        valueIface->setCurrentValue(QVariant(value.toDouble()));

        QJsonObject result;
        result[QStringLiteral("success")] = true;
        result[QStringLiteral("ref")] = ref;
        result[QStringLiteral("strategy")] = QStringLiteral("valueInterface");
        return envelopeToString(ResponseEnvelope::wrap(result));
      }
    }

    // Strategy 4: String value -> QAccessibleEditableTextInterface
    if (value.isString()) {
      QAccessibleEditableTextInterface* editIface = iface->editableTextInterface();
      if (editIface) {
        // Get current text length for replacement
        QAccessibleTextInterface* textIface = iface->textInterface();
        int currentLen = 0;
        if (textIface) {
          currentLen = textIface->characterCount();
        }
        editIface->replaceText(0, currentLen, value.toString());

        QJsonObject result;
        result[QStringLiteral("success")] = true;
        result[QStringLiteral("ref")] = ref;
        result[QStringLiteral("strategy")] = QStringLiteral("editableText");
        return envelopeToString(ResponseEnvelope::wrap(result));
      }

      // Strategy 4b: Try QAccessibleValueInterface with string
      QAccessibleValueInterface* valueIface = iface->valueInterface();
      if (valueIface) {
        valueIface->setCurrentValue(QVariant(value.toString()));

        QJsonObject result;
        result[QStringLiteral("success")] = true;
        result[QStringLiteral("ref")] = ref;
        result[QStringLiteral("strategy")] = QStringLiteral("valueInterface");
        return envelopeToString(ResponseEnvelope::wrap(result));
      }
    }

    throw JsonRpcException(
        ErrorCode::kFormInputUnsupported,
        QStringLiteral("No supported input strategy for element %1 (role: %2)").arg(ref, roleName),
        QJsonObject{{QStringLiteral("ref"), ref}, {QStringLiteral("role"), roleName}});
  });
}

// ============================================================================
// chr.getPageText - Get all visible text from active window
// ============================================================================

void ChromeModeApi::registerGetPageTextMethod() {
  m_handler->RegisterMethod(
      QStringLiteral("chr.getPageText"), [](const QString& /*params*/) -> QString {
        QWidget* window = getActiveWindowWidget();

        QAccessible::setActive(true);
        QAccessibleInterface* rootIface = QAccessible::queryAccessibleInterface(window);
        if (!rootIface) {
          QJsonObject result;
          result[QStringLiteral("text")] = QString();
          return envelopeToString(ResponseEnvelope::wrap(result));
        }

        QStringList texts;
        collectPageText(rootIface, texts, 0, 30);

        QJsonObject result;
        result[QStringLiteral("text")] = texts.join(QStringLiteral("\n"));
        return envelopeToString(ResponseEnvelope::wrap(result));
      });
}

// ============================================================================
// chr.find - Find elements by natural language query
// ============================================================================

void ChromeModeApi::registerFindMethod() {
  m_handler->RegisterMethod(QStringLiteral("chr.find"), [](const QString& params) -> QString {
    auto p = parseParams(params);
    QString query = p[QStringLiteral("query")].toString();

    if (query.isEmpty()) {
      throw JsonRpcException(JsonRpcError::kInvalidParams,
                             QStringLiteral("Missing required parameter: query"),
                             QJsonObject{{QStringLiteral("method"), QStringLiteral("chr.find")}});
    }

    QWidget* window = getActiveWindowWidget();

    QAccessible::setActive(true);
    QAccessibleInterface* rootIface = QAccessible::queryAccessibleInterface(window);
    if (!rootIface) {
      QJsonObject result;
      result[QStringLiteral("matches")] = QJsonArray();
      result[QStringLiteral("count")] = 0;
      return envelopeToString(ResponseEnvelope::wrap(result));
    }

    // Append to existing ref map (preserves refs from prior find/readPage calls)
    QJsonArray matches;
    int refCounter = s_refToAccessible.size();
    QString queryLower = query.toLower();

    findMatchingNodes(rootIface, queryLower, refCounter, matches, 0, 30);

    if (matches.size() > 20) {
      throw JsonRpcException(
          ErrorCode::kFindTooManyResults,
          QStringLiteral("Found %1 matches for \"%2\". Be more specific.")
              .arg(matches.size())
              .arg(query),
          QJsonObject{{QStringLiteral("query"), query},
                      {QStringLiteral("count"), matches.size()},
                      {QStringLiteral("hint"),
                       QStringLiteral("Use a more specific query or combine role + name")}});
    }

    QJsonObject result;
    result[QStringLiteral("matches")] = matches;
    result[QStringLiteral("count")] = matches.size();
    return envelopeToString(ResponseEnvelope::wrap(result));
  });
}

// ============================================================================
// chr.navigate - Activate tabs and menu items by ref
// ============================================================================

void ChromeModeApi::registerNavigateMethod() {
  m_handler->RegisterMethod(QStringLiteral("chr.navigate"), [](const QString& params) -> QString {
    auto p = parseParams(params);
    QString action = p[QStringLiteral("action")].toString();

    if (action.isEmpty()) {
      throw JsonRpcException(
          JsonRpcError::kInvalidParams, QStringLiteral("Missing required parameter: action"),
          QJsonObject{{QStringLiteral("method"), QStringLiteral("chr.navigate")}});
    }

    if (action == QStringLiteral("activateTab") || action == QStringLiteral("activateMenuItem")) {
      QString ref = p[QStringLiteral("ref")].toString();
      if (ref.isEmpty()) {
        throw JsonRpcException(
            JsonRpcError::kInvalidParams,
            QStringLiteral("Action '%1' requires 'ref' parameter").arg(action),
            QJsonObject{{QStringLiteral("method"), QStringLiteral("chr.navigate")},
                        {QStringLiteral("action"), action}});
      }

      QAccessibleInterface* iface = resolveRef(ref);
      QAccessibleActionInterface* actionIface = iface->actionInterface();
      if (!actionIface) {
        throw JsonRpcException(
            ErrorCode::kNavigateInvalid,
            QStringLiteral("Element %1 has no action interface").arg(ref),
            QJsonObject{{QStringLiteral("ref"), ref}, {QStringLiteral("action"), action}});
      }

      actionIface->doAction(QAccessibleActionInterface::pressAction());

      QJsonObject result;
      result[QStringLiteral("navigated")] = true;
      result[QStringLiteral("action")] = action;
      result[QStringLiteral("ref")] = ref;
      return envelopeToString(ResponseEnvelope::wrap(result));
    }

    if (action == QStringLiteral("back") || action == QStringLiteral("forward")) {
      QWidget* window = getActiveWindowWidget();

      // Find QAction with undo/redo shortcuts
      QString targetShortcut =
          (action == QStringLiteral("back")) ? QStringLiteral("Ctrl+Z") : QStringLiteral("Ctrl+Y");
      QString altShortcut = (action == QStringLiteral("back")) ? QStringLiteral("Ctrl+Z")
                                                               : QStringLiteral("Ctrl+Shift+Z");

      QList<QAction*> actions = window->findChildren<QAction*>();
      for (QAction* act : actions) {
        if (act->shortcut() == QKeySequence(targetShortcut) ||
            act->shortcut() == QKeySequence(altShortcut)) {
          act->trigger();

          QJsonObject result;
          result[QStringLiteral("navigated")] = true;
          result[QStringLiteral("action")] = action;
          return envelopeToString(ResponseEnvelope::wrap(result));
        }
      }

      throw JsonRpcException(
          ErrorCode::kNavigateInvalid,
          QStringLiteral("No %1 action found in active window").arg(action),
          QJsonObject{
              {QStringLiteral("action"), action},
              {QStringLiteral("hint"),
               QStringLiteral("Window does not have an undo/redo action with standard shortcut")}});
    }

    throw JsonRpcException(
        ErrorCode::kNavigateInvalid, QStringLiteral("Unknown navigate action: %1").arg(action),
        QJsonObject{{QStringLiteral("action"), action},
                    {QStringLiteral("validActions"),
                     QJsonArray{QStringLiteral("activateTab"), QStringLiteral("activateMenuItem"),
                                QStringLiteral("back"), QStringLiteral("forward")}}});
  });
}

// ============================================================================
// chr.tabsContext - List all top-level windows
// ============================================================================

void ChromeModeApi::registerTabsContextMethod() {
  m_handler->RegisterMethod(
      QStringLiteral("chr.tabsContext"), [](const QString& /*params*/) -> QString {
        QWidget* activeWindow = QApplication::activeWindow();
        const auto topLevels = QApplication::topLevelWidgets();

        QJsonArray windows;
        for (QWidget* w : topLevels) {
          if (!w->isVisible())
            continue;

          QJsonObject info;
          info[QStringLiteral("windowTitle")] = w->windowTitle();
          info[QStringLiteral("className")] = QString::fromUtf8(w->metaObject()->className());
          QString objName = w->objectName();
          if (!objName.isEmpty())
            info[QStringLiteral("objectName")] = objName;
          info[QStringLiteral("isActive")] = (w == activeWindow);

          QRect geom = w->geometry();
          info[QStringLiteral("geometry")] = QJsonObject{{QStringLiteral("x"), geom.x()},
                                                         {QStringLiteral("y"), geom.y()},
                                                         {QStringLiteral("width"), geom.width()},
                                                         {QStringLiteral("height"), geom.height()}};

          windows.append(info);
        }

        QJsonObject result;
        result[QStringLiteral("windows")] = windows;
        result[QStringLiteral("count")] = windows.size();
        return envelopeToString(ResponseEnvelope::wrap(result));
      });
}

// ============================================================================
// chr.readConsoleMessages - Read captured qDebug/qWarning messages
// ============================================================================

void ChromeModeApi::registerReadConsoleMessagesMethod() {
  m_handler->RegisterMethod(
      QStringLiteral("chr.readConsoleMessages"), [](const QString& params) -> QString {
        auto p = parseParams(params);

        QString pattern = p[QStringLiteral("pattern")].toString();
        bool onlyErrors = p[QStringLiteral("onlyErrors")].toBool(false);
        bool clearAfter = p[QStringLiteral("clear")].toBool(false);
        int limit = p[QStringLiteral("limit")].toInt(0);

        QList<ConsoleMessage> msgs =
            ConsoleMessageCapture::instance()->messages(pattern, onlyErrors, limit);

        if (clearAfter) {
          ConsoleMessageCapture::instance()->clear();
        }

        QJsonArray messagesJson;
        for (const ConsoleMessage& msg : msgs) {
          QJsonObject msgObj;
          msgObj[QStringLiteral("type")] = msgTypeToString(msg.type);
          msgObj[QStringLiteral("message")] = msg.message;
          if (!msg.file.isEmpty())
            msgObj[QStringLiteral("file")] = msg.file;
          if (msg.line > 0)
            msgObj[QStringLiteral("line")] = msg.line;
          if (!msg.function.isEmpty())
            msgObj[QStringLiteral("function")] = msg.function;
          msgObj[QStringLiteral("timestamp")] = msg.timestamp;
          messagesJson.append(msgObj);
        }

        QJsonObject result;
        result[QStringLiteral("messages")] = messagesJson;
        result[QStringLiteral("count")] = messagesJson.size();
        return envelopeToString(ResponseEnvelope::wrap(result));
      });
}

}  // namespace qtPilot
