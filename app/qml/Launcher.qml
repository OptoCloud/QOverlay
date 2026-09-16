import QtQuick

// Small wrist-mounted launcher. A single icon button: click to toggle the full control
// panel. Rendered as its own small overlay attached to the controller. Context property
// `app` is the OverlayBridge; `pointer` is the VR cursor state.
Rectangle {
    id: root
    implicitWidth: 256
    implicitHeight: 256
    color: "transparent"

    Rectangle {
        anchors.centerIn: parent
        width: 200
        height: 200
        radius: 44
        color: press.pressed ? "#2f6df0"
             : (press.containsMouse ? "#1f2634" : "#151a24")
        border.color: (app && app.panelVisible) ? "#2f6df0" : "#39414f"
        border.width: 3

        // Simple "windows/overlays" glyph: four rounded tiles.
        Grid {
            anchors.centerIn: parent
            columns: 2
            rowSpacing: 16
            columnSpacing: 16
            Repeater {
                model: 4
                Rectangle {
                    width: 52
                    height: 52
                    radius: 10
                    color: (app && app.panelVisible) ? "#8fb4ff" : "#cdd6e6"
                }
            }
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 14
            text: (app && app.panelVisible) ? "close" : "open"
            color: "#8b94a6"
            font.pixelSize: 20
        }

        MouseArea {
            id: press
            anchors.fill: parent
            hoverEnabled: true
            preventStealing: true
            onClicked: if (app) app.togglePanel()
        }
    }

    // VR pointer cursors are separate OpenVR overlays now (see PointerCursor) — no QML dot.
}
