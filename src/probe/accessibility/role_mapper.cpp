// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

#include "role_mapper.h"

#include <QHash>
#include <QSet>

namespace qtPilot {

// Complete QAccessible::Role to Chrome/ARIA role name mapping (~55 entries)
// Source: Qt 6 QAccessible::Role enum + ARIA role definitions
static const QHash<QAccessible::Role, QString> s_roleMap = {
    // Interactive widget roles
    {QAccessible::Button, QStringLiteral("button")},
    {QAccessible::CheckBox, QStringLiteral("checkbox")},
    {QAccessible::RadioButton, QStringLiteral("radio")},
    {QAccessible::ComboBox, QStringLiteral("combobox")},
    {QAccessible::SpinBox, QStringLiteral("spinbutton")},
    {QAccessible::Slider, QStringLiteral("slider")},
    {QAccessible::Dial, QStringLiteral("slider")},  // Dial has no ARIA equiv
    {QAccessible::ProgressBar, QStringLiteral("progressbar")},
    {QAccessible::EditableText, QStringLiteral("textbox")},
    {QAccessible::Link, QStringLiteral("link")},

    // Menu roles
    {QAccessible::MenuBar, QStringLiteral("menubar")},
    {QAccessible::PopupMenu, QStringLiteral("menu")},
    {QAccessible::MenuItem, QStringLiteral("menuitem")},
    {QAccessible::ButtonMenu, QStringLiteral("button")},  // Menu button
    {QAccessible::ButtonDropDown, QStringLiteral("button")},

    // Container/structure roles
    {QAccessible::Window, QStringLiteral("window")},
    {QAccessible::Dialog, QStringLiteral("dialog")},
    {QAccessible::ToolBar, QStringLiteral("toolbar")},
    {QAccessible::StatusBar, QStringLiteral("status")},
    {QAccessible::Grouping, QStringLiteral("group")},
    {QAccessible::Separator, QStringLiteral("separator")},
    {QAccessible::Splitter, QStringLiteral("separator")},  // Splitter ~ separator
    {QAccessible::Pane, QStringLiteral("region")},
    {QAccessible::Client, QStringLiteral("generic")},  // Central widget area
    {QAccessible::Application, QStringLiteral("application")},
    {QAccessible::Document, QStringLiteral("document")},
    {QAccessible::WebDocument, QStringLiteral("document")},
    {QAccessible::Section, QStringLiteral("section")},
    {QAccessible::Heading, QStringLiteral("heading")},
    {QAccessible::Paragraph, QStringLiteral("paragraph")},
    {QAccessible::Form, QStringLiteral("form")},
    {QAccessible::Notification, QStringLiteral("alert")},
    {QAccessible::Note, QStringLiteral("note")},
    {QAccessible::ComplementaryContent, QStringLiteral("complementary")},
    {QAccessible::Footer, QStringLiteral("contentinfo")},

    // Tab roles
    {QAccessible::PageTab, QStringLiteral("tab")},
    {QAccessible::PageTabList, QStringLiteral("tablist")},
    {QAccessible::PropertyPage, QStringLiteral("tabpanel")},

    // Table/list/tree roles
    {QAccessible::Table, QStringLiteral("table")},
    {QAccessible::ColumnHeader, QStringLiteral("columnheader")},
    {QAccessible::RowHeader, QStringLiteral("rowheader")},
    {QAccessible::Row, QStringLiteral("row")},
    {QAccessible::Cell, QStringLiteral("cell")},
    {QAccessible::List, QStringLiteral("list")},
    {QAccessible::ListItem, QStringLiteral("listitem")},
    {QAccessible::Tree, QStringLiteral("tree")},
    {QAccessible::TreeItem, QStringLiteral("treeitem")},

    // Text/display roles
    {QAccessible::StaticText, QStringLiteral("text")},  // Chrome uses "text"
    {QAccessible::Graphic, QStringLiteral("img")},
    {QAccessible::Chart, QStringLiteral("img")},  // Chart ~ image
    {QAccessible::Canvas, QStringLiteral("img")},
    {QAccessible::Animation, QStringLiteral("img")},

    // Misc roles
    {QAccessible::TitleBar, QStringLiteral("banner")},
    {QAccessible::ScrollBar, QStringLiteral("scrollbar")},
    {QAccessible::ToolTip, QStringLiteral("tooltip")},
    {QAccessible::HelpBalloon, QStringLiteral("tooltip")},
    {QAccessible::AlertMessage, QStringLiteral("alert")},
    {QAccessible::Indicator, QStringLiteral("status")},
    {QAccessible::Whitespace, QStringLiteral("none")},
    {QAccessible::LayeredPane, QStringLiteral("region")},
    {QAccessible::Terminal, QStringLiteral("log")},
    {QAccessible::Desktop, QStringLiteral("application")},
    {QAccessible::ColorChooser, QStringLiteral("dialog")},
    {QAccessible::Clock, QStringLiteral("timer")},
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    {QAccessible::BlockQuote, QStringLiteral("blockquote")},
#endif
};

// Roles that represent interactive elements an agent can interact with
static const QSet<QAccessible::Role> s_interactiveRoles = {
    QAccessible::Button,         QAccessible::CheckBox,     QAccessible::RadioButton,
    QAccessible::ComboBox,       QAccessible::SpinBox,      QAccessible::Slider,
    QAccessible::Dial,           QAccessible::EditableText, QAccessible::Link,
    QAccessible::MenuItem,       QAccessible::PageTab,      QAccessible::ListItem,
    QAccessible::TreeItem,       QAccessible::Cell,         QAccessible::ButtonMenu,
    QAccessible::ButtonDropDown, QAccessible::ScrollBar,    QAccessible::HotkeyField,
};

QString RoleMapper::toChromeName(QAccessible::Role role) {
  auto it = s_roleMap.constFind(role);
  if (it != s_roleMap.constEnd())
    return it.value();
  return QStringLiteral("generic");
}

bool RoleMapper::isInteractive(QAccessible::Role role) {
  return s_interactiveRoles.contains(role);
}

}  // namespace qtPilot
