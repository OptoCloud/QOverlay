import QtQuick

// Per-overlay control bar: just three buttons — Options, Sources, Close. Options and Sources
// toggle the single shared popup overlay (see PopupOverlay) to this overlay's panel; the popup
// teleports here and binds to this overlay's `ctl`. The bar highlights whichever panel is open
// via ctl.openPanel. Context: ctl (OverlayControls), pointerLeft, pointerRight.
Rectangle {
    id: root
    implicitWidth: 620
    implicitHeight: 100
    radius: 18
    color: "#12141a"
    border.color: "#2a2f3a"
    border.width: 1

    component TextButton: Rectangle {
        property string label: ""
        property bool active: false
        property color accent: "#2b3140"
        property color accentHover: "#3a4152"
        signal clicked()
        implicitWidth: t.implicitWidth + 52
        implicitHeight: 60
        radius: 12
        color: active ? "#274063"
             : a.pressed ? Qt.lighter(accentHover, 1.15)
             : (a.containsMouse ? accentHover : accent)
        border.color: active ? "#4f9dff" : "#3a4250"
        border.width: 1
        Text { id: t; anchors.centerIn: parent; text: label; color: "#e6eaf2"; font.pixelSize: 22 }
        MouseArea { id: a; anchors.fill: parent; hoverEnabled: true; preventStealing: true; onClicked: parent.clicked() }
    }

    Row {
        anchors.centerIn: parent
        spacing: 18

        TextButton {
            label: "Options"
            active: ctl ? ctl.openPanel === "options" : false
            onClicked: if (ctl) ctl.openOptions()
        }
        TextButton {
            label: "Sources"
            active: ctl ? ctl.openPanel === "sources" : false
            onClicked: if (ctl) ctl.openSources()
        }
        TextButton {
            label: "Close"
            accent: "#40242a"; accentHover: "#5a2f37"
            onClicked: if (ctl) ctl.deleteOverlay()
        }
    }
}
