import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import GremblingEx

Popup {
    id: root

    modal: true
    focus: true
    x: Overlay.overlay ? (Overlay.overlay.width - width) / 2 : 0
    y: Overlay.overlay ? (Overlay.overlay.height - height) / 2 : 0
    width: 600
    height: 450
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    property var rulesMap: profileEditorViewModel.autoSwitchRules()

    function refreshRules() {
        rulesMap = profileEditorViewModel.autoSwitchRules()
    }

    onOpened: {
        refreshRules()
    }

    // Fase 14: GlassPanel standardizes the Rectangle+MultiEffect glass pair
    // every screen was hand-duplicating - borderColor/shadowColor overridden
    // to Theme.accent here (rather than left at GlassPanel's neutral
    // default) to keep this popup's own distinct neon-glow look.
    background: GlassPanel {
        borderColor: Theme.accent
        shadowColor: Theme.accent
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacingMd

        Item { Layout.preferredHeight: Theme.spacingSm }

        // Header
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            spacing: 2

            Text { text: "Auto-Switch Profiles"; color: Theme.text; font.pixelSize: 17; font.weight: Font.DemiBold }
            Text {
                text: "Automatically switch profiles when a specific application is in focus."
                color: Theme.subtext0
                font.pixelSize: 12
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.surface1
        }

        // Default Profile
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg

            Text {
                text: "Default Profile:"
                color: Theme.subtext0
                font.pixelSize: 13
            }
            Text {
                text: profileEditorViewModel.autoSwitchDefaultProfile() || "None (Stay on current)"
                color: Theme.text
                font.pixelSize: 13
                Layout.fillWidth: true
                elide: Text.ElideLeft
            }

            ToolButton {
                label: "Set Default"
                onClicked: defaultProfileDialog.open()
            }
            ToolButton {
                label: "Clear"
                visible: profileEditorViewModel.autoSwitchDefaultProfile() !== ""
                onClicked: {
                    profileEditorViewModel.setAutoSwitchDefaultProfile("")
                    root.refreshRules()
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.surface1
        }

        // Rules List
        ListView {
            id: rulesList
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            clip: true

            model: Object.keys(root.rulesMap)

            delegate: Rectangle {
                width: ListView.view.width
                height: 40
                color: index % 2 === 0 ? Theme.surface0 : "transparent"
                radius: Theme.radiusSmall

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingSm

                    Text {
                        text: modelData
                        color: Theme.accent
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        Layout.preferredWidth: 150
                    }
                    Text {
                        text: root.rulesMap[modelData]
                        color: Theme.subtext0
                        font.pixelSize: 12
                        Layout.fillWidth: true
                        elide: Text.ElideLeft
                    }
                    ToolButton {
                        label: "Remove"
                        onClicked: {
                            profileEditorViewModel.removeAutoSwitchRule(modelData)
                            root.refreshRules()
                        }
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                text: "No rules added yet."
                color: Theme.surface2
                font.pixelSize: 14
                visible: parent.count === 0
            }
        }

        // Add Rule
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            Layout.bottomMargin: Theme.spacingLg
            spacing: Theme.spacingSm

            Rectangle {
                Layout.preferredWidth: 150
                Layout.preferredHeight: 32
                color: Theme.mantle
                radius: Theme.radiusSmall
                border.width: 1
                border.color: exeInput.activeFocus ? Theme.accent : Theme.surface2

                TextInput {
                    id: exeInput
                    anchors.fill: parent
                    anchors.margins: Theme.spacingSm
                    color: Theme.text
                    font.pixelSize: 13
                    verticalAlignment: TextInput.AlignVCenter
                    clip: true
                    Text {
                        anchors.fill: parent
                        text: "e.g. dcs.exe"
                        color: Theme.surface2
                        visible: !exeInput.text && !exeInput.activeFocus
                        verticalAlignment: TextInput.AlignVCenter
                    }
                }
            }

            ToolButton {
                label: "Select Profile..."
                onClicked: ruleProfileDialog.open()
            }
        }
    }

    FileDialog {
        id: defaultProfileDialog
        title: "Select Default Profile"
        nameFilters: ["GremblingEx Profiles (*.json)"]
        fileMode: FileDialog.OpenFile
        onAccepted: {
            profileEditorViewModel.setAutoSwitchDefaultProfile(selectedFile.toString().replace("file:///", ""))
            root.refreshRules()
        }
    }

    FileDialog {
        id: ruleProfileDialog
        title: "Select Profile for Rule"
        nameFilters: ["GremblingEx Profiles (*.json)"]
        fileMode: FileDialog.OpenFile
        onAccepted: {
            if (exeInput.text.trim() !== "") {
                profileEditorViewModel.addAutoSwitchRule(exeInput.text.trim(), selectedFile.toString().replace("file:///", ""))
                exeInput.text = ""
                root.refreshRules()
            }
        }
    }
}
