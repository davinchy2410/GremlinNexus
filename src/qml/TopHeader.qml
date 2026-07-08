import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import QtQuick.Effects
import QtQuick.Window
import GremblingNexus

// Top navigation bar (Fase 15.5): replaces the left Sidebar rail with a
// fixed-height horizontal header - branding on the left, the section tabs
// centered, and the global Engine switch on the right. Selection state
// still lives in mainViewModel.currentView, same as the old Sidebar.
//
// UI refactor: the center group used to hold 8 flat tabs (Profiles, Device
// Tester, Web Dashboard, Auto-Switch, PWA Editor, Quick Save, Settings, Log
// Console), which is what pushed this bar's combined width past the
// window's and forced the clip:true safety net below in the first place.
// Only the 3 primary destinations stay as first-level tabs now; the four
// secondary tools live behind "TOOLS ▾". Quick Save moved out entirely -
// it's Profiles-screen-specific (profileEditorViewModel.quickSave()), not a
// global action, so it now lives as a "SAVE" button in
// ProfileEditorView.qml's own header instead of floating in this app-wide
// bar.
Rectangle {
    id: root
    color: Theme.mantle
    implicitHeight: 64

    /// Bubbles up to main.qml, which owns the actual RemoteControlPopup
    /// instance - same signal Sidebar used to expose.
    signal remoteControlRequested()
    signal autoSwitchRequested()

    // The Engine switch's ON state is a dedicated "phosphor LED" look
    // (bright neon green + near-black ink for max contrast) rather than
    // Theme.success (a softer teal reserved for status dots/log lines) -
    // this is the one high-priority master control in the whole header, so
    // it gets its own unmistakable palette instead of blending in with
    // every other "success" affordance.
    readonly property color engineOnColor: "#00ff41"
    readonly property color engineOnInk: Theme.mantle   // #070a14 - max contrast against engineOnColor
    readonly property color engineOffColor: Theme.overlay0 // dim slate - reads as visually "off"

    // Shadow to float above content
    MultiEffect {
        source: root
        anchors.fill: root
        shadowEnabled: true
        shadowColor: Theme.shadowColor
        shadowBlur: 1.0
        shadowVerticalOffset: 4
        z: -1
    }

    Rectangle {
        // Subtle 1px seam separating the header from the content area below -
        // one step darker than the header itself, consistent with the old
        // Sidebar's own right-edge seam.
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.surface0
    }

    // Fase 12: drag-to-move the frameless window from anywhere in the header
    // that isn't already claimed by a TopHeaderButton/GremblingButton/
    // WindowControls button - those are all declared (and therefore painted,
    // and hit-tested first) after this MouseArea, so their own MouseAreas
    // still win a click at the same point. startSystemMove() hands the
    // actual drag off to Windows itself (snapping, multi-monitor, ...)
    // instead of hand-rolling position math.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        onPressed: (mouse) => {
            if (mouse.button === Qt.LeftButton) {
                Window.window.startSystemMove();
            }
        }
        onDoubleClicked: {
            Window.window.toggleSmartMaximize();
        }
    }

    // Fase 12: anchored independently of the RowLayout below (rather than
    // being one of its children) and flush against the top-right corner,
    // matching every native title bar's own convention - if it lived inside
    // the RowLayout instead, this header's own tab overflow could shove it
    // past the window's right edge, making "Close" impossible to reach
    // without resizing first.
    WindowControls {
        id: windowControls
        anchors.top: parent.top
        anchors.right: parent.right
    }

    RowLayout {
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: windowControls.left
        anchors.leftMargin: Theme.spacingMd
        anchors.rightMargin: Theme.spacingMd
        spacing: Theme.spacingSm

        // Hard safety net - however wide the nav group wants to be, nothing
        // in this RowLayout may render past its own anchored bounds (which
        // stop at windowControls.left). Without this, a RowLayout whose
        // children can't shrink below their own implicit width just
        // overflows past its anchor when there isn't enough room.
        clip: true

        // --- Left: branding ----------------------------------------------
        RowLayout {
            spacing: Theme.spacingSm

            Item {
                Layout.preferredWidth: 22
                Layout.preferredHeight: 22

                SvgIcon {
                    id: logoIcon
                    anchors.fill: parent
                    pathData: "M12 3.4l8.6 5v10l-8.6 5 -8.6 -5v-10z M6.8 9.2v8.6 M6.8 9.2l10.4 8.6 M17.2 17.8V9.2"
                    color: Theme.accent
                }
            }

            ColumnLayout {
                spacing: 0
                Text {
                    text: qsTr("Gremlin Nexus")
                    color: Theme.text
                    font.pixelSize: 16
                    font.weight: Font.Bold
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 1
                }
                Text {
                    text: qsTr("Input Engine")
                    color: Theme.overlay0
                    font.pixelSize: 10
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 1
                }
            }
        }

        // --- Center-left: primary nav + Tools menu + Quick Save -----------
        // Layout.fillWidth (rather than the old AlignHCenter) so the group
        // packs left, right after branding, leaving the rest of the row's
        // slack space between it and the Engine switch.
        RowLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
            spacing: 0

            TopHeaderButton {
                label: qsTr("Profiles")
                iconData: "M3 3h18v18H3z M9 3v18 M15 3v18 M3 9h18 M3 15h18 M3 21L21 3"
                selected: mainViewModel.currentView === "Profiles"
                onClicked: mainViewModel.currentView = "Profiles"
            }
            TopHeaderButton {
                label: qsTr("Device Tester")
                iconData: "M12 21a9 9 0 1 0 0 -18 9 9 0 0 0 0 18z M12 1v22 M1 12h22"
                selected: mainViewModel.currentView === "DeviceTester"
                onClicked: mainViewModel.currentView = "DeviceTester"
            }
            TopHeaderButton {
                label: qsTr("Settings")
                iconData: "M12 15a3 3 0 1 0 0 -6 3 3 0 0 0 0 6z M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1 0 2.83 2 2 0 0 1 -2.83 0l-.06 -.06a1.65 1.65 0 0 0 -1.82 -.33 1.65 1.65 0 0 0 -1 1.51V21a2 2 0 0 1 -2 2 2 2 0 0 1 -2 -2v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0 -1.82.33l-.06.06a2 2 0 0 1 -2.83 0 2 2 0 0 1 0 -2.83l.06 -.06a1.65 1.65 0 0 0 .33 -1.82 1.65 1.65 0 0 0 -1.51 -1H3a2 2 0 0 1 -2 -2 2 2 0 0 1 2 -2h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0 -.33 -1.82l-.06 -.06a2 2 0 0 1 0 -2.83 2 2 0 0 1 2.83 0l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1 -1.51V3a2 2 0 0 1 2 -2 2 2 0 0 1 2 2v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82 -.33l.06 -.06a2 2 0 0 1 2.83 0 2 2 0 0 1 0 2.83l-.06.06a1.65 1.65 0 0 0 -.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 2 2 2 2 0 0 1 -2 2h-.09a1.65 1.65 0 0 0 -1.51 1z"
                selected: mainViewModel.currentView === "Settings"
                onClicked: mainViewModel.currentView = "Settings"
            }

            // --- "TOOLS ▾" dropdown --------------------------------------
            // Houses the four secondary actions that don't need to be a
            // permanent, always-visible tab: two popups (Web Dashboard,
            // Auto-Switch), the PWA Editor launcher, and the Log Console
            // view switch. Styled like TopHeaderButton (same pill/hover
            // language) with a caret that flips when the menu is open.
            Item {
                id: toolsButton
                implicitWidth: toolsContent.implicitWidth + 20
                implicitHeight: 40

                readonly property bool menuOpen: toolsPopup.visible

                // Guard against the classic Popup "reopen bounce": a click
                // that lands on this button while toolsPopup is open first
                // triggers the Popup's own CloseOnPressOutside handling
                // (on the press), which flips visible to false *before*
                // this same click's release reaches toolsMouseArea.onClicked
                // below - so a naive `visible ? close() : open()` ternary
                // sees it as already-closed and immediately reopens it.
                // toolsPopup's onClosed sets this flag the instant that
                // outside-press close happens, so the paired onClicked can
                // recognize "this click is the one that just closed it" and
                // no-op instead of reopening; the short Timer clears the
                // flag again so the next, unrelated click behaves normally.
                property bool suppressReopen: false

                Timer {
                    id: toolsReopenGuard
                    interval: 150
                    onTriggered: toolsButton.suppressReopen = false
                }

                Rectangle {
                    id: toolsBg
                    anchors.fill: parent
                    radius: Theme.radiusMedium
                    color: toolsButton.menuOpen
                        ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.1)
                        : (toolsMouseArea.containsMouse ? Theme.surface0 : "transparent")
                    Behavior on color { ColorAnimation { duration: Theme.animMedium; easing.type: Easing.OutCubic } }

                    scale: toolsMouseArea.pressed ? 0.97 : (toolsMouseArea.containsMouse ? 1.02 : 1.0)
                    Behavior on scale { NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutBack } }
                }

                RowLayout {
                    id: toolsContent
                    anchors.centerIn: parent
                    spacing: 4

                    Text {
                        text: qsTr("Tools")
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                        font.capitalization: Font.AllUppercase
                        font.letterSpacing: 1
                        color: toolsButton.menuOpen ? Theme.accent : (toolsMouseArea.containsMouse ? Theme.text : Theme.subtext0)
                        Behavior on color { ColorAnimation { duration: Theme.animMedium } }
                    }
                    Text {
                        text: "▾"
                        font.pixelSize: 10
                        color: toolsButton.menuOpen ? Theme.accent : (toolsMouseArea.containsMouse ? Theme.text : Theme.overlay0)
                        rotation: toolsButton.menuOpen ? 180 : 0
                        Behavior on rotation { NumberAnimation { duration: Theme.animFast } }
                        Behavior on color { ColorAnimation { duration: Theme.animMedium } }
                    }
                }

                MouseArea {
                    id: toolsMouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (toolsButton.suppressReopen) {
                            return;
                        }
                        if (toolsPopup.visible) {
                            toolsPopup.close();
                        } else {
                            toolsPopup.open();
                        }
                    }
                }

                Popup {
                    id: toolsPopup
                    y: toolsButton.height + 4
                    x: 0
                    width: 220
                    padding: 4
                    closePolicy: Popup.CloseOnPressOutside | Popup.CloseOnEscape

                    onClosed: {
                        toolsButton.suppressReopen = true;
                        toolsReopenGuard.restart();
                    }

                    background: Item {
                        Rectangle {
                            id: toolsPopupBg
                            anchors.fill: parent
                            color: Qt.rgba(Theme.surface0.r, Theme.surface0.g, Theme.surface0.b, 0.95)
                            border.width: 1
                            border.color: Theme.accent
                        }
                        MultiEffect {
                            source: toolsPopupBg
                            anchors.fill: toolsPopupBg
                            shadowEnabled: true
                            shadowColor: Theme.accent
                            shadowBlur: 2.0
                            blurMax: 32
                        }
                    }

                    contentItem: Column {
                        spacing: 0

                        Repeater {
                            // Plain JS array (not a ListModel) so each entry
                            // can carry its own action closure straight
                            // through to the delegate via modelData.run().
                            model: [
                                {
                                    label: qsTr("Web Dashboard"),
                                    icon: "M22 12A10 10 0 1 1 12 2a10 10 0 0 1 10 10z M12 2a15.3 15.3 0 0 1 4 10 15.3 15.3 0 0 1 -4 10 15.3 15.3 0 0 1 -4 -10 15.3 15.3 0 0 1 4 -10z M2 12h20",
                                    run: function() { root.remoteControlRequested(); }
                                },
                                {
                                    label: qsTr("Auto-Switch"),
                                    icon: "M13 2L3 14h9l-1 8 10 -12h-9l1 -8z",
                                    run: function() { root.autoSwitchRequested(); }
                                },
                                {
                                    label: qsTr("PWA Editor"),
                                    icon: "M5 2h14a2 2 0 0 1 2 2v16a2 2 0 0 1 -2 2H5a2 2 0 0 1 -2 -2V4a2 2 0 0 1 2 -2z M12 18h.01",
                                    run: function() { profileEditorViewModel.openPwaEditor(); }
                                },
                                {
                                    label: qsTr("Log Console"),
                                    icon: "M7 8l4 4 -4 4 M13 16h5",
                                    run: function() { mainViewModel.currentView = "LogConsole"; }
                                }
                            ]

                            delegate: Rectangle {
                                id: menuItemBg
                                width: toolsPopup.width - 8
                                height: 34
                                radius: Theme.radiusSmall
                                color: menuItemArea.containsMouse ? Theme.accent : "transparent"
                                Behavior on color { ColorAnimation { duration: Theme.animFast } }

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: Theme.spacingSm
                                    anchors.rightMargin: Theme.spacingSm
                                    spacing: Theme.spacingSm

                                    SvgIcon {
                                        Layout.preferredWidth: 16
                                        Layout.preferredHeight: 16
                                        pathData: modelData.icon
                                        color: menuItemArea.containsMouse ? Theme.crust : Theme.subtext0
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.label
                                        font.pixelSize: 12
                                        font.weight: Font.DemiBold
                                        font.capitalization: Font.AllUppercase
                                        font.letterSpacing: 1
                                        color: menuItemArea.containsMouse ? Theme.crust : Theme.text
                                    }
                                }

                                MouseArea {
                                    id: menuItemArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        toolsPopup.close();
                                        modelData.run();
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // --- Right: global Engine switch -----------------------------------
        // Ghost/dim when OFF, solid phosphor-LED when ON - the visual weight
        // now actually tracks the button's real-world stakes (this is the
        // one control that starts/stops the whole input router).
        Item {
            id: engineButton
            Layout.leftMargin: Theme.spacingLg
            Layout.preferredWidth: engineContent.implicitWidth + Theme.spacingMd * 2
            Layout.preferredHeight: 36

            readonly property bool isOn: engineViewModel.isEngineRunning

            scale: engineMouseArea.pressed ? 0.96 : (engineMouseArea.containsMouse ? 1.03 : 1.0)
            Behavior on scale { NumberAnimation { duration: 150; easing.type: Easing.OutBack } }

            Rectangle {
                id: engineBg
                anchors.fill: parent
                radius: Theme.radiusMedium
                color: engineButton.isOn ? root.engineOnColor : "transparent"
                border.width: 1
                border.color: engineButton.isOn ? root.engineOnColor : root.engineOffColor
                Behavior on color { ColorAnimation { duration: Theme.animMedium } }
                Behavior on border.color { ColorAnimation { duration: Theme.animMedium } }
            }

            // LED glow - only lit while the engine is actually running, so
            // the button reads like a powered indicator, not just a toggle.
            MultiEffect {
                source: engineBg
                anchors.fill: engineBg
                shadowEnabled: engineButton.isOn
                shadowColor: root.engineOnColor
                shadowBlur: 1.0
                blurMax: 32
                opacity: engineButton.isOn ? 0.85 : 0.0
                Behavior on opacity { NumberAnimation { duration: Theme.animMedium } }
            }

            RowLayout {
                id: engineContent
                anchors.centerIn: parent
                spacing: Theme.spacingSm

                Text {
                    text: "⏻" // power glyph.
                    font.pixelSize: 15
                    color: engineButton.isOn ? root.engineOnInk : root.engineOffColor
                    Behavior on color { ColorAnimation { duration: Theme.animMedium } }
                }
                Text {
                    text: engineButton.isOn ? qsTr("ENGINE: ON") : qsTr("ENGINE: OFF")
                    font.pixelSize: 12
                    font.weight: Font.Bold
                    color: engineButton.isOn ? root.engineOnInk : root.engineOffColor
                    Behavior on color { ColorAnimation { duration: Theme.animMedium } }
                }
            }

            MouseArea {
                id: engineMouseArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: engineViewModel.isEngineRunning = !engineViewModel.isEngineRunning
            }
        }
    }
}
