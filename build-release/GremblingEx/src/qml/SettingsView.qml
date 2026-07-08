import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GremblingEx

// "Settings" screen (Fase 10.9 mock-up, wired to SettingsViewModel in Fase
// 13): three glassmorphism cards - vJoy status, HidHide integration, and
// app-wide preferences (run on Windows startup, Auto-Switch).
Item {
    id: root

    Rectangle {
        anchors.fill: parent
        color: Theme.base
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingLg

        ColumnLayout {
            spacing: 2
            Text {
                text: "Settings"
                color: Theme.text
                font.pixelSize: 24
                font.weight: Font.DemiBold
            }
            Text {
                text: "Application preferences"
                color: Theme.subtext0
                font.pixelSize: 13
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacingLg

            // --- vJoy Status Panel -------------------------------------
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                GlassPanel {
                    anchors.fill: parent
                    cornerRadius: Theme.radiusLarge
                    bgOpacity: 0.7
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingLg
                    spacing: Theme.spacingMd

                    Text { text: "vJoy Status"; color: Theme.text; font.pixelSize: 17; font.weight: Font.DemiBold }

                    ColumnLayout {
                        spacing: Theme.spacingSm
                        Layout.topMargin: Theme.spacingXs

                        RowLayout {
                            spacing: Theme.spacingSm
                            Rectangle { width: 8; height: 8; radius: 4; color: Theme.success }
                            Text { text: "Driver Active"; color: Theme.text; font.pixelSize: 13 }
                        }
                        Text { text: "Version: vJoy 2.1.9.1"; color: Theme.subtext0; font.pixelSize: 12 }
                        Text { text: "Devices registered: 16 (IDs 1-16)"; color: Theme.subtext0; font.pixelSize: 12 }
                    }

                    Item { Layout.fillHeight: true }

                    GremblingButton {
                        text: "Reset vJoy"
                        accentColor: Theme.danger
                        accentHoverColor: Theme.danger
                        Layout.alignment: Qt.AlignLeft
                        onClicked: settingsViewModel.resetVJoy()
                    }
                }
            }

            // --- HidHide Integration Panel ------------------------------
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                GlassPanel {
                    anchors.fill: parent
                    cornerRadius: Theme.radiusLarge
                    bgOpacity: 0.7
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingLg
                    spacing: Theme.spacingMd

                    Text { text: "HidHide Integration"; color: Theme.text; font.pixelSize: 17; font.weight: Font.DemiBold }

                    RowLayout {
                        spacing: Theme.spacingSm
                        Rectangle {
                            width: 8; height: 8; radius: 4
                            color: settingsViewModel.isHidHideInstalled ? Theme.success : Theme.overlay0
                        }
                        Text {
                            text: settingsViewModel.isHidHideInstalled ? "HidHideCLI detected" : "HidHideCLI not installed"
                            color: Theme.text
                            font.pixelSize: 13
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.spacingXs
                        spacing: Theme.spacingMd

                        Text { text: "Global Cloaking"; color: Theme.text; font.pixelSize: 13; Layout.fillWidth: true }
                        ToggleSwitch {
                            checked: settingsViewModel.globalCloakEnabled
                            onToggled: (v) => settingsViewModel.globalCloakEnabled = v
                        }
                    }

                    Item { Layout.fillHeight: true }

                    GremblingButton {
                        text: "Re-Whitelist GremblingEx"
                        Layout.alignment: Qt.AlignLeft
                        onClicked: settingsViewModel.whitelistApplication()
                    }
                }
            }

            // --- Application Preferences Panel --------------------------
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                GlassPanel {
                    anchors.fill: parent
                    cornerRadius: Theme.radiusLarge
                    bgOpacity: 0.7
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingLg
                    spacing: Theme.spacingMd

                    Text { text: "Application Preferences"; color: Theme.text; font.pixelSize: 17; font.weight: Font.DemiBold }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.spacingXs
                        spacing: Theme.spacingMd

                        Text { text: "Run on Windows Startup"; color: Theme.text; font.pixelSize: 13; Layout.fillWidth: true }
                        ToggleSwitch {
                            checked: settingsViewModel.runOnStartup
                            onToggled: (v) => settingsViewModel.runOnStartup = v
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingMd

                        Text { text: "Auto-Switch Profiles"; color: Theme.text; font.pixelSize: 13; Layout.fillWidth: true }
                        ToggleSwitch {
                            checked: settingsViewModel.autoSwitchEnabled
                            onToggled: (v) => settingsViewModel.autoSwitchEnabled = v
                        }
                    }

                    Item { Layout.fillHeight: true }
                }
            }
        }
    }
}
