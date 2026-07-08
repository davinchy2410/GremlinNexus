import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import GremblingEx

// "Profiles" screen (Phase 10 Part 2 / Fase 10.7 / Fase 10.8): an accordion
// list of connected devices (see ProfileEditorViewModel) plus:
//  - header "Load Profile"/"Save Profile" buttons (Fase 10.7), backed by
//    ProfileEditorViewModel's loadProfileFromPath()/saveProfileToPath() and
//    a native QtQuick.Dialogs FileDialog (pure QML - this project
//    deliberately dropped QWidgets in Phase 10, so QFileDialog is not an
//    option here; see CMakeLists.txt).
//  - a Modes dropdown + "+"/"-" (Fase 10.8) selecting which mode new
//    bindings register under.
//  - a "Listen to Inputs" toggle (Fase 10.8's Quick Bind): while on,
//    wiggling a physical axis or pressing a physical button auto-scrolls to
//    and flashes the matching InputRow instead of counting buttons by hand.
//  - clicking any InputRow opens the shared ActionPickerPopup to bind it.
//
// Manual Flickable instead of ScrollView (Fase 10.8): Quick Bind's
// auto-scroll needs a contentY it can animate directly - ScrollView's
// internal Flickable wrapper is an implementation detail, not something
// reliably scriptable from outside. Same reasoning as the original
// ScrollView choice otherwise applies unchanged: the device count here is
// always small (a handful of physical devices, not thousands of rows), so
// there's still no ListView delegate-recycling to gain - a plain Repeater
// inside a manually-scrolled Column reflows naturally as each DeviceCard
// expands/collapses.
Item {
    id: root

    // Fase 20.10: which (devicePath, inputName) autoScrollTimer last
    // actually highlighted - lets onHardwareInputDetected below skip
    // re-highlighting the same input while a real joystick's continuous
    // electrical jitter keeps re-reporting it. Declared here (not on the
    // Connections block below) because both onHardwareInputDetected
    // (nested under scrollFlick) and autoScrollTimer's onTriggered (a
    // direct child of root) need to read/write it, and QML only resolves
    // an unqualified property name by walking up an object's own ancestor
    // chain - root is the nearest common ancestor of both.
    property string lastFocusedInputId: ""

    Rectangle {
        anchors.fill: parent
        color: Theme.base
    }

    Flickable {
        id: scrollFlick
        anchors.fill: parent
        anchors.margins: Theme.spacingLg
        clip: true
        contentWidth: width
        contentHeight: columnLayout.implicitHeight
        boundsBehavior: Flickable.StopAtBounds

        Behavior on contentY {
            NumberAnimation { duration: Theme.animSlow; easing.type: Easing.OutCubic }
        }

        ScrollBar.vertical: ScrollBar { }

        /// Quick Bind auto-scroll (Fase 10.8): brings item (an InputRow
        /// returned by DeviceCard.highlightInput()) into view, clamped so
        /// it never scrolls past either end of the content.
        function scrollToItem(item) {
            if (!item) {
                return;
            }
            const pt = item.mapToItem(columnLayout, 0, 0);
            const maxContentY = Math.max(0, scrollFlick.contentHeight - scrollFlick.height);
            scrollFlick.contentY = Math.max(0, Math.min(pt.y - Theme.spacingLg, maxContentY));
        }

        ColumnLayout {
            id: columnLayout
            width: scrollFlick.width
            spacing: Theme.spacingMd

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingMd

                ColumnLayout {
                    spacing: 2
                    Text {
                        text: "Profiles"
                        color: Theme.text
                        font.pixelSize: 24
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: "Current Profile: " + profileEditorViewModel.currentProfileName
                        color: Theme.subtext0
                        font.pixelSize: 13
                    }
                }

                Item { Layout.fillWidth: true }

                ToolButton {
                    label: "New Profile"
                    onClicked: profileEditorViewModel.newProfile()
                }
                ToolButton {
                    label: "Load Profile"
                    onClicked: loadProfileDialog.open()
                }
                ToolButton {
                    label: "Save Profile"
                    onClicked: saveProfileDialog.open()
                }
                ToolButton {
                    label: "Export Cheatsheet"
                    onClicked: exportCheatsheetDialog.open()
                }
                ToolButton {
                    label: "Import Legacy (XML)"
                    onClicked: importLegacyDialog.open()
                }
                ToolButton {
                    label: "Star Citizen Integration"
                    accentColor: Theme.accent
                    onClicked: mainViewModel.currentView = "StarCitizen"
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.bottomMargin: Theme.spacingSm
                spacing: Theme.spacingMd

                ColumnLayout {
                    spacing: 2
                    Text { text: "Mode"; color: Theme.subtext0; font.pixelSize: 11 }
                    RowLayout {
                        spacing: Theme.spacingXs

                        AppComboBox {
                            id: modeCombo
                            Layout.preferredWidth: 170
                            model: profileEditorViewModel.modes
                            onActivated: (idx) => { profileEditorViewModel.currentMode = profileEditorViewModel.modes[idx]; }
                            Component.onCompleted: currentIndex = profileEditorViewModel.modes.indexOf(profileEditorViewModel.currentMode)

                            Connections {
                                target: profileEditorViewModel
                                function onCurrentModeChanged() {
                                    modeCombo.currentIndex = profileEditorViewModel.modes.indexOf(profileEditorViewModel.currentMode);
                                }
                                function onModesChanged() {
                                    modeCombo.currentIndex = profileEditorViewModel.modes.indexOf(profileEditorViewModel.currentMode);
                                }
                            }
                        }
                        ToolButton {
                            label: "+"
                            Layout.preferredWidth: 34
                            onClicked: newModePopup.open()
                        }
                        ToolButton {
                            label: "−"
                            Layout.preferredWidth: 34
                            // "Global" mirrors EventRouter::kGlobalMode's literal value -
                            // the one mode ProfileEditorViewModel::removeMode() never removes.
                            enabled: profileEditorViewModel.currentMode !== "Global"
                            opacity: enabled ? 1.0 : 0.4
                            onClicked: profileEditorViewModel.removeMode(profileEditorViewModel.currentMode)
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                ColumnLayout {
                    spacing: 2
                    Layout.alignment: Qt.AlignRight
                    Text { text: "Quick Bind"; color: Theme.subtext0; font.pixelSize: 11; Layout.alignment: Qt.AlignRight }
                    RowLayout {
                        spacing: Theme.spacingSm
                        Text {
                            text: "Listen to Inputs"
                            color: Theme.text
                            font.pixelSize: 13
                        }
                        ToggleSwitch {
                            checked: profileEditorViewModel.listenForInputs
                            onToggled: (v) => profileEditorViewModel.listenForInputs = v
                        }
                    }
                }
            }

            Repeater {
                id: cardsRepeater
                model: profileEditorViewModel
                delegate: DeviceCard {
                    visible: !model.deviceName.toLowerCase().includes("vjoy")

                    Layout.fillWidth: true
                    deviceName: model.deviceName
                    vendorProduct: model.vendorProduct
                    systemPath: model.systemPath
                    inputs: model.inputs
                    onInputClicked: (devicePath, inputName, inputKind, hasBinding) => actionPicker.openFor(devicePath, inputName, inputKind, hasBinding)
                    onCalibrateRequested: (devicePath) => calibrationWizard.openFor(devicePath)
                }
            }

            Item { Layout.preferredHeight: Theme.spacingLg }
        }

        Connections {
            target: profileEditorViewModel

            function onHardwareInputDetected(devicePath, inputName) {
                const row = profileEditorViewModel.deviceRowForSystemPath(devicePath);
                if (row < 0) return;

                const card = cardsRepeater.itemAt(row);
                if (!card) return;

                const inputId = devicePath + "|" + inputName;
                const inputNameLower = inputName.toLowerCase();
                const isBtn = inputNameLower.includes("button") || inputNameLower.includes("pov");

                if (inputId === lastFocusedInputId && card.expanded) {
                    // Fase 20.11: the same-input-while-expanded skip below
                    // exists to swallow analog-axis jitter, but it was also
                    // swallowing a button's own repeat presses - mash
                    // "Button 1" and only the first flash ever showed. A
                    // button has no jitter to filter, so give it an
                    // immediate flash() here instead of dropping it; an
                    // axis still just gets ignored, unchanged from Fase 20.10.
                    if (isBtn) {
                        card.highlightInput(inputName);
                    }
                    return; // Already focused here and expanded - ignore repeat axis jitter.
                }

                // Fase 20.10: Fase 20.9's unconditional autoScrollTimer.restart()
                // on every single event starved the Timer outright - a real
                // analog axis never stops reporting jitter, so the 50ms debounce
                // kept getting pushed back before it could ever fire, and the UI
                // never scrolled at all while the joystick was held. Only restart
                // the Timer when a higher-priority event needs it (a button/hat
                // interrupting an axis) or when it isn't already running; same-
                // priority repeats (axis interrupting axis) just update the
                // pending target in place so the already-ticking Timer still
                // reaches zero on schedule.
                if (autoScrollTimer.running) {
                    if (autoScrollTimer.isButton && !isBtn) {
                        return; // Priority: the queued button wins over this axis.
                    }
                    if (!autoScrollTimer.isButton && isBtn) {
                        // A button interrupts a queued axis - hand it control and restart.
                        autoScrollTimer.targetCard = card;
                        autoScrollTimer.targetInput = inputName;
                        autoScrollTimer.isButton = true;
                        autoScrollTimer.interval = 50;
                        autoScrollTimer.restart();
                        return;
                    }
                    // Same priority (axis interrupting axis) - update the
                    // target only, do NOT restart, so the running Timer
                    // still expires on schedule instead of starving.
                    autoScrollTimer.targetCard = card;
                    autoScrollTimer.targetInput = inputName;
                    return;
                }

                // Timer wasn't running - arm it fresh.
                autoScrollTimer.targetCard = card;
                autoScrollTimer.targetInput = inputName;
                autoScrollTimer.isButton = isBtn;

                if (!card.expanded) {
                    // Collapsed - give DeviceCard's expand animation
                    // (Theme.animMedium, 300ms) time to finish before
                    // highlightInput()/scrollToItem() measure geometry.
                    card.expanded = true;
                    autoScrollTimer.interval = 300;
                } else {
                    // Already expanded - a short debounce so continuous
                    // axis jitter coalesces into occasional scrolls instead
                    // of hundreds per second.
                    autoScrollTimer.interval = 50;
                }
                autoScrollTimer.restart();
            }

            // Fase 20.5: patches the one delegate that changed in place via
            // DeviceCard::updateBinding() instead of relying on dataChanged
            // to refresh the whole row - see ProfileEditorViewModel's own
            // Fase 20.5 notes for why a wholesale row refresh broke
            // scrollToItem's geometry calculation.
            function onBindingUpdated(devicePath, inputName, hasBinding, bindingLabel) {
                const row = profileEditorViewModel.deviceRowForSystemPath(devicePath);
                if (row < 0) {
                    return;
                }
                const card = cardsRepeater.itemAt(row);
                if (card) {
                    card.updateBinding(inputName, hasBinding, bindingLabel);
                }
            }
        }
    }

    ActionPickerPopup {
        id: actionPicker
    }

    CalibrationWizardPopup {
        id: calibrationWizard
    }

    Popup {
        id: newModePopup
        modal: true
        focus: true
        x: Overlay.overlay ? (Overlay.overlay.width - width) / 2 : 0
        y: Overlay.overlay ? (Overlay.overlay.height - height) / 2 : 0
        width: 280
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: Theme.surface0
            radius: Theme.radiusMedium
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.08)
        }

        onOpened: {
            newModeField.text = "";
            newModeField.forceActiveFocus();
        }

        contentItem: ColumnLayout {
            spacing: Theme.spacingSm

            Text {
                text: "New Mode"
                color: Theme.text
                font.pixelSize: 15
                font.weight: Font.DemiBold
                Layout.margins: Theme.spacingMd
                Layout.bottomMargin: 0
            }

            TextField {
                id: newModeField
                Layout.fillWidth: true
                Layout.leftMargin: Theme.spacingMd
                Layout.rightMargin: Theme.spacingMd
                placeholderText: "Mode name"
                color: Theme.text
                font.pixelSize: 13
                background: Rectangle {
                    color: Theme.surface1
                    radius: Theme.radiusSmall
                    border.width: 1
                    border.color: Qt.rgba(1, 1, 1, 0.08)
                }
                Keys.onReturnPressed: createModeButton.clicked()
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.margins: Theme.spacingMd
                Layout.topMargin: Theme.spacingXs
                spacing: Theme.spacingSm

                Item { Layout.fillWidth: true }

                ToolButton {
                    label: "Cancel"
                    onClicked: newModePopup.close()
                }
                ToolButton {
                    id: createModeButton
                    label: "Create"
                    onClicked: {
                        const name = newModeField.text.trim();
                        if (name.length > 0) {
                            profileEditorViewModel.addMode(name);
                            newModePopup.close();
                        }
                    }
                }
            }
        }
    }

    FileDialog {
        id: exportCheatsheetDialog
        title: "Export Cheatsheet"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "pdf"
        nameFilters: ["PDF documents (*.pdf)"]
        onAccepted: profileEditorViewModel.exportCheatsheetPdf(selectedFile)
    }

    FileDialog {
        id: loadProfileDialog
        title: "Load Profile"
        fileMode: FileDialog.OpenFile
        nameFilters: ["JSON profiles (*.json)"]
        onAccepted: profileEditorViewModel.loadProfileFromPath(selectedFile)
    }

    FileDialog {
        id: saveProfileDialog
        title: "Save Profile"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: ["JSON profiles (*.json)"]
        onAccepted: profileEditorViewModel.saveProfileToPath(selectedFile)
    }

    // Fase 15: legacyMigrator.importLegacyProfile() converts the picked
    // legacy XML into GremblingEx's JSON schema at newPath (derived from the
    // XML's own name), then loads it straight into the router on success -
    // no separate "where do you want to save the migrated JSON" prompt,
    // since that path is just an implementation detail of the conversion.
    FileDialog {
        id: importLegacyDialog
        title: "Select Legacy Profile (XML)"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Joystick Gremlin Profiles (*.xml)", "All Files (*)"]
        onAccepted: {
            const oldPath = selectedFile;
            const newPath = oldPath.toString().replace(".xml", "_migrated.json");
            if (legacyMigrator.importLegacyProfile(oldPath, newPath)) {
                profileEditorViewModel.loadProfileFromPath(newPath);
            }
        }
    }

    // Fase 20.6/20.9: debounces Quick Bind's highlight+scroll - interval is
    // set dynamically per event by onHardwareInputDetected above: 300ms
    // (matching Theme.animMedium) while waiting out a card's expand
    // animation, or a short 50ms once already expanded, just enough to
    // coalesce continuous analog-axis jitter into occasional scrolls
    // instead of hundreds per second. isButton lets a queued button/hat
    // event win over axis jitter arriving in the same window.
    Timer {
        id: autoScrollTimer
        interval: 300
        property var targetCard: null
        property string targetInput: ""
        property bool isButton: false

        onTriggered: {
            if (!targetCard) return;
            root.lastFocusedInputId = targetCard.systemPath + "|" + targetInput;
            const rowItem = targetCard.highlightInput(targetInput);
            if (rowItem) {
                scrollFlick.scrollToItem(rowItem);
            }
        }
    }
}
