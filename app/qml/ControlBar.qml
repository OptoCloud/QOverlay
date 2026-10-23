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

    // Shared with ControlPanel/OptionsPanel — app/qml/TextButton.qml. This bar's buttons are
    // larger than the others (52px pad, 60px height, 22px font), set per-instance below.

    Row {
        anchors.centerIn: parent
        spacing: 18

        TextButton {
            horizontalPadding: 52; buttonHeight: 60; fontSize: 22
            label: "Options"
            active: ctl ? ctl.openPanel === "options" : false
            onClicked: if (ctl) ctl.openOptions()
        }
        TextButton {
            horizontalPadding: 52; buttonHeight: 60; fontSize: 22
            label: "Sources"
            active: ctl ? ctl.openPanel === "sources" : false
            onClicked: if (ctl) ctl.openSources()
        }
        TextButton {
            horizontalPadding: 52; buttonHeight: 60; fontSize: 22
            label: "Close"
            accent: "#40242a"; accentHover: "#5a2f37"
            onClicked: if (ctl) ctl.deleteOverlay()
        }
    }
}
