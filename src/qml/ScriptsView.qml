import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import GremblingNexus

// "Scripts" screen (Fase 19, Script Bridge, step 5/6; alias UI added Fase
// 19.6 step 2/6): CRUD list of configured Python scripts, each
// independently start/stop-able - backed by ScriptsViewModel
// (scripts_config.json persistence, one QProcess per running script). Only
// reachable via TopHeader's Tools menu when settingsViewModel.scriptsEnabled
// && settingsViewModel.scriptsModuleDetected are both true (see
// TopHeader.qml) - this view itself doesn't re-check that, so opening it any
// other way (e.g. hand-editing mainViewModel.currentView) would just show an
// empty/functional-but-pointless list.
//
// Each script row can expand to show its input aliases (physical device
// axis/button -> a name the script's own @bridge.on_axis(name)/on_button
// refers to) and output aliases (a name the script's own set_axis(name,
// value)/set_button refers to -> a channel of the shared "Nexus Scripts"
// virtual device). Editing these here only changes what's persisted/exposed
// via ScriptsViewModel - nothing yet in C++ actually forwards physical
// input to a running script or applies a script's output to "Nexus
// Scripts" (that's Fase 19.6 steps 3-4), so these aliases don't do anything
// observable in the rest of the app yet.
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
                            id: scriptDelegate
                            property var scriptData: modelData
                            property int scriptIndex: index
                            property bool expanded: false
                            // Re-fetched only while expanded (a plain
                            // Q_INVOKABLE snapshot, not a live Q_PROPERTY) -
                            // collapse/re-expand to pick up a device plugged
                            // in while this panel is open. Good enough for
                            // this first pass; not worth a live-updating
                            // model yet.
                            property var availableDevices: expanded ? scriptsViewModel.availableInputDevices() : []

                            Layout.fillWidth: true
                            implicitHeight: contentColumn.implicitHeight + Theme.spacingSm * 2
                            radius: Theme.radiusSmall
                            color: Theme.surface1

                            ColumnLayout {
                                id: contentColumn
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: Theme.spacingSm
                                spacing: Theme.spacingSm

                                RowLayout {
                                    id: scriptRow
                                    Layout.fillWidth: true
                                    spacing: Theme.spacingSm

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 1
                                        Text { text: scriptDelegate.scriptData.name; color: Theme.text; font.pixelSize: 13; font.weight: Font.DemiBold }
                                        Text {
                                            text: scriptDelegate.scriptData.scriptPath
                                            color: Theme.overlay0
                                            font.pixelSize: 11
                                            elide: Text.ElideMiddle
                                            Layout.fillWidth: true
                                        }
                                    }

                                    Badge {
                                        text: scriptDelegate.scriptData.status
                                        color: scriptDelegate.scriptData.status === "Running" ? Qt.rgba(Theme.success.r, Theme.success.g, Theme.success.b, 0.15)
                                             : scriptDelegate.scriptData.status === "Crashed" ? Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.15)
                                             : Theme.surface2
                                        border.color: scriptDelegate.scriptData.status === "Running" ? Theme.success
                                                    : scriptDelegate.scriptData.status === "Crashed" ? Theme.danger
                                                    : Theme.overlay0
                                    }

                                    ToolButton {
                                        label: qsTr("Aliases (%1 in, %2 out)").arg(scriptDelegate.scriptData.inputAliases.length).arg(scriptDelegate.scriptData.outputAliases.length)
                                        onClicked: scriptDelegate.expanded = !scriptDelegate.expanded
                                    }

                                    ToolButton {
                                        label: scriptDelegate.scriptData.status === "Running" ? qsTr("Stop") : qsTr("Start")
                                        onClicked: scriptDelegate.scriptData.status === "Running"
                                            ? scriptsViewModel.stopScript(scriptDelegate.scriptIndex)
                                            : scriptsViewModel.startScript(scriptDelegate.scriptIndex)
                                    }

                                    ToolButton {
                                        label: qsTr("×")
                                        onClicked: scriptsViewModel.removeScript(scriptDelegate.scriptIndex)
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    visible: scriptDelegate.expanded
                                    spacing: Theme.spacingSm

                                    Rectangle { Layout.fillWidth: true; height: 1; color: Qt.rgba(1, 1, 1, 0.08) }

                                    // --- Input aliases: physical device -> script ---
                                    Text { text: qsTr("Input aliases (physical device → script)"); color: Theme.subtext0; font.pixelSize: 11; font.weight: Font.DemiBold }

                                    Repeater {
                                        model: scriptDelegate.scriptData.inputAliases
                                        delegate: RowLayout {
                                            Layout.fillWidth: true
                                            spacing: Theme.spacingSm
                                            Text { text: modelData.name; color: Theme.text; font.pixelSize: 12; Layout.preferredWidth: 90; elide: Text.ElideRight }
                                            Text {
                                                text: modelData.devicePath + "  ·  " + (modelData.isAxis ? qsTr("Axis") : qsTr("Button")) + " " + modelData.channelIndex
                                                color: Theme.overlay0
                                                font.pixelSize: 11
                                                elide: Text.ElideMiddle
                                                Layout.fillWidth: true
                                            }
                                            ToolButton { label: qsTr("×"); onClicked: scriptsViewModel.removeInputAlias(scriptDelegate.scriptIndex, index) }
                                        }
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: Theme.spacingXs

                                        TextField {
                                            id: inAliasName
                                            Layout.preferredWidth: 90
                                            placeholderText: qsTr("name")
                                            color: Theme.text
                                            background: Rectangle { color: Theme.surface2; radius: Theme.radiusSmall; border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.08) }
                                        }
                                        AppComboBox {
                                            id: inDeviceCombo
                                            Layout.preferredWidth: 170
                                            model: scriptDelegate.availableDevices
                                            textRole: "deviceName"
                                        }
                                        AppComboBox {
                                            id: inKindCombo
                                            Layout.preferredWidth: 80
                                            model: [qsTr("Axis"), qsTr("Button")]
                                        }
                                        TextField {
                                            id: inChannelField
                                            Layout.preferredWidth: 44
                                            placeholderText: qsTr("#")
                                            color: Theme.text
                                            validator: IntValidator { bottom: 0; top: 255 }
                                            background: Rectangle { color: Theme.surface2; radius: Theme.radiusSmall; border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.08) }
                                        }
                                        GremblingButton {
                                            text: qsTr("Add")
                                            enabled: inAliasName.text.trim().length > 0 && inChannelField.text.length > 0 && inDeviceCombo.currentIndex >= 0 && scriptDelegate.availableDevices.length > 0
                                            onClicked: {
                                                const dev = scriptDelegate.availableDevices[inDeviceCombo.currentIndex]
                                                scriptsViewModel.addInputAlias(scriptDelegate.scriptIndex, inAliasName.text.trim(), dev.systemPath, parseInt(inChannelField.text), inKindCombo.currentIndex === 0)
                                                inAliasName.text = ""
                                                inChannelField.text = ""
                                            }
                                        }
                                    }

                                    // --- Output aliases: script -> "Nexus Scripts" ---
                                    Text { text: qsTr("Output aliases (script → Nexus Scripts)"); color: Theme.subtext0; font.pixelSize: 11; font.weight: Font.DemiBold }

                                    Repeater {
                                        model: scriptDelegate.scriptData.outputAliases
                                        delegate: RowLayout {
                                            Layout.fillWidth: true
                                            spacing: Theme.spacingSm
                                            Text { text: modelData.name; color: Theme.text; font.pixelSize: 12; Layout.preferredWidth: 90; elide: Text.ElideRight }
                                            Text {
                                                text: (modelData.isAxis ? qsTr("Axis") : qsTr("Button")) + " " + modelData.channelIndex
                                                color: Theme.overlay0
                                                font.pixelSize: 11
                                                Layout.fillWidth: true
                                            }
                                            ToolButton { label: qsTr("×"); onClicked: scriptsViewModel.removeOutputAlias(scriptDelegate.scriptIndex, index) }
                                        }
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: Theme.spacingXs

                                        TextField {
                                            id: outAliasName
                                            Layout.preferredWidth: 90
                                            placeholderText: qsTr("name")
                                            color: Theme.text
                                            background: Rectangle { color: Theme.surface2; radius: Theme.radiusSmall; border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.08) }
                                        }
                                        AppComboBox {
                                            id: outKindCombo
                                            Layout.preferredWidth: 80
                                            model: [qsTr("Axis"), qsTr("Button")]
                                        }
                                        TextField {
                                            id: outChannelField
                                            Layout.preferredWidth: 44
                                            placeholderText: qsTr("#")
                                            color: Theme.text
                                            validator: IntValidator { bottom: 0; top: 127 }
                                            background: Rectangle { color: Theme.surface2; radius: Theme.radiusSmall; border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.08) }
                                        }
                                        GremblingButton {
                                            text: qsTr("Add")
                                            enabled: outAliasName.text.trim().length > 0 && outChannelField.text.length > 0
                                            onClicked: {
                                                scriptsViewModel.addOutputAlias(scriptDelegate.scriptIndex, outAliasName.text.trim(), parseInt(outChannelField.text), outKindCombo.currentIndex === 0)
                                                outAliasName.text = ""
                                                outChannelField.text = ""
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
