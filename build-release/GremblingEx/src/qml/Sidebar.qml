import QtQuick
import QtQuick.Layouts
import GremblingEx

// Left navigation rail (Phase 10 Part 1): branding header + one
// SidebarButton per top-level section. Selection state lives in
// mainViewModel.currentView (a plain C++ context property), not locally -
// so any future screen can also read/drive which section is active.
Rectangle {
    id: root
    color: Theme.mantle

    Rectangle {
        // Subtle 1px seam separating the rail from the content area -
        // deliberately not a hard black line, just one step darker than
        // the rail itself, consistent with the elevation-based palette.
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: Theme.surface0
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: Theme.spacingLg
        anchors.bottomMargin: Theme.spacingLg
        spacing: Theme.spacingXs

        ColumnLayout {
            Layout.leftMargin: Theme.spacingMd
            Layout.bottomMargin: Theme.spacingLg
            spacing: 2

            Text {
                text: "GremblingEx"
                color: Theme.text
                font.pixelSize: 19
                font.weight: Font.Bold
            }
            Text {
                text: "Input Engine"
                color: Theme.overlay0
                font.pixelSize: 11
            }
        }

        SidebarButton {
            Layout.fillWidth: true
            label: "Profiles"
            iconGlyph: "▣" // filled square outline - "documents/collection".
            selected: mainViewModel.currentView === "Profiles"
            onClicked: mainViewModel.currentView = "Profiles"
        }
        SidebarButton {
            Layout.fillWidth: true
            label: "Curves"
            iconGlyph: "∿" // sine wave - stands in for a response curve.
            selected: mainViewModel.currentView === "Curves"
            onClicked: mainViewModel.currentView = "Curves"
        }
        SidebarButton {
            Layout.fillWidth: true
            label: "Device Tester"
            iconGlyph: "◉" // fisheye/target - live monitoring.
            selected: mainViewModel.currentView === "DeviceTester"
            onClicked: mainViewModel.currentView = "DeviceTester"
        }
        SidebarButton {
            Layout.fillWidth: true
            label: "Settings"
            iconGlyph: "⚙" // gear.
            selected: mainViewModel.currentView === "Settings"
            onClicked: mainViewModel.currentView = "Settings"
        }
        SidebarButton {
            Layout.fillWidth: true
            label: "Log Console"
            iconGlyph: "▤" // lines - stands in for a log/console listing.
            selected: mainViewModel.currentView === "LogConsole"
            onClicked: mainViewModel.currentView = "LogConsole"
        }

        Item { Layout.fillHeight: true } // Pushes the entries above to the top.

        // Global "Engine" switch (Fase 10.7): starts/stops EventRouter's
        // whole input->output pipeline via engineViewModel, independent of
        // which screen is currently showing. Subtle/surface-colored when
        // off, strong green when on - deliberately the loudest element in
        // the rail, since it is the one control that decides whether any
        // physical input reaches a virtual device at all.
        Rectangle {
            id: engineButton
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingMd
            Layout.rightMargin: Theme.spacingMd
            Layout.preferredHeight: 52
            radius: Theme.radiusMedium
            color: engineViewModel.isEngineRunning
                ? Theme.success
                : (engineArea.containsMouse ? Theme.surface1 : Theme.surface0)
            border.width: 1
            border.color: engineViewModel.isEngineRunning ? Theme.success : Qt.rgba(1, 1, 1, 0.08)

            Behavior on color { ColorAnimation { duration: Theme.animMedium } }

            RowLayout {
                anchors.centerIn: parent
                spacing: Theme.spacingSm

                Text {
                    text: "⏻" // power glyph.
                    color: engineViewModel.isEngineRunning ? Theme.crust : Theme.subtext0
                    font.pixelSize: 16
                }
                Text {
                    text: engineViewModel.isEngineRunning ? "Engine: ON" : "Engine: OFF"
                    color: engineViewModel.isEngineRunning ? Theme.crust : Theme.subtext0
                    font.pixelSize: 13
                    font.weight: Font.Bold
                }
            }

            MouseArea {
                id: engineArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: engineViewModel.isEngineRunning = !engineViewModel.isEngineRunning
            }
        }
    }
}
