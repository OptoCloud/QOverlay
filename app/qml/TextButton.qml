import QtQuick

// Shared styled button used by ControlPanel, ControlBar, and OptionsPanel. Each caller used
// to define its own copy of this (identical structure, different sizing/padding) — extracted
// here so the interaction/hover/press logic only needs to be fixed in one place.
Rectangle {
    id: root
    property string label: ""
    property bool active: false
    property color accent: "#2b3140"
    property color accentHover: "#3a4152"
    // Horizontal padding added on top of the label's natural width, and the button's fixed
    // height — these replace the per-caller implicitWidth/implicitHeight literals.
    property int horizontalPadding: 32
    property int buttonHeight: 40
    property int fontSize: 16
    signal clicked()

    implicitWidth: btnText.implicitWidth + horizontalPadding
    implicitHeight: buttonHeight
    radius: 10
    color: active ? "#274063"
         : area.pressed ? Qt.lighter(accentHover, 1.15)
         : (area.containsMouse ? accentHover : accent)
    border.color: active ? "#4f9dff" : "#3a4250"
    border.width: 1

    Text {
        id: btnText
        anchors.centerIn: parent
        text: root.label
        color: "#e6eaf2"
        font.pixelSize: root.fontSize
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        preventStealing: true
        onClicked: root.clicked()
    }
}
