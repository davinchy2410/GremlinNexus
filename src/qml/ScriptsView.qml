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

    // Which scripts' alias panels are expanded, keyed by script name - kept
    // here rather than as local per-delegate state, because
    // scriptsViewModel.scripts is a plain QVariantList Q_PROPERTY (not a
    // real QAbstractListModel), so every scriptsChanged() emission - which
    // addInputAlias()/addOutputAlias() themselves trigger - makes the
    // Repeater below rebuild every delegate from scratch. A property local
    // to the delegate would reset to false right as you added an alias,
    // collapsing the panel you were just typing into.
    property var expandedScripts: ({})

    function toggleExpanded(scriptName) {
        const copy = Object.assign({}, root.expandedScripts)
        copy[scriptName] = !copy[scriptName]
        root.expandedScripts = copy
    }

    // "Nexus Scripts" has no real per-channel meaning (unlike a physical
    // device's axes/buttons, it's just a fixed bank of generic slots), so
    // output aliases just get plain numbered names - matches capacity
    // registered in main.cpp (numAxes: 8, numButtons: 128).
    function outputChannelNames(isAxis) {
        const count = isAxis ? 8 : 128
        const names = []
        for (let i = 1; i <= count; ++i) {
            names.push((isAxis ? qsTr("Axis %1") : qsTr("Button %1")).arg(i))
        }
        return names
    }

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

                ToolButton {
                    label: qsTr("View code")
                    enabled: pathField.text.trim().length > 0
                    onClicked: sourcePopup.openFor(pathField.text, nameField.text.trim().length > 0 ? nameField.text.trim() : qsTr("Selected script"))
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
                            readonly property bool expanded: root.expandedScripts[scriptData.name] === true
                            // Re-fetched whenever expanded, or whenever
                            // deviceListVersion changes while already
                            // expanded (a device connecting/disconnecting) -
                            // see ScriptsViewModel::deviceListVersion's docs.
                            property var availableDevices: expanded ? (scriptsViewModel.deviceListVersion, scriptsViewModel.availableInputDevices()) : []

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
                                        label: qsTr("View code")
                                        onClicked: sourcePopup.openFor(scriptDelegate.scriptData.scriptPath, scriptDelegate.scriptData.name)
                                    }

                                    ToolButton {
                                        label: qsTr("Aliases (%1 in, %2 out)").arg(scriptDelegate.scriptData.inputAliases.length).arg(scriptDelegate.scriptData.outputAliases.length)
                                        onClicked: root.toggleExpanded(scriptDelegate.scriptData.name)
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

                                    Text {
                                        text: qsTr("The name below must match exactly what the script's own code uses (e.g. @bridge.on_axis(\"rudder\")) - use \"View code\" above to check.")
                                        color: Theme.overlay0
                                        font.pixelSize: 10
                                        font.italic: true
                                        wrapMode: Text.WordWrap
                                        Layout.fillWidth: true
                                    }

                                    // --- Input aliases: physical device -> script ---
                                    Text { text: qsTr("Input aliases (physical device → script)"); color: Theme.subtext0; font.pixelSize: 11; font.weight: Font.DemiBold }

                                    Repeater {
                                        model: scriptDelegate.scriptData.inputAliases
                                        delegate: RowLayout {
                                            Layout.fillWidth: true
                                            spacing: Theme.spacingSm
                                            Text { text: modelData.name; color: Theme.text; font.pixelSize: 12; Layout.preferredWidth: 90; elide: Text.ElideRight }
                                            Text {
                                                // scriptsViewModel.deviceListVersion is read here purely so this
                                                // binding re-evaluates once DeviceManager's device list changes
                                                // (e.g. finishes its startup scan, or a device reconnects) - the
                                                // Q_INVOKABLE calls below have no NOTIFY signal of their own for
                                                // QML to track, so without this the device name/channel shown
                                                // could get stuck on "not connected yet" from the very first
                                                // paint. See ScriptsViewModel::deviceListVersion's own docs.
                                                readonly property int _deviceListVersion: scriptsViewModel.deviceListVersion
                                                readonly property bool connected: scriptsViewModel.isDeviceConnected(modelData.devicePath)
                                                readonly property var channelNames: scriptsViewModel.channelNamesForDevice(modelData.devicePath, modelData.isAxis)
                                                text: (connected ? "" : qsTr("[Offline] "))
                                                    + scriptsViewModel.deviceDisplayName(modelData.devicePath) + "  ·  "
                                                    + (modelData.channelIndex < channelNames.length
                                                       ? channelNames[modelData.channelIndex]
                                                       : (modelData.isAxis ? qsTr("Axis") : qsTr("Button")) + " " + modelData.channelIndex)
                                                // Same "offline = danger-toned" convention Profiles' own device
                                                // tree uses for a disconnected/orphaned device row.
                                                color: connected ? Theme.overlay0 : Theme.danger
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
                                        AppComboBox {
                                            id: inChannelCombo
                                            Layout.preferredWidth: 170
                                            // Re-evaluates whenever inDeviceCombo/inKindCombo's currentIndex
                                            // changes (both referenced directly here, so QML tracks them) -
                                            // same names Profiles' own device tree shows (see InputNaming.h).
                                            model: (inDeviceCombo.currentIndex >= 0 && inDeviceCombo.currentIndex < scriptDelegate.availableDevices.length)
                                                ? scriptsViewModel.channelNamesForDevice(scriptDelegate.availableDevices[inDeviceCombo.currentIndex].systemPath, inKindCombo.currentIndex === 0)
                                                : []
                                        }
                                        GremblingButton {
                                            text: qsTr("Add")
                                            enabled: inAliasName.text.trim().length > 0 && inChannelCombo.currentIndex >= 0 && inChannelCombo.count > 0 && inDeviceCombo.currentIndex >= 0 && scriptDelegate.availableDevices.length > 0
                                            onClicked: {
                                                const dev = scriptDelegate.availableDevices[inDeviceCombo.currentIndex]
                                                scriptsViewModel.addInputAlias(scriptDelegate.scriptIndex, inAliasName.text.trim(), dev.systemPath, inChannelCombo.currentIndex, inKindCombo.currentIndex === 0)
                                                inAliasName.text = ""
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
                                                readonly property var channelName: root.outputChannelNames(modelData.isAxis)[modelData.channelIndex]
                                                text: channelName !== undefined ? channelName : (modelData.isAxis ? qsTr("Axis") : qsTr("Button")) + " " + modelData.channelIndex
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
                                        AppComboBox {
                                            id: outChannelCombo
                                            Layout.preferredWidth: 150
                                            model: root.outputChannelNames(outKindCombo.currentIndex === 0)
                                        }
                                        GremblingButton {
                                            text: qsTr("Add")
                                            enabled: outAliasName.text.trim().length > 0 && outChannelCombo.currentIndex >= 0
                                            onClicked: {
                                                scriptsViewModel.addOutputAlias(scriptDelegate.scriptIndex, outAliasName.text.trim(), outChannelCombo.currentIndex, outKindCombo.currentIndex === 0)
                                                outAliasName.text = ""
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

    // Read-only source preview (Fase 19.6): nothing sandboxes what a
    // script can do once started (see this project's own README on the
    // trust model), so this is the cheap mitigation - see what a script
    // actually does before running it. Re-reads the file fresh every time
    // it opens (readScriptSource() never caches), so there's deliberately
    // no "Refresh" button - closing and reopening already shows the
    // current file.
    Popup {
        id: sourcePopup
        modal: true
        focus: true
        parent: Overlay.overlay
        x: parent ? Math.round((parent.width - width) / 2) : 0
        y: parent ? Math.round((parent.height - height) / 2) : 0
        width: parent ? Math.min(parent.width - 80, 980) : 980
        height: parent ? Math.min(parent.height - 80, 720) : 720
        padding: 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        property string sourceText: ""
        property string sourceTitle: ""

        function openFor(path, title) {
            sourcePopup.sourceTitle = title
            const text = scriptsViewModel.readScriptSource(path)
            sourcePopup.sourceText = text.length > 0 ? text : qsTr("(could not read this file)")
            sourcePopup.open()
        }

        background: Rectangle {
            color: Theme.surface0
            radius: Theme.radiusMedium
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.08)
        }

        contentItem: ColumnLayout {
            spacing: Theme.spacingSm

            Text {
                Layout.topMargin: Theme.spacingMd
                Layout.leftMargin: Theme.spacingLg
                Layout.rightMargin: Theme.spacingLg
                Layout.fillWidth: true
                text: sourcePopup.sourceTitle
                color: Theme.text
                font.pixelSize: 15
                font.weight: Font.DemiBold
                elide: Text.ElideMiddle
            }

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: Theme.spacingLg
                Layout.rightMargin: Theme.spacingLg
                clip: true

                TextArea {
                    readOnly: true
                    selectByMouse: true
                    text: sourcePopup.sourceText
                    color: Theme.text
                    font.family: "Consolas"
                    font.pixelSize: 12
                    wrapMode: TextEdit.NoWrap
                    background: Item {}
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.spacingLg
                Layout.rightMargin: Theme.spacingLg
                Layout.bottomMargin: Theme.spacingMd

                Item { Layout.fillWidth: true }
                GremblingButton {
                    text: qsTr("Close")
                    onClicked: sourcePopup.close()
                }
            }
        }
    }
}
