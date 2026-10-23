import QtQuick

// Options panel for the shared popup overlay: opacity, keep-upright, and lock-to-device for
// the overlay the popup is currently bound to. Context: ctl (OverlayControls, rebound each
// time the popup opens), pointerLeft, pointerRight.
Rectangle {
    id: root
    implicitWidth: 560
    implicitHeight: col.implicitHeight + 44
    radius: 18
    color: "#12141a"
    border.color: "#2a2f3a"
    border.width: 1

    // Shared with ControlPanel/ControlBar — app/qml/TextButton.qml. This panel's buttons use
    // 40px pad, 56px height, 20px font, set per-instance below.

    Column {
        id: col
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 22
        spacing: 18

        Text {
            text: (ctl ? ctl.title : "") + "  ·  options"
            color: "#e6eaf2"; font.pixelSize: 20; font.bold: true; elide: Text.ElideRight
            width: parent.width
        }

        // Opacity.
        Text {
            text: "Opacity  " + Math.round((ctl ? ctl.opacity : 1) * 100) + "%"
            color: "#9aa3b2"; font.pixelSize: 17
        }
        Item {
            id: slider
            width: parent.width; height: 42
            readonly property real value: ctl ? ctl.opacity : 1
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width; height: 12; radius: 6; color: "#2a3140"
                Rectangle { width: parent.width * slider.value; height: parent.height; radius: 6; color: "#4f9dff" }
            }
            Rectangle {
                width: 32; height: 32; radius: 16; color: "#e6eaf2"; border.color: "#4f9dff"; border.width: 2
                y: (slider.height - height) / 2
                x: Math.max(0, Math.min(slider.width - width, slider.width * slider.value - width / 2))
            }
            MouseArea {
                anchors.fill: parent
                preventStealing: true
                function set(mx) { if (ctl) ctl.opacity = Math.max(0.05, Math.min(1, mx / slider.width)) }
                onPressed: (mouse) => set(mouse.x)
                onPositionChanged: (mouse) => { if (pressed) set(mouse.x) }
            }
        }

        Rectangle { width: parent.width; height: 1; color: "#232833" }

        // Keep upright (pitch level).
        Row {
            width: parent.width
            spacing: 14
            Text {
                width: parent.width - pitchToggle.width - 14
                anchors.verticalCenter: parent.verticalCenter
                text: "Keep upright (pitch level)"
                color: "#dfe4ee"; font.pixelSize: 17
            }
            Rectangle {
                id: pitchToggle
                width: 62; height: 32; radius: 16
                anchors.verticalCenter: parent.verticalCenter
                color: (ctl && ctl.pitchLevel) ? "#2f6df0" : "#2a3140"
                border.color: "#3a4250"; border.width: 1
                Rectangle {
                    width: 26; height: 26; radius: 13; color: "#e6eaf2"
                    y: 3; x: (ctl && ctl.pitchLevel) ? parent.width - width - 3 : 3
                    Behavior on x { NumberAnimation { duration: 90 } }
                }
                MouseArea {
                    anchors.fill: parent; preventStealing: true
                    onClicked: if (ctl) ctl.pitchLevel = !ctl.pitchLevel
                }
            }
        }

        // Lock to a tracked device.
        Text { text: "Lock to"; color: "#9aa3b2"; font.pixelSize: 17 }
        Row {
            width: parent.width
            spacing: 10
            Repeater {
                model: [ {k:"off", n:"Off"}, {k:"left", n:"Left"}, {k:"right", n:"Right"}, {k:"head", n:"Head"} ]
                delegate: TextButton {
                    horizontalPadding: 40; buttonHeight: 56; fontSize: 20
                    required property var modelData
                    label: modelData.n
                    active: (ctl ? ctl.lockTarget : "off") === modelData.k
                    onClicked: if (ctl) ctl.lockTarget = modelData.k
                }
            }
        }
    }
}
