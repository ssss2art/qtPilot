// Copyright (c) 2024 qtPilot Contributors
// SPDX-License-Identifier: MIT

import QtQuick
import QtQuick.Controls

Window {
    id: root

    required property QtObject demoModel
    property int count: 0

    objectName: "demoWindow"
    width: 480
    height: 320
    visible: true
    title: "qtPilot WebMCP example"

    Column {
        anchors.centerIn: parent
        spacing: 16

        Text {
            objectName: "countLabel"
            text: "Count: " + root.count
            font.pixelSize: 24
        }

        Button {
            objectName: "incrementButton"
            text: "Increment"
            onClicked: root.count += 1
        }

        ListView {
            objectName: "itemsView"
            width: 220
            height: 80
            model: root.demoModel
            delegate: Component {
                Text {
                    required property string display
                    text: display
                }
            }
        }
    }

    function resetCount() {
        count = 0
    }
}
