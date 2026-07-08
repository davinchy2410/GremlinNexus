import QtQuick
import QtQuick.Layouts
import QtQuick.Effects
import QtQuick.Window
import GremblingEx

// Top navigation bar (Fase 15.5): replaces the left Sidebar rail with a
// fixed-height horizontal header - branding on the left, the section tabs
// centered, and the global Engine switch on the right. Selection state
// still lives in mainViewModel.currentView, same as the old Sidebar.
Rectangle {
    id: root
    color: Theme.mantle
    implicitHeight: 64

    /// Bubbles up to main.qml, which owns the actual RemoteControlPopup
    /// instance - same signal Sidebar used to expose.
    signal remoteControlRequested()
    signal autoSwitchRequested()

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
            Window.window.visibility = Window.window.visibility === Window.Maximized
                ? Window.Windowed : Window.Maximized;
        }
    }

    // Fase 12: anchored independently of the RowLayout below (rather than
    // being one of its children) and flush against the top-right corner,
    // matching every native title bar's own convention - if it lived inside
    // the RowLayout instead, this header's own tab overflow (8 section tabs
    // plus branding plus the Engine switch already push close to the
    // window's width) could shove it past the window's right edge, making
    // "Close" impossible to reach without resizing first.
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

        // Fase 14: hard safety net - however wide the center tabs group
        // wants to be, nothing in this RowLayout may render past its own
        // anchored bounds (which stop at windowControls.left). Without this,
        // a RowLayout whose children can't shrink below their own implicit
        // width (every TopHeaderButton/GremblingButton here) just overflows
        // past its anchor when there isn't enough room - which is exactly
        // what let the Engine switch visually render underneath, and steal
        // clicks from, the minimize/maximize/close buttons once a 9th tab
        // (Quick Save) pushed this bar's natural width past the window's.
        clip: true

        // --- Left: branding ----------------------------------------------
        RowLayout {
            spacing: Theme.spacingSm

            Item {
                Layout.preferredWidth: 22
                Layout.preferredHeight: 22

                Image {
                    id: logoIcon
                    anchors.fill: parent
                    source: "icons/engine.svg"
                    sourceSize: Qt.size(22, 22)
                    smooth: true
                }
                MultiEffect {
                    source: logoIcon
                    anchors.fill: logoIcon
                    colorization: 1.0
                    colorizationColor: Theme.accent
                }
            }

            ColumnLayout {
                spacing: 0
                Text { text: "GremblingEx"; color: Theme.text; font.pixelSize: 16; font.weight: Font.Bold }
                Text { text: "Input Engine"; color: Theme.overlay0; font.pixelSize: 10 }
            }
        }

        // --- Center: section tabs ------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignHCenter
            spacing: 2

            TopHeaderButton {
                label: "Profiles"
                iconSource: "icons/profiles.svg"
                selected: mainViewModel.currentView === "Profiles"
                onClicked: mainViewModel.currentView = "Profiles"
            }
            TopHeaderButton {
                label: "Curves"
                iconSource: "icons/curves.svg"
                selected: mainViewModel.currentView === "Curves"
                onClicked: mainViewModel.currentView = "Curves"
            }
            TopHeaderButton {
                label: "Device Tester"
                iconSource: "icons/tester.svg"
                selected: mainViewModel.currentView === "DeviceTester"
                onClicked: mainViewModel.currentView = "DeviceTester"
            }
            TopHeaderButton {
                label: "Web Dashboard"
                iconSource: "icons/web.svg"
                selected: false
                onClicked: root.remoteControlRequested()
            }
            TopHeaderButton {
                label: "Auto-Switch"
                iconSource: "icons/profiles.svg" // Reuse profiles icon or similar
                selected: false
                onClicked: root.autoSwitchRequested()
            }
            TopHeaderButton {
                label: "PWA Editor"
                iconSource: "icons/web.svg" // Reuse Web Dashboard icon - both are PWA-facing.
                selected: false
                onClicked: profileEditorViewModel.openPwaEditor()
            }
            TopHeaderButton {
                label: "Quick Save"
                iconSource: "icons/profiles.svg" // Reuse the Profiles icon as a placeholder.
                selected: false
                onClicked: profileEditorViewModel.quickSave()
            }
            TopHeaderButton {
                label: "Settings"
                iconSource: "icons/settings.svg"
                selected: mainViewModel.currentView === "Settings"
                onClicked: mainViewModel.currentView = "Settings"
            }
            TopHeaderButton {
                label: "Log Console"
                iconSource: "icons/log.svg"
                selected: mainViewModel.currentView === "LogConsole"
                onClicked: mainViewModel.currentView = "LogConsole"
            }
        }

        // --- Right: global Engine switch ----------------------------------
        GremblingButton {
            id: engineButton
            // Fase 14: trimmed from 150 - with 9 tabs now in the center
            // group, 150 didn't leave enough room before this bar's own
            // clip: true safety net (see above) started cutting this
            // button's own right edge off.
            Layout.preferredWidth: 130
            Layout.preferredHeight: 40
            
            accentColor: engineViewModel.isEngineRunning ? Theme.success : Theme.overlay0
            accentHoverColor: engineViewModel.isEngineRunning ? Theme.success : Theme.text
            
            // We trick the GremblingButton content Item to show our custom engine text
            contentItem: RowLayout {
                spacing: Theme.spacingSm
                Text {
                    text: "⏻" // power glyph.
                    color: engineViewModel.isEngineRunning ? Theme.success : Theme.subtext0
                    font.pixelSize: 15
                }
                Text {
                    text: engineViewModel.isEngineRunning ? "ENGINE: ON" : "ENGINE: OFF"
                    color: engineViewModel.isEngineRunning ? Theme.success : Theme.subtext0
                    font.pixelSize: 12
                    font.weight: Font.Bold
                }
            }

            onClicked: engineViewModel.isEngineRunning = !engineViewModel.isEngineRunning
        }
    }
}
