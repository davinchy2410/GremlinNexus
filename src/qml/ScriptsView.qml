import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import GremblingNexus

// "Scripts" screen (Fase 19, Script Bridge, step 5/6): CRUD list of
// configured Python scripts, each independently start/stop-able - backed by
// ScriptsViewModel (scripts_config.json persistence, one QProcess per
// running script). Only reachable via TopHeader's Tools menu when
// settingsViewModel.scriptsEnabled && settingsViewModel.scriptsModuleDetected
// are both true (see TopHeader.qml) - this view itself doesn't re-check
// that, so opening it any other way (e.g. hand-editing mainViewModel.
// currentView) would just show an empty/functional-but-pointless list.
//
// Deliberately no per-script input/output alias configuration UI yet (which
// physical axis/button a script's own named input means, or which channel
// of "Nexus Scripts" its output lands on) - see ScriptsViewModel's own
// class docs for why that's a separate follow-up.
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
                text: qsTr("Scripts (Beta)")
                color: Theme.text
                font.pixelSize: 24
                font.weight: Font.DemiBold
            }
            Text {
                text: qsTr("Run external Python scripts alongside Nexus")
                color: Theme.subtext0
                font.pixelSize: 13
            }
        }

        // --- Add Script -----------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            radius: Theme.radiusMedium
            color: Theme.surface0
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.06)
            implicitHeight: addRow.implicitHeight + Theme.spacingMd * 2

            RowLayout {
                id: addRow
                anchors.fill: parent
                anchors.margins: Theme.spacingMd
                spacing: Theme.spacingSm

                TextField {
                    id: nameField
                    Layout.preferredWidth: 160
                    placeholderText: qsTr("Script name")
                    color: Theme.text
                    background: Rectangle {
                        color: Theme.surface1; radius: Theme.radiusSmall
                        border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.08)
                    }
                }

                TextField {
                    id: pathField
                    Layout.fillWidth: true
                    readOnly: true
                    placeholderText: qsTr("No file selected")
                    color: Theme.text
                    background: Rectangle {
                        color: Theme.surface1; radius: Theme.radiusSmall
                        border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.08)
                    }
                }

                ToolButton {
                    label: qsTr("Browse…")
                    onClicked: scriptFileDialog.open()
                }

                GremblingButton {
                    text: qsTr("Add")
                    enabled: nameField.text.trim().length > 0 && pathField.text.trim().length > 0
                    onClicked: {
                        scriptsViewModel.addScript(nameField.text.trim(), pathField.text)
                        nameField.text = ""
                        pathField.text = ""
                    }
                }
            }
        }

        FileDialog {
            id: scriptFileDialog
            title: qsTr("Select Python Script")
            fileMode: FileDialog.OpenFile
            nameFilters: ["Python scripts (*.py)"]
            onAccepted: pathField.text = selectedFile
        }

        // --- Script list -------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.radiusMedium
            color: Theme.surface0
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.06)
            clip: true

            Text {
                anchors.centerIn: parent
                visible: scriptsViewModel.scripts.length === 0
                text: qsTr("No scripts configured yet")
                color: Theme.subtext0
                font.pixelSize: 13
            }

            ScrollView {
                anchors.fill: parent
                anchors.margins: Theme.spacingSm
                clip: true
                visible: scriptsViewModel.scripts.length > 0

                ColumnLayout {
                    width: parent.width
                    spacing: Theme.spacingXs

                    Repeater {
                        model: scriptsViewModel.scripts

                        delegate: Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: scriptRow.implicitHeight + Theme.spacingSm * 2
                            radius: Theme.radiusSmall
                            color: Theme.surface1

                            RowLayout {
                                id: scriptRow
                                anchors.fill: parent
                                anchors.margins: Theme.spacingSm
                                spacing: Theme.spacingSm

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 1
                                    Text { text: modelData.name; color: Theme.text; font.pixelSize: 13; font.weight: Font.DemiBold }
                                    Text {
                                        text: modelData.scriptPath
                                        color: Theme.overlay0
                                        font.pixelSize: 11
                                        elide: Text.ElideMiddle
                                        Layout.fillWidth: true
                                    }
                                }

                                Badge {
                                    text: modelData.status
                                    color: modelData.status === "Running" ? Qt.rgba(Theme.success.r, Theme.success.g, Theme.success.b, 0.15)
                                         : modelData.status === "Crashed" ? Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.15)
                                         : Theme.surface2
                                    border.color: modelData.status === "Running" ? Theme.success
                                                : modelData.status === "Crashed" ? Theme.danger
                                                : Theme.overlay0
                                }

                                ToolButton {
                                    label: modelData.status === "Running" ? qsTr("Stop") : qsTr("Start")
                                    onClicked: modelData.status === "Running"
                                        ? scriptsViewModel.stopScript(index)
                                        : scriptsViewModel.startScript(index)
                                }

                                ToolButton {
                                    label: qsTr("×")
                                    onClicked: scriptsViewModel.removeScript(index)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
