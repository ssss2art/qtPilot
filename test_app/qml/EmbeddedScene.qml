import QtQuick 2.15
import QtQuick.Controls 2.15

Rectangle {
    id: root
    objectName: "qmlRootRect"
    width: 600
    height: 400
    color: "#f5f5f7"

    Column {
        anchors.centerIn: parent
        spacing: 16

        Text {
            id: qmlStatusText
            objectName: "qmlStatusText"
            text: qmlBridge ? qmlBridge.statusText : "No Bridge"
            font.pixelSize: 18
            font.bold: true
            color: "#333333"
            horizontalAlignment: Text.AlignHCenter
            width: 400
        }

        TextInput {
            id: qmlTextInput
            objectName: "qmlTextInput"
            width: 300
            height: 36
            text: "Edit QML Text"
            font.pixelSize: 14
            color: "#111111"
            verticalAlignment: TextInput.AlignVCenter
            padding: 8

            Rectangle {
                anchors.fill: parent
                color: "white"
                border.color: "#cccccc"
                border.width: 1
                radius: 4
                z: -1
            }
        }

        Button {
            id: qmlButton
            objectName: "qmlButton"
            text: "QML Action Button"
            width: 200
            height: 40
            onClicked: {
                if (qmlBridge) {
                    qmlBridge.increment()
                }
            }
        }

        ListView {
            id: qmlListView
            objectName: "qmlListView"
            width: 300
            height: 120
            clip: true
            model: ["QML Item Alpha", "QML Item Beta", "QML Item Gamma", "QML Item Delta"]
            delegate: Component {
                Rectangle {
                    id: delegateItem
                    objectName: "qmlDelegate_" + index
                    width: 300
                    height: 30
                    color: index % 2 === 0 ? "#ffffff" : "#f0f0f0"
                    border.color: "#e0e0e0"

                    Text {
                        anchors.centerIn: parent
                        text: modelData
                        font.pixelSize: 12
                    }
                }
            }
        }
    }
}
