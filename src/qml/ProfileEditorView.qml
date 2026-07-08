import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Effects
import QtQuick.Layouts
import GremblingNexus

// "Profiles" screen (Phase 10 Part 2 / Fase 10.7 / Fase 10.8; Tabs
// migration): a TabBar (one tab per connected device, see
// ProfileEditorViewModel) over a StackLayout showing only the active
// device's own DeviceCard, plus:
//  - header "New Profile"/"Options ▾" buttons (Fase 10.7 originals, folded
//    into the Options dropdown later), backed by ProfileEditorViewModel's
//    loadProfileFromPath()/saveProfileToPath() and a native
//    QtQuick.Dialogs FileDialog (pure QML - this project deliberately
//    dropped QWidgets in Phase 10, so QFileDialog is not an option here;
//    see CMakeLists.txt).
//  - a Modes dropdown + "Manage Modes" (Fase 10.8) selecting which mode new
//    bindings register under.
//  - a "Listen to Inputs" toggle (Fase 10.8's Quick Bind): while on,
//    wiggling a physical axis or pressing a physical button switches to
//    that device's tab and flashes the matching InputRow instead of
//    counting buttons by hand.
//  - clicking any InputRow opens the shared ActionPickerPopup to bind it.
//
// Before the Tabs migration, every device was a collapsible card stacked in
// one shared, manually-scrolled Flickable (Quick Bind needed a contentY it
// could animate directly, which ScrollView's own internal Flickable isn't
// reliably scriptable from outside) - that stopped scaling once a rig had
// several multi-hundred-input devices, since reaching the last one meant
// scrolling past every other one first. Each DeviceCard now owns its own
// internal scroll area instead (see DeviceCard.qml), and only the active
// tab's is ever mounted/visible at a time.
Item {
    id: root

    // Fase 20.10: which (devicePath, inputName) autoScrollTimer last
    // actually highlighted - lets onHardwareInputDetected below skip
    // re-highlighting the same input while a real joystick's continuous
    // electrical jitter keeps re-reporting it. Declared here (not on the
    // Connections block below) because both onHardwareInputDetected
    // (nested under deviceArea) and autoScrollTimer's onTriggered (a
    // direct child of root) need to read/write it, and QML only resolves
    // an unqualified property name by walking up an object's own ancestor
    // chain - root is the nearest common ancestor of both.
    property string lastFocusedInputId: ""

    Rectangle {
        anchors.fill: parent
        color: Theme.base
    }

    // Fase (bugfix): anchored directly to root instead of living inside
    // deviceArea below - this is the "New Profile"/"Options" button row,
    // and it should stay put above the device tabs, the same way
    // TopHeader.qml's own app-wide header never scrolls with whichever
    // screen is active underneath it.
    RowLayout {
        id: fixedHeaderRow
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingMd

        ColumnLayout {
            spacing: 2
            Text {
                text: qsTr("Profiles")
                color: Theme.text
                font.pixelSize: 24
                font.weight: Font.DemiBold
            }
            Text {
                text: qsTr("Current Profile: ") + profileEditorViewModel.currentProfileName
                color: Theme.subtext0
                font.pixelSize: 13
            }
        }

        Item { Layout.fillWidth: true }

        ToolButton {
            label: qsTr("New Profile")
            onClicked: profileEditorViewModel.newProfile()
        }

        // Quick Save, relocated from TopHeader.qml's global bar (Fase
        // bugfix) - it's specific to whichever profile is open in this
        // screen, not a global action, so it belongs here rather than
        // floating in the app-wide header. Same target function the
        // floppy-disk icon called before.
        ToolButton {
            label: qsTr("Save")
            onClicked: profileEditorViewModel.quickSave()
        }

        // --- "⚙ Options ▾" dropdown --------------------------------------
        // Collapses the previous Load/Save/Export/Import/Star-Citizen
        // buttons into one menu, grouped by what they actually do: file
        // I/O, then interchange formats, then the external integration.
        Item {
            id: optionsButton
            implicitWidth: optionsContent.implicitWidth + 24
            implicitHeight: 34

            readonly property bool menuOpen: optionsMenu.visible
            readonly property bool highlighted: menuOpen || optionsMouseArea.containsMouse

            Rectangle {
                anchors.fill: parent
                radius: Theme.radiusSmall
                color: optionsButton.highlighted ? Theme.accent : "transparent"
                border.width: 1
                border.color: Theme.accent
                Behavior on color { ColorAnimation { duration: Theme.animFast } }
            }

            RowLayout {
                id: optionsContent
                anchors.centerIn: parent
                spacing: 6

                Text {
                    // A gear already means "Settings" elsewhere in this app
                    // (the global SETTINGS tab) - reusing it here for a
                    // completely different, per-profile file/options menu
                    // made the two easy to mix up. "≡" reads as "more
                    // options" instead, the same convention as most apps'
                    // hamburger/overflow menus.
                    text: "≡"
                    font.pixelSize: 14
                    color: optionsButton.highlighted ? Theme.crust : Theme.text
                }
                Text {
                    text: qsTr("Options")
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 1
                    color: optionsButton.highlighted ? Theme.crust : Theme.text
                }
                Text {
                    text: "▾"
                    font.pixelSize: 10
                    color: optionsButton.highlighted ? Theme.crust : Theme.subtext0
                    rotation: optionsButton.menuOpen ? 180 : 0
                    Behavior on rotation { NumberAnimation { duration: Theme.animFast } }
                }
            }

            MouseArea {
                id: optionsMouseArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                // popup(item) computes the correct on-screen position from
                // optionsButton directly, so it stays correct regardless of
                // optionsMenu's own parent/coordinate space below - unlike a
                // fixed `y: parent.height`, which broke the instant
                // optionsMenu's parent became Overlay.overlay (parent.height
                // then meant the whole window's height, not the button's).
                onClicked: optionsMenu.popup(optionsButton)
            }

            Menu {
                id: optionsMenu

                // Reparented to the window's global overlay (rather than
                // rendered/positioned relative to optionsButton's own Item,
                // nested inside fixedHeaderRow's RowLayout) so it can never
                // be clipped or stacked under anything by an ancestor
                // layout - popup(optionsButton) above is what actually
                // anchors it visually under the button despite this.
                parent: Overlay.overlay
                z: 999

                // The actual root cause of "the menu never appears" (verified
                // via a temporary debug build logging optionsMenu.width at
                // open: it came back 0): Menu's default contentItem doesn't
                // pick up StyledMenuItem's implicitWidth:220 on its own here,
                // so the popup opened with zero width - "visible: true" the
                // whole time, just invisible. An explicit width fixes it the
                // same way toolsPopup (TopHeader.qml) already sets one.
                width: 220

                background: Item {
                    Rectangle {
                        id: optionsMenuBg
                        anchors.fill: parent
                        color: Qt.rgba(Theme.surface0.r, Theme.surface0.g, Theme.surface0.b, 0.95)
                        border.width: 1
                        border.color: Theme.accent
                    }
                    MultiEffect {
                        source: optionsMenuBg
                        anchors.fill: optionsMenuBg
                        shadowEnabled: true
                        shadowColor: Theme.accent
                        shadowBlur: 2.0
                        blurMax: 32
                    }
                }

                // Group 1: file I/O
                AppMenuItem {
                    text: qsTr("Load Profile")
                    onTriggered: loadProfileDialog.open()
                }
                AppMenuItem {
                    text: qsTr("Save Profile")
                    onTriggered: saveProfileDialog.open()
                }

                // vJoy Auditor: read-only diagnostic showing which vJoy
                // axes/buttons the current profile already occupies (see
                // VJoyAuditorPopup.qml's own docs) - grouped with Load/Save
                // rather than the interchange-format group below since it's
                // a way of inspecting THIS profile, not converting it.
                // No iconData (UI fix): every other item in this menu
                // (Load/Save Profile, Export Cheatsheet, ...) is text-only -
                // AppMenuItem.qml gives an icon its own left-margined slot
                // and only falls back to margining the text directly when
                // iconData is "" (see its own contentItem), so a lone icon
                // here shifted this row's text out of vertical alignment
                // with every sibling item's own left edge.
                AppMenuItem {
                    text: qsTr("vJoy Auditor")
                    onTriggered: auditorPopup.open()
                }

                MenuSeparator {
                    contentItem: Rectangle { implicitHeight: 1; color: Theme.surface1 }
                }

                // Group 2: interchange formats
                AppMenuItem {
                    text: qsTr("Export Cheatsheet")
                    onTriggered: exportCheatsheetDialog.open()
                }
                AppMenuItem {
                    text: qsTr("Import Legacy (XML)")
                    onTriggered: importLegacyDialog.open()
                }

                MenuSeparator {
                    contentItem: Rectangle { implicitHeight: 1; color: Theme.surface1 }
                }

                // Group 3: external integration
                AppMenuItem {
                    text: qsTr("Star Citizen Integration")
                    onTriggered: mainViewModel.currentView = "StarCitizen"
                }
            }
        }
    }

    // Fase (Tabs migration): the vertical accordion-list-in-a-Flickable
    // (every device stacked and expand/collapse-able, one giant scroll for
    // the whole screen) didn't scale past a handful of devices - a rig with
    // several multi-hundred-input panels meant endless scrolling just to
    // reach the last one. Replaced with a TabBar (one tab per device) over
    // a StackLayout that only ever renders the active device's content;
    // each DeviceCard now owns its own internal scroll area instead of
    // sharing one across every device (see DeviceCard.qml).
    ColumnLayout {
        id: deviceArea
        anchors.top: fixedHeaderRow.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.topMargin: Theme.spacingMd
        anchors.leftMargin: Theme.spacingLg
        anchors.rightMargin: Theme.spacingLg
        anchors.bottomMargin: Theme.spacingLg
        spacing: Theme.spacingMd

        RowLayout {
            Layout.fillWidth: true
            Layout.bottomMargin: Theme.spacingSm
            spacing: Theme.spacingMd

            ColumnLayout {
                spacing: 2
                Text { text: qsTr("Mode"); color: Theme.subtext0; font.pixelSize: 11 }
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
                        // Sprint 5 (Familias de Modos): replaces the old
                        // flat +/pencil/- trio with a single entry point
                        // into ModeManagerPopup, which now also owns
                        // creating/renaming/deleting modes plus each
                        // mode's "Inherits from" parent - see that
                        // popup's own docs for why a single surface
                        // replaced three buttons instead of just adding
                        // a fourth.
                        //
                        // No iconData (Fase icon cleanup): a gear here would
                        // be a third distinct meaning for the same glyph in
                        // this one screen (global SETTINGS tab, the
                        // Profiles header's "≡ Options" menu, and now this)
                        // - the "/>>" terminal-prompt suffix reads as
                        // "jump into a sub-screen" without borrowing an icon
                        // that already means something else nearby.
                        label: qsTr("Manage Modes  />>")
                        onClicked: modeManagerPopup.open()
                    }
                }
            }

            Item { Layout.fillWidth: true }

            ColumnLayout {
                spacing: 2
                Layout.alignment: Qt.AlignRight
                Text { text: qsTr("Quick Bind"); color: Theme.subtext0; font.pixelSize: 11; Layout.alignment: Qt.AlignRight }
                RowLayout {
                    spacing: Theme.spacingSm
                    Text {
                        text: qsTr("Listen to Inputs")
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

        TabBar {
            id: deviceTabBar
            Layout.fillWidth: true
            spacing: 2

            // Horizontal-scroll bugfix: a rig with many connected devices
            // needs more tab width than any reasonable window has - the
            // Basic style's default TabBar.contentItem is already a
            // horizontal ListView (StopAtBounds + AutoFlickIfNeeded, so it
            // scroll/flicks once contentWidth exceeds the bar's own width,
            // never shrinks individual TabButtons to force-fit them), but
            // that's an undocumented style-internal default we don't want
            // to silently depend on. Overriding contentItem here pins down
            // the exact same scrolling ListView explicitly, plus adds a
            // visible horizontal ScrollBar so overflow reads as "scroll for
            // more" instead of tabs just quietly clipping off the edge.
            contentItem: ListView {
                model: deviceTabBar.contentModel
                currentIndex: deviceTabBar.currentIndex

                spacing: deviceTabBar.spacing
                orientation: ListView.Horizontal
                boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.AutoFlickIfNeeded
                snapMode: ListView.SnapToItem

                highlightMoveDuration: 0
                highlightRangeMode: ListView.ApplyRange
                preferredHighlightBegin: 40
                preferredHighlightEnd: width - 40

                ScrollBar.horizontal: ScrollBar {
                    policy: ScrollBar.AsNeeded
                }
            }

            background: Item {
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: Theme.surface1
                }
            }

            // Each TabButton delegate below sizes itself off its own
            // contentItem Text's implicit width (no explicit `width:`
            // override anywhere in the delegate) - that's what lets the
            // ListView above judge when contentWidth actually exceeds the
            // bar and needs to scroll, instead of every tab getting
            // force-shrunk to fit.
            Repeater {
                id: deviceTabsRepeater
                model: profileEditorViewModel
                delegate: TabButton {
                    id: deviceTab
                    visible: !model.deviceName.toLowerCase().includes("vjoy")
                    text: model.deviceName
                    implicitHeight: 38

                    contentItem: Text {
                        text: deviceTab.text
                        color: deviceTab.checked ? Theme.accent : (deviceTab.hovered ? Theme.text : Theme.subtext0)
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        font.capitalization: Font.AllUppercase
                        font.letterSpacing: 1
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        Behavior on color { ColorAnimation { duration: Theme.animMedium } }
                    }

                    background: Item {
                        Rectangle {
                            anchors.fill: parent
                            color: deviceTab.checked
                                ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.08)
                                : (deviceTab.hovered ? Theme.surface0 : "transparent")
                            Behavior on color { ColorAnimation { duration: Theme.animMedium } }
                        }
                        // Bright cyan underline - the same "active tab" language
                        // TopHeaderButton.qml already uses for the main nav tabs.
                        Rectangle {
                            anchors.bottom: parent.bottom
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: deviceTab.checked ? parent.width * 0.7 : 0
                            height: 2
                            radius: 1
                            color: Theme.accent
                            opacity: deviceTab.checked ? 1.0 : 0.0
                            Behavior on width { NumberAnimation { duration: Theme.animMedium; easing.type: Easing.OutBack } }
                            Behavior on opacity { NumberAnimation { duration: Theme.animMedium } }
                        }
                    }
                }
            }

            // Guards against defaulting to a hidden vJoy tab (currentIndex
            // 0 otherwise) leaving the StackLayout showing a blank page with
            // no visibly-selected tab above it - picks the first real device
            // instead, once the Repeater has actually built its delegates.
            Component.onCompleted: Qt.callLater(() => {
                const current = deviceTabsRepeater.itemAt(deviceTabBar.currentIndex);
                if (current && current.visible) {
                    return;
                }
                for (let i = 0; i < deviceTabsRepeater.count; i++) {
                    const btn = deviceTabsRepeater.itemAt(i);
                    if (btn && btn.visible) {
                        deviceTabBar.currentIndex = i;
                        return;
                    }
                }
            })
        }

        StackLayout {
            id: deviceStack
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: deviceTabBar.currentIndex

            Repeater {
                id: cardsRepeater
                model: profileEditorViewModel
                delegate: DeviceCard {
                    id: deviceCardDelegate
                    visible: !model.deviceName.toLowerCase().includes("vjoy")

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    deviceName: model.deviceName
                    vendorProduct: model.vendorProduct
                    systemPath: model.systemPath
                    inputs: model.inputs
                    onInputClicked: (devicePath, inputName, inputKind, hasBinding) => actionPicker.openFor(devicePath, inputName, inputKind, hasBinding)
                    onCalibrateRequested: (devicePath) => calibrationWizard.openFor(devicePath)
                    onCurvesRequested: (devicePath, inputName) => {
                        profileEditorViewModel.setCurveEditorTarget(devicePath, inputName);
                        mainViewModel.currentView = "Curves";
                    }

                    // FUI "Wow Factor" (Data Terminal redesign): the active
                    // tab's content materializes instead of appearing static
                    // - opacity/scale only, so this never fights the
                    // StackLayout's own positioning of this delegate.
                    opacity: 0
                    scale: 0.96
                    Behavior on opacity { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
                    Behavior on scale { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
                    Component.onCompleted: staggerTimer.start()

                    Timer {
                        id: staggerTimer
                        interval: index * 60
                        onTriggered: {
                            deviceCardDelegate.opacity = 1;
                            deviceCardDelegate.scale = 1;
                        }
                    }
                }
            }
        }

        Connections {
            target: profileEditorViewModel

            /// Quick Bind (Fase 10.8, ported to Tabs): a physical
            /// axis/button move switches to that device's own tab (instant -
            /// no expand animation to wait out anymore) and asks its
            /// DeviceCard to flash + scroll to the matching row internally.
            function onHardwareInputDetected(devicePath, inputName) {
                const row = profileEditorViewModel.deviceRowForSystemPath(devicePath);
                if (row < 0) return;

                const card = cardsRepeater.itemAt(row);
                if (!card) return;

                const inputId = devicePath + "|" + inputName;
                const inputNameLower = inputName.toLowerCase();
                const isBtn = inputNameLower.includes("button") || inputNameLower.includes("pov");
                const alreadyOnTab = deviceTabBar.currentIndex === row;

                if (inputId === root.lastFocusedInputId && alreadyOnTab) {
                    // Fase 20.11: swallow analog-axis jitter for the same,
                    // already-focused input - but still flash a button's own
                    // repeat presses (no jitter to filter for a button).
                    if (isBtn) {
                        card.highlightInput(inputName);
                    }
                    return;
                }

                // Fase 20.10: only restart the debounce Timer when a
                // higher-priority event needs it (a button/hat interrupting
                // an axis) or when it isn't already running; same-priority
                // repeats (axis interrupting axis) just update the pending
                // target in place so the already-ticking Timer still expires
                // on schedule instead of being starved by continuous jitter.
                if (autoScrollTimer.running) {
                    if (autoScrollTimer.isButton && !isBtn) {
                        return; // Priority: the queued button wins over this axis.
                    }
                    if (!autoScrollTimer.isButton && isBtn) {
                        autoScrollTimer.targetCard = card;
                        autoScrollTimer.targetInput = inputName;
                        autoScrollTimer.isButton = true;
                        autoScrollTimer.interval = 50;
                        autoScrollTimer.restart();
                        return;
                    }
                    autoScrollTimer.targetCard = card;
                    autoScrollTimer.targetInput = inputName;
                    return;
                }

                autoScrollTimer.targetCard = card;
                autoScrollTimer.targetInput = inputName;
                autoScrollTimer.isButton = isBtn;

                if (!alreadyOnTab) {
                    // Switching tabs is instant, but the newly-current
                    // StackLayout page may not have laid out its own
                    // internal Flickable's geometry yet on this same frame -
                    // give it a beat before highlightInput() measures it.
                    deviceTabBar.currentIndex = row;
                    autoScrollTimer.interval = 100;
                } else {
                    // Same tab already active - a short debounce so
                    // continuous axis jitter coalesces into occasional
                    // scrolls instead of hundreds per second.
                    autoScrollTimer.interval = 50;
                }
                autoScrollTimer.restart();
            }

            // Fase 20.5: patches the one delegate that changed in place via
            // DeviceCard::updateBinding() instead of relying on dataChanged
            // to refresh the whole row - see ProfileEditorViewModel's own
            // Fase 20.5 notes for why a wholesale row refresh broke
            // scrollToItem's geometry calculation.
            function onBindingUpdated(devicePath, inputName, hasBinding, bindingLabel, actionNote) {
                const row = profileEditorViewModel.deviceRowForSystemPath(devicePath);
                if (row < 0) {
                    return;
                }
                const card = cardsRepeater.itemAt(row);
                if (card) {
                    card.updateBinding(inputName, hasBinding, bindingLabel, actionNote);
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

    ModeManagerPopup {
        id: modeManagerPopup
    }

    VJoyAuditorPopup {
        id: auditorPopup
    }

    FileDialog {
        id: exportCheatsheetDialog
        title: qsTr("Export Cheatsheet")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "pdf"
        nameFilters: ["PDF documents (*.pdf)"]
        onAccepted: profileEditorViewModel.exportCheatsheetPdf(selectedFile)
    }

    FileDialog {
        id: loadProfileDialog
        title: qsTr("Load Profile")
        fileMode: FileDialog.OpenFile
        nameFilters: ["JSON profiles (*.json)"]
        onAccepted: profileEditorViewModel.loadProfileFromPath(selectedFile)
    }

    FileDialog {
        id: saveProfileDialog
        title: qsTr("Save Profile")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: ["JSON profiles (*.json)"]
        onAccepted: profileEditorViewModel.saveProfileToPath(selectedFile)
    }

    // Fase 15: legacyMigrator.importLegacyProfile() converts the picked
    // legacy XML into Grembling Nexus' JSON schema at newPath (derived from the
    // XML's own name), then loads it straight into the router on success -
    // no separate "where do you want to save the migrated JSON" prompt,
    // since that path is just an implementation detail of the conversion.
    FileDialog {
        id: importLegacyDialog
        title: qsTr("Select Legacy Profile (XML)")
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

    // Fase 20.6/20.9 (ported to Tabs): debounces Quick Bind's highlight+
    // scroll - interval is set dynamically per event by
    // onHardwareInputDetected above: 100ms while waiting out a tab switch,
    // or a short 50ms once already on the right tab, just enough to
    // coalesce continuous analog-axis jitter into occasional scrolls
    // instead of hundreds per second. isButton lets a queued button/hat
    // event win over axis jitter arriving in the same window. Scrolling the
    // matched row into view is now DeviceCard's own responsibility (its
    // internal Flickable), since there's no single shared list to scroll
    // across every device anymore - see DeviceCard.highlightInput().
    Timer {
        id: autoScrollTimer
        interval: 300
        property var targetCard: null
        property string targetInput: ""
        property bool isButton: false

        onTriggered: {
            if (!targetCard) return;
            root.lastFocusedInputId = targetCard.systemPath + "|" + targetInput;
            targetCard.highlightInput(targetInput);
        }
    }
}
