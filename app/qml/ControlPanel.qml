import QtQuick

// QOverlay control panel — the VR window manager UI. Lists capturable monitors and
// windows (Add → spawns a capture overlay) and the currently-active overlays (Close →
// destroys one). Context properties from C++: `app` (OverlayBridge: status/hand switch)
// and `windowManager` (WindowManager: surface enumeration + overlay lifecycle).
Rectangle {
    id: root
    implicitWidth: 720
    implicitHeight: 1060
    radius: 20
    color: "#12141a"
    border.color: "#2a2f3a"
    border.width: 1

    // Small pill button used throughout the panel.
    component TextButton: Rectangle {
        id: btn
        property string label: ""
        property color accent: "#2b3140"
        property color accentHover: "#3a4152"
        signal clicked()

        implicitWidth: btnText.implicitWidth + 32
        implicitHeight: 40
        radius: 10
        color: area.pressed ? Qt.lighter(accentHover, 1.15) : (area.containsMouse ? accentHover : accent)
        border.color: "#3a4250"
        border.width: 1

        Text {
            id: btnText
            anchors.centerIn: parent
            text: btn.label
            color: "#e6eaf2"
            font.pixelSize: 16
        }
        MouseArea {
            id: area
            anchors.fill: parent
            hoverEnabled: true
            // Keep the press grab so an enclosing Flickable (the preview grid / lists)
            // can't reinterpret VR-pointer jitter as a scroll and swallow the click.
            preventStealing: true
            onClicked: btn.clicked()
        }
    }

    // Colored badge for a surface kind (monitor / window).
    component KindBadge: Rectangle {
        property string kind: "window"
        implicitWidth: badgeText.implicitWidth + 16
        implicitHeight: 22
        radius: 11
        color: kind === "monitor" ? "#1e3a5f" : "#3a2f1e"
        Text {
            id: badgeText
            anchors.centerIn: parent
            text: parent.kind
            color: parent.kind === "monitor" ? "#7db4ff" : "#e0b877"
            font.pixelSize: 12
        }
    }

    Column {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 18

        // ── Header ──
        Row {
            width: parent.width
            Column {
                width: parent.width - refreshBtn.width
                spacing: 2
                Text { text: "QOverlay"; color: "#f2f4f8"; font.pixelSize: 32; font.bold: true }
                Text { text: "VR window manager"; color: "#7d8698"; font.pixelSize: 14 }
            }
            TextButton {
                id: refreshBtn
                label: "Refresh"
                anchors.verticalCenter: parent.verticalCenter
                onClicked: if (windowManager) windowManager.refresh()
            }
        }

        Rectangle { width: parent.width; height: 1; color: "#232833" }

        // ── Available surfaces (preview grid) ──
        Text {
            text: "Available  ·  " + (windowManager ? windowManager.availableSurfaces.length : 0)
            color: "#9aa3b2"
            font.pixelSize: 15
            font.bold: true
        }

        Rectangle {
            width: parent.width
            height: 470
            radius: 12
            color: "#0d0f14"
            border.color: "#222834"
            border.width: 1
            clip: true

            GridView {
                id: grid
                anchors.fill: parent
                anchors.margins: 10
                cellWidth: Math.floor(width / 3)
                cellHeight: cellWidth * 0.82
                model: windowManager ? windowManager.availableSurfaces : []
                boundsBehavior: Flickable.StopAtBounds

                delegate: Item {
                    required property var modelData
                    width: grid.cellWidth
                    height: grid.cellHeight

                    // The whole tile is the Add affordance — click it to spawn the overlay.
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 5
                        radius: 10
                        color: "#161a22"
                        border.color: tileArea.containsMouse ? "#3a7d52" : "#252b36"
                        border.width: tileArea.pressed ? 2 : 1
                        clip: true

                        // Preview image, or a placeholder if the grab failed.
                        Image {
                            anchors.fill: parent
                            anchors.margins: 1
                            source: modelData.preview
                            fillMode: Image.PreserveAspectCrop
                            visible: modelData.preview !== ""
                            asynchronous: true
                        }
                        Text {
                            anchors.centerIn: parent
                            visible: modelData.preview === ""
                            text: "no preview"
                            color: "#4a515f"
                            font.pixelSize: 13
                        }

                        KindBadge {
                            kind: modelData.kind
                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.margins: 8
                        }

                        // Title caption over a bottom scrim.
                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 34
                            color: "#d90d0f14"
                            Text {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                verticalAlignment: Text.AlignVCenter
                                text: modelData.title
                                color: "#dfe4ee"
                                font.pixelSize: 13
                                elide: Text.ElideRight
                            }
                        }

                        // "Added" flash on click.
                        Rectangle {
                            id: addFlash
                            anchors.fill: parent
                            radius: 10
                            color: "#3a7d52"
                            opacity: 0
                            Text {
                                anchors.centerIn: parent
                                text: "Added"
                                color: "#ffffff"
                                font.pixelSize: 18
                                font.bold: true
                            }
                            NumberAnimation {
                                id: flashAnim
                                target: addFlash
                                property: "opacity"
                                from: 0.85; to: 0
                                duration: 550
                            }
                        }

                        MouseArea {
                            id: tileArea
                            anchors.fill: parent
                            hoverEnabled: true
                            preventStealing: true // beat the GridView's flick-steal in VR
                            onClicked: {
                                if (windowManager) windowManager.addSurface(modelData.id)
                                flashAnim.restart()
                            }
                        }
                    }
                }
            }
        }

        // ── Active overlays ──
        Row {
            width: parent.width
            Text {
                width: parent.width - closeAllBtn.width
                anchors.verticalCenter: parent.verticalCenter
                text: "Active overlays  ·  " + (windowManager ? windowManager.activeOverlays.length : 0)
                color: "#9aa3b2"
                font.pixelSize: 15
                font.bold: true
            }
            TextButton {
                id: closeAllBtn
                label: "Close all"
                accent: "#40242a"
                accentHover: "#5a2f37"
                visible: windowManager && windowManager.activeOverlays.length > 0
                onClicked: if (windowManager) windowManager.closeAll()
            }
        }

        Rectangle {
            width: parent.width
            height: 180
            radius: 12
            color: "#0d0f14"
            border.color: "#222834"
            border.width: 1
            clip: true

            Text {
                anchors.centerIn: parent
                width: parent.width - 40
                horizontalAlignment: Text.AlignHCenter
                visible: !windowManager || windowManager.activeOverlays.length === 0
                text: "No active overlays.\nAdd a monitor or window above."
                color: "#5f6878"
                font.pixelSize: 15
            }

            ListView {
                anchors.fill: parent
                anchors.margins: 6
                spacing: 4
                model: windowManager ? windowManager.activeOverlays : []

                delegate: Rectangle {
                    required property var modelData
                    width: ListView.view.width
                    height: 52
                    radius: 8
                    color: "#161a22"

                    Row {
                        anchors.left: parent.left
                        anchors.right: closeBtn.left
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 12
                        anchors.rightMargin: 8
                        spacing: 10

                        KindBadge {
                            kind: modelData.kind
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            width: parent.width - 100
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.title
                            color: "#dfe4ee"
                            font.pixelSize: 15
                            elide: Text.ElideRight
                        }
                    }

                    TextButton {
                        id: closeBtn
                        label: "Close"
                        accent: "#40242a"
                        accentHover: "#5a2f37"
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.rightMargin: 10
                        onClicked: if (windowManager) windowManager.closeOverlay(modelData.id)
                    }
                }
            }
        }
    }

    // ── Footer: hand switch + status ──
    Row {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 28
        spacing: 14

        TextButton {
            label: (app && app.keyboardVisible) ? "Hide keyboard" : "Keyboard"
            accent: (app && app.keyboardVisible) ? "#2b3f5a" : "#2b3140"
            accentHover: "#3a4a66"
            onClicked: if (app) app.toggleKeyboard()
        }
        TextButton {
            label: (app ? (app.leftHanded ? "Left hand" : "Right hand") : "—") + "  ·  switch"
            onClicked: if (app) app.switchHand()
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: app ? app.status : ""
            color: "#5f6878"
            font.pixelSize: 14
        }
    }

    // The VR pointer cursors are drawn as separate OpenVR overlays now (see PointerCursor),
    // so hovering doesn't re-rasterize this panel just to move a dot.
}
