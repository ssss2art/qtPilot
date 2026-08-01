// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT
//
// Views for the three test models. Every view is bound to a real
// QAbstractItemModel from C++ (not a JS array or ListElement), so the
// qt.models.* probe API has something genuine to discover.

import QtQuick
import QtQuick.Controls

Window {
    id: root
    objectName: "modelTestWindow"
    visible: true
    width: 720
    height: 520
    title: "qtPilot QML Model Test App"

    Column {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 12

        Text {
            objectName: "heading"
            text: "qt.models.* exercise app"
            font.pixelSize: 18
        }

        Row {
            spacing: 16
            width: parent.width
            height: root.height - 90

            // --- Flat list with custom roles -----------------------------
            Column {
                width: 220
                height: parent.height
                spacing: 4

                Text { text: "TaskListModel (list, 4 roles)" }

                ListView {
                    objectName: "taskView"
                    width: parent.width
                    height: parent.height - 24
                    clip: true
                    model: taskModel
                    delegate: Item {
                        width: ListView.view.width
                        height: 34
                        required property string title
                        required property string owner
                        required property int priority
                        required property bool done
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: (parent.done ? "✓ " : "• ")
                                  + parent.title + "  [" + parent.owner
                                  + ", p" + parent.priority + "]"
                        }
                    }
                }
            }

            // --- Hierarchy ------------------------------------------------
            Column {
                width: 220
                height: parent.height
                spacing: 4

                Text { text: "FileTreeModel (tree)" }

                // TreeView lives in QtQuick.Controls; a plain ListView cannot
                // show hierarchy, and the point here is that the *model* is a
                // real tree the probe can walk via parentPath.
                TreeView {
                    objectName: "fileTreeView"
                    width: parent.width
                    height: parent.height - 24
                    clip: true
                    model: treeModel
                    delegate: Item {
                        implicitWidth: 200
                        implicitHeight: 26
                        required property string name
                        required property int size
                        required property int depth
                        Text {
                            x: parent.depth * 14
                            anchors.verticalCenter: parent.verticalCenter
                            text: parent.name + (parent.size > 0
                                                 ? "  (" + parent.size + "b)" : "")
                        }
                    }
                }
            }

            // --- Lazily-populated list ------------------------------------
            Column {
                width: 220
                height: parent.height
                spacing: 4

                Text {
                    objectName: "lazyLabel"
                    text: "LazyLogModel (" + lazyView.count + " of 500 loaded)"
                }

                ListView {
                    id: lazyView
                    objectName: "lazyView"
                    width: parent.width
                    height: parent.height - 24
                    clip: true
                    model: lazyModel
                    delegate: Text {
                        required property string message
                        required property string level
                        text: "[" + level + "] " + message
                    }
                }
            }
        }
    }
}
