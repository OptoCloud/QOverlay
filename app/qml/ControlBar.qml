import QtQuick

// Per-overlay control bar, floated just under its capture overlay. Opacity slider,
// change-source, and delete — all acting on that overlay via the `ctl` bridge
// (OverlayControls). Context: ctl, pointerLeft, pointerRight.
Rectangle {
    id: root
    implicitWidth: 980
    implicitHeight: 150
    radius: 18
    color: "#12141a"
    border.color: "#2a2f3a"
    border.width: 1

    component TextButton: Rectangle {
        property string label: ""
        property color accent: "#2b3140"
        property color accentHover: "#3a4152"
        signal clicked()
        implicitWidth: t.implicitWidth + 44
        implicitHeight: 64
        radius: 12
        color: a.pressed ? Qt.lighter(accentHover, 1.15) : (a.containsMouse ? accentHover : accent)
        border.color: "#3a4250"; border.width: 1
        Text { id: t; anchors.centerIn: parent; text: label; color: "#e6eaf2"; font.pixelSize: 22 }
        MouseArea { id: a; anchors.fill: parent; hoverEnabled: true; preventStealing: true; onClicked: parent.clicked() }
    }

    Row {
        anchors.fill: parent
        anchors.margins: 22
        spacing: 22

        // Title + kind.
        Column {
            width: 260
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4
            Text {
                width: parent.width
                text: ctl ? ctl.title : ""
                color: "#e6eaf2"; font.pixelSize: 22; font.bold: true; elide: Text.ElideRight
            }
            Text { text: ctl ? ctl.kind : ""; color: "#7d8698"; font.pixelSize: 16 }
        }

        // Opacity slider.
        Column {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            Text {
                text: "opacity  " + Math.round((ctl ? ctl.opacity : 1) * 100) + "%"
                color: "#9aa3b2"; font.pixelSize: 16
            }
            Item {
                id: slider
                width: 320; height: 52
                readonly property real value: ctl ? ctl.opacity : 1
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width; height: 12; radius: 6; color: "#2a3140"
                    Rectangle { width: parent.width * slider.value; height: parent.height; radius: 6; color: "#4f9dff" }
                }
                Rectangle {
                    width: 30; height: 30; radius: 15; color: "#e6eaf2"; border.color: "#4f9dff"; border.width: 2
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
        }

        TextButton {
            anchors.verticalCenter: parent.verticalCenter
            label: "Change source"
            onClicked: if (ctl) ctl.changeSource()
        }
        TextButton {
            anchors.verticalCenter: parent.verticalCenter
            label: "Delete"
            accent: "#40242a"; accentHover: "#5a2f37"
            onClicked: if (ctl) ctl.deleteOverlay()
        }
    }

    // VR pointer cursors are separate OpenVR overlays now (see PointerCursor) — no QML dot.
}
