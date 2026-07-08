import QtCore
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import GremblingNexus

// "Star Citizen" screen (Fase SC-3): imports a Star Citizen actionmaps.xml
// export (via the SCManager singleton, backed by SCIntegrationManager) and
// cross-references each of its bindings against this session's *live*
// Grembling Nexus profile (via profileEditorViewModel.resolveSCInput(), Fase
// SC-2), so the user can see at a glance which physical hardware input
// actually drives each in-game action.
Item {
    id: root

    /// *.xml files found in folderField's current path - refreshed by
    /// refreshXmlFiles(), not a live filesystem watch, since the folder
    /// picker/"Load XML" flow below is an explicit user action, not
    /// something that needs to react to files appearing mid-session.
    property var xmlFiles: []

    Settings {
        id: scSettings
        category: "StarCitizen"
        property string lastSelectedXml: ""
    }

    function refreshXmlFiles() {
        root.xmlFiles = SCManager.getAvailableXmlFiles(folderField.text);
        const idx = root.xmlFiles.indexOf(scSettings.lastSelectedXml);
        if (idx !== -1) {
            xmlFileCombo.currentIndex = idx;
        } else {
            xmlFileCombo.currentIndex = 0;
            if (root.xmlFiles.length > 0) {
                scSettings.lastSelectedXml = root.xmlFiles[0];
            }
        }
    }

    /// Fase SC-5: safely joins a folder path and a file name. folderField's
    /// text is often pasted/typed with Windows-style backslashes (or a mix
    /// of both) - normalizing to forward slashes and trimming any trailing
    /// separator before concatenating avoids the "C:\folder/file.xml" or
    /// missing/doubled-separator paths that silently failed to open.
    function joinPath(dir, fileName) {
        let normalizedDir = dir.replace(/\\/g, "/");
        if (normalizedDir.endsWith("/")) {
            normalizedDir = normalizedDir.slice(0, -1);
        }
        return normalizedDir + "/" + fileName;
    }

    Component.onCompleted: {
        const detected = SCManager.detectMappingsFolder();
        if (detected !== "") {
            folderField.text = detected;
        }
        refreshXmlFiles();
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.base
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingMd

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMd

            ColumnLayout {
                spacing: 2
                Text {
                    text: qsTr("Star Citizen")
                    color: Theme.text
                    font.pixelSize: 24
                    font.weight: Font.DemiBold
                }
                Text {
                    text: qsTr("Import your Star Citizen control mapping (actionmaps.xml) and compare it against your Grembling Nexus profile")
                    color: Theme.subtext0
                    font.pixelSize: 13
                }
            }

            Item { Layout.fillWidth: true }

            ToolButton {
                label: qsTr("Back to Profiles")
                onClicked: mainViewModel.currentView = "Profiles"
            }
        }

        // --- Mappings folder -----------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: qsTr("Mappings Folder"); color: Theme.subtext0; font.pixelSize: 11 }
                TextField {
                    id: folderField
                    Layout.fillWidth: true
                    placeholderText: qsTr("C:\\...\\StarCitizen\\LIVE\\user\\client\\0\\controls\\mappings")
                    color: Theme.text
                    font.pixelSize: 13
                    background: Rectangle {
                        color: Theme.surface1
                        radius: Theme.radiusSmall
                        border.width: 1
                        border.color: Qt.rgba(1, 1, 1, 0.08)
                    }
                    onEditingFinished: root.refreshXmlFiles()
                }
            }

            ToolButton {
                label: qsTr("Auto-Detect")
                onClicked: {
                    const detected = SCManager.detectMappingsFolder();
                    if (detected !== "") {
                        folderField.text = detected;
                    }
                    root.refreshXmlFiles();
                }
            }

            ToolButton {
                label: qsTr("Browse...")
                onClicked: folderDialog.open()
            }
        }

        // --- File selection + loading ---------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: qsTr("XML File"); color: Theme.subtext0; font.pixelSize: 11 }
                AppComboBox {
                    id: xmlFileCombo
                    Layout.fillWidth: true
                    model: root.xmlFiles
                    onActivated: (index) => {
                        scSettings.lastSelectedXml = root.xmlFiles[index];
                    }
                }
            }

            ToolButton {
                label: qsTr("Load XML")
                enabled: root.xmlFiles.length > 0
                opacity: enabled ? 1.0 : 0.5
                onClicked: {
                    if (root.xmlFiles.length === 0) {
                        return;
                    }
                    const fullPath = root.joinPath(folderField.text, root.xmlFiles[xmlFileCombo.currentIndex]);
                    if (!SCManager.loadProfile(fullPath)) {
                        console.warn("StarCitizenView: loadProfile failed for", fullPath);
                    }
                }
            }

            ToolButton {
                label: qsTr("Export to Star Citizen")
                accentColor: Theme.accent
                onClicked: {
                    // Fase SC-4: exports over the currently-selected XML if
                    // one is picked, or a new grembling_export.xml alongside
                    // it if the user hasn't selected/loaded anything yet.
                    const targetName = root.xmlFiles.length > 0
                        ? root.xmlFiles[xmlFileCombo.currentIndex]
                        : "grembling_export.xml";
                    const fullPath = root.joinPath(folderField.text, targetName);
                    if (SCManager.exportProfile(fullPath)) {
                        root.refreshXmlFiles();
                    }
                }
            }
        }

        // --- Windows Order Translator ---------------------------------
        // The game often internally assigns "js1"/"js2" to the vJoy devices
        // in a different order than expected (both vJoy devices share the
        // same VID/PID/product string, so Windows enumerates them
        // unstably, making it impossible to auto-detect which is which from
        // the XML alone). This panel lets the user correct that mapping by
        // hand.
        Rectangle {
            Layout.fillWidth: true
            radius: Theme.radiusMedium
            color: Theme.surface0
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.06)
            implicitHeight: jsTranslatorRow.implicitHeight + Theme.spacingMd * 2

            RowLayout {
                id: jsTranslatorRow
                anchors.fill: parent
                anchors.margins: Theme.spacingMd
                spacing: Theme.spacingLg

                Text {
                    text: qsTr("Windows Order Translator:")
                    color: Theme.subtext0
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }

                Repeater {
                    model: 4

                    RowLayout {
                        readonly property int repeaterIndex: index

                        spacing: Theme.spacingXs

                        Text {
                            // A single fully-translatable template string
                            // with a %1 placeholder (not string
                            // concatenation glueing a raw, untranslated
                            // suffix onto a qsTr() fragment) - some
                            // languages need a different word order around
                            // the number than English/Spanish do.
                            text: qsTr("The game reads js%1 as:").arg(repeaterIndex + 1)
                            color: Theme.text
                            font.pixelSize: 12
                        }

                        AppComboBox {
                            id: jsCombo
                            Layout.preferredWidth: 110
                            model: ["vJoy 1", "vJoy 2", "vJoy 3", "vJoy 4"]
                            currentIndex: SCManager.getJsTranslation(repeaterIndex + 1) - 1
                            onActivated: (cbIndex) => SCManager.setJsTranslation(repeaterIndex + 1, cbIndex + 1)
                        }
                    }
                }

                Item { Layout.fillWidth: true }
            }
        }

        // --- Visual controls list -------------------------------------
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
                visible: bindsListView.count === 0
                text: qsTr("Select and load an actionmaps.xml file to see its controls here.")
                color: Theme.overlay0
                font.pixelSize: 13
                font.italic: true
            }

            ListView {
                id: bindsListView
                anchors.fill: parent
                anchors.margins: Theme.spacingSm
                model: SCManager.scBinds
                clip: true
                spacing: Theme.spacingXs
                ScrollBar.vertical: ScrollBar { }

                delegate: RowLayout {
                    width: bindsListView.width
                    spacing: Theme.spacingSm

                    Text {
                        Layout.fillWidth: true
                        text: modelData.actionName
                        color: Theme.text
                        font.pixelSize: 13
                        elide: Text.ElideRight
                    }

                    // Fase SC-5.2: what Star Citizen itself expects for this
                    // action, independent of whether anything physical is
                    // currently bound to it - so "Not assigned" on the right
                    // reads as "go bind THIS vJoy target in Profiles"
                    // instead of leaving the user to guess.
                    Text {
                        visible: modelData.formattedInput !== ""
                        text: modelData.formattedInput
                        color: Theme.subtext0
                        font.pixelSize: 12
                    }

                    Text {
                        readonly property string hardwareText: profileEditorViewModel.resolveSCInput(modelData.input)
                        // Bugfix: both branches used to be raw, un-wrapped
                        // string literals ("Hardware: " + hardwareText /
                        // "No asignado") - neither ever went through
                        // qsTr(), so this line stayed in Spanish even with
                        // the English translator active (or always showed
                        // untranslated regardless of language).
                        text: hardwareText !== "" ? qsTr("Hardware: %1").arg(hardwareText) : qsTr("Not assigned")
                        color: hardwareText !== "" ? Theme.success : Theme.overlay0
                        font.pixelSize: 12
                    }
                }
            }
        }
    }

    // Fase SC-5.1: an explicit folder picker - detectMappingsFolder() only
    // guesses the default RSI install layout on the C: drive, so a user with
    // Star Citizen installed elsewhere (a different drive/launcher location)
    // saw an empty folderField (placeholderText renders in a lighter color
    // than real text, but reads similarly enough at a glance to look like a
    // populated path) and consequently a permanently-disabled "Load XML".
    FolderDialog {
        id: folderDialog
        title: qsTr("Select the Star Citizen Mappings Folder")
        onAccepted: {
            // selectedFolder is a "file:///..." QUrl - QDir on the C++ side
            // needs a plain native path, same conversion this project's own
            // FileDialog usages already apply (see AutoSwitchPopup.qml).
            folderField.text = selectedFolder.toString().replace("file:///", "");
            root.refreshXmlFiles();
        }
    }
}
