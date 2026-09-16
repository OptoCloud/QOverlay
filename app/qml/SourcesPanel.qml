import QtQuick

// Sources panel for the shared popup overlay: a Monitors/Windows grouped list; picking a row
// retargets the bound overlay and closes the popup. Context: ctl (OverlayControls),
// windowManager (WindowManager), pointerLeft, pointerRight.
Rectangle {
    id: root
    implicitWidth: 620
    implicitHeight: 620
    radius: 18
    color: "#12141a"
    border.color: "#2a2f3a"
    border.width: 1
    clip: true

    Text {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 18
        text: (ctl ? ctl.title : "") + "  ·  choose a source"
        color: "#e6eaf2"; font.pixelSize: 20; font.bold: true; elide: Text.ElideRight
    }

    ListView {
        id: sourceList
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 12
        anchors.topMargin: 12
        spacing: 4
        model: windowManager ? windowManager.availableSurfaces : []
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        // Grouped by kind — availableSurfaces enumerates monitors first, so sections are contiguous.
        section.property: "kind"
        section.delegate: Text {
            required property string section
            width: ListView.view.width
            padding: 8
            text: section === "monitor" ? "Monitors" : "Windows"
            color: "#7db4ff"; font.pixelSize: 14; font.bold: true
        }

        delegate: Rectangle {
            required property var modelData
            width: ListView.view.width
            height: 60
            radius: 10
            color: rowArea.pressed ? "#22304a" : (rowArea.containsMouse ? "#1b2029" : "#161a22")
            border.color: rowArea.containsMouse ? "#3a7d52" : "#252b36"
            border.width: 1

            Row {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 12

                // App icon (or a generic glyph for monitors / missing icons).
                Item {
                    width: 36; height: 36
                    anchors.verticalCenter: parent.verticalCenter
                    Image {
                        anchors.fill: parent
                        source: modelData.icon ? modelData.icon : ""
                        visible: modelData.icon !== ""
                        fillMode: Image.PreserveAspectFit
                        smooth: true; asynchronous: true
                    }
                    Rectangle {
                        anchors.fill: parent
                        visible: !modelData.icon
                        radius: 8; color: "#243043"; border.color: "#33465f"; border.width: 1
                        Text {
                            anchors.centerIn: parent
                            text: modelData.kind === "monitor" ? "🖵" : "▧"
                            color: "#7db4ff"; font.pixelSize: 18
                        }
                    }
                }

                // App name + window-title description.
                Column {
                    width: parent.width - 36 - 12
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    Text {
                        width: parent.width
                        text: modelData.app ? modelData.app : modelData.title
                        color: "#e6eaf2"; font.pixelSize: 17; font.bold: true; elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        text: modelData.title
                        color: "#7d8698"; font.pixelSize: 14; elide: Text.ElideRight
                        visible: text.length > 0
                    }
                }
            }

            MouseArea {
                id: rowArea
                anchors.fill: parent
                hoverEnabled: true
                preventStealing: true // beat the ListView flick-steal in VR
                onClicked: {
                    if (ctl) { ctl.setSource(modelData.id); ctl.closePanel() }
                }
            }
        }
    }
}
