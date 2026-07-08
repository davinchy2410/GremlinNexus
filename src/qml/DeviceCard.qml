import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Effects
import GremblingNexus

// One device's full tab page (Tabs migration - previously an accordion card
// stacked with every other device in one shared scrollable list; see
// ProfileEditorView.qml's own docs for why). Fills whatever StackLayout page
// it's given entirely - header row (name/VID-PID, input count, Calibrate,
// Hide Unbound, the "[ ⚙ ]" terminal menu) pinned at the top, and its own
// internal Flickable owning the vertical scroll for potentially hundreds of
// axis/button rows underneath, since there's no longer one outer Flickable
// shared across every device to do that for it.
Item {
    id: root

    property string deviceName: ""
    property string vendorProduct: ""
    property string systemPath: ""
    property var inputs: []

    /// Sprint Final (Hide Unbound filter): when true, the input Repeater
    /// below hides every row whose live hasBinding is false - purely a
    /// local view-state toggle (not persisted, not touching m_router/
    /// ProfileEditorViewModel at all).
    property bool hideUnbound: false

    // HidHide cloaking (Fase 11): whether this device is currently hidden
    // from non-whitelisted applications. Re-read explicitly after every
    // toggle (see the "Visible"/"Cloaked" menu item below) rather than kept
    // live via a changed-signal - HidHideManager's own state can also
    // change outside this process (its GUI, another app), so
    // isDeviceCloaked() is a fresh query every time anyway.
    property bool hidHideInstalled: profileEditorViewModel.isHidHideInstalled()
    property bool cloaked: hidHideInstalled && profileEditorViewModel.isDeviceCloaked(root.systemPath)

    /// FUI "Wow Factor" (Data Terminal redesign): the "N inputs" readout
    /// counts up from 0 instead of appearing static the instant the tab
    /// loads - see inputCountAnim below. Re-triggers if inputs itself ever
    /// changes later (e.g. a device reconnects with a different input
    /// count), not just on first load.
    property int displayedInputCount: 0
    NumberAnimation {
        id: inputCountAnim
        target: root
        property: "displayedInputCount"
        from: 0
        to: root.inputs.length
        duration: 300
        easing.type: Easing.OutCubic
    }
    Component.onCompleted: inputCountAnim.start()
    onInputsChanged: inputCountAnim.restart()

    /// Emitted when one of this device's InputRows is clicked - bubbles up
    /// enough context (systemPath + the clicked input's own name/kind) for
    /// ProfileEditorView's shared Action Picker popup to open pre-targeted
    /// at the right input.
    signal inputClicked(string systemPath, string inputName, string inputKind, bool hasBinding)

    /// Fase 10.9: bubbles up to ProfileEditorView's shared
    /// CalibrationWizardPopup, same "just carry the systemPath up" pattern
    /// as inputClicked above.
    signal calibrateRequested(string systemPath)

    /// Fase (Curves nav rework); Sprint Final: now also carries which exact
    /// axis input was clicked (InputRow's own per-axis curve icon, not a
    /// device-wide button anymore - see the Repeater delegate below).
    /// Bubbles up to ProfileEditorView, which calls
    /// profileEditorViewModel.setCurveEditorTarget(systemPath, inputName)
    /// before switching mainViewModel.currentView to "Curves".
    signal curvesRequested(string systemPath, string inputName)

    /// Quick Bind support (Fase 10.8; Tabs migration): flashes the InputRow
    /// named name and scrolls this device's own internal Flickable to bring
    /// it into view, so ProfileEditorView's hardwareInputDetected handler
    /// only has to switch to this device's tab and call this - no more
    /// "expand the card" step now that there's nothing to expand. Returns
    /// the InputRow item, or null if this device has no input by that name.
    function highlightInput(name) {
        for (let i = 0; i < inputRepeater.count; i++) {
            const item = inputRepeater.itemAt(i);
            if (item && item.inputName === name) {
                item.flash();
                inputScrollFlick.scrollToItem(item);
                return item;
            }
        }
        return null;
    }

    /// Fase 20.5: patches one already-existing InputRow delegate's binding
    /// state in place, instead of the Repeater rebuilding every delegate the
    /// way reassigning root.inputs wholesale would - that rebuild is what
    /// broke scrollToItem's geometry calculation in Fase 20.4. Sprint QoL
    /// Part 2: note defaults to "" so ProfileEditorView.qml's own
    /// onBindingUpdated (whose signal note argument didn't exist before this
    /// method already had callers) doesn't need updating just to keep
    /// compiling - it's already passing it through in practice, see that
    /// file's own handler.
    function updateBinding(name, hasBinding, label, note) {
        for (let i = 0; i < inputRepeater.count; i++) {
            const item = inputRepeater.itemAt(i);
            if (item && item.inputName === name) {
                item.hasBinding = hasBinding;
                item.bindingLabel = label;
                item.actionNote = note || "";

                // Keeps the underlying JS array in sync too, so a later
                // Repeater rebuild from root.inputs doesn't revert this
                // delegate's binding.
                if (root.inputs[i]) {
                    root.inputs[i].hasBinding = hasBinding;
                    root.inputs[i].bindingLabel = label;
                    root.inputs[i].actionNote = note || "";
                }
                return;
            }
        }
    }

    Rectangle {
        id: cardBackground
        anchors.fill: parent
        radius: Theme.radiusMedium
        color: Theme.surface0
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.05)
        clip: true

        ColumnLayout {
            id: contentColumn
            anchors.fill: parent
            anchors.margins: Theme.spacingMd
            spacing: Theme.spacingSm

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Text {
                        text: root.deviceName
                        color: Theme.text
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                        font.capitalization: Font.AllUppercase
                        font.letterSpacing: 1
                    }
                    Text {
                        text: root.vendorProduct
                        color: Theme.overlay0
                        font.pixelSize: 11
                    }
                }

                Text {
                    text: root.displayedInputCount + " inputs"
                    color: Theme.subtext0
                    font.pixelSize: 11
                }

                // Terminal actions (Tabs migration): only the two
                // constantly-used actions stay as their own buttons; the
                // other four (1:1 Map, Swap To, Clear All, Visible/Cloaked)
                // moved into the "[ ⚙ ]" menu below - a full 6-button row
                // per device made sense stacked in a list, but not repeated
                // identically across every tab's header.
                ToolButton {
                    id: btnCalibrate
                    label: qsTr("Calibrate")
                    iconData: "M12 20a8 8 0 1 0 0 -16 8 8 0 0 0 0 16z M12 2v20 M2 12h20"
                    onClicked: root.calibrateRequested(root.systemPath)
                }

                ToolButton {
                    id: btnHideUnbound
                    label: root.hideUnbound ? qsTr("Hidden Unbound") : qsTr("Hide Unbound")
                    iconData: root.hideUnbound
                        ? "M17.94 17.94A10.07 10.07 0 0 1 12 20c-7 0 -11 -8 -11 -8a18.45 18.45 0 0 1 5.06 -5.94M9.9 4.24A9.12 9.12 0 0 1 12 4c7 0 11 8 11 8a18.5 18.5 0 0 1 -2.16 3.19m-6.72 -1.07a3 3 0 1 1 -4.24 -4.24M1 1l22 22"
                        : "M1 12s4 -8 11 -8 11 8 11 8 -4 8 -11 8 -11 -8 -11 -8z M12 9a3 3 0 1 0 0 6 3 3 0 1 0 0 -6z"
                    onClicked: root.hideUnbound = !root.hideUnbound
                }

                // Device actions menu trigger: styled as a plain ToolButton
                // (same border/fill/hover as Calibrate/Hide Unbound next to
                // it) instead of a bracketed ghost glyph - "[ >_ ]" read too
                // close to the global Log Console's own terminal styling,
                // and a standalone bracket of text didn't look like a real,
                // framed interface control. Kebab (⋮) instead of an icon:
                // this is a generic "more actions" trigger, not one more
                // specific hardware/settings glyph to keep track of.
                ToolButton {
                    id: terminalButton
                    label: "⋮"
                    implicitWidth: implicitHeight // square, not a stretched pill

                    // Guards against the classic Popup "reopen bounce" -
                    // same pattern as TopHeader.qml's "TOOLS" button (see
                    // that file for the full explanation): a click landing
                    // on this button while deviceMenu is open first triggers
                    // CloseOnPressOutside (on the press, before this click's
                    // own onClicked runs on release), so a naive
                    // `visible ? close() : popup()` ternary sees it as
                    // already-closed and immediately reopens it.
                    property bool suppressReopen: false

                    Timer {
                        id: terminalReopenGuard
                        interval: 150
                        onTriggered: terminalButton.suppressReopen = false
                    }

                    onClicked: {
                        if (terminalButton.suppressReopen) {
                            return;
                        }
                        if (deviceMenu.visible) {
                            deviceMenu.close();
                        } else {
                            // The (0, height+4) offset matters here, not just
                            // cosmetically: popup(item) alone anchors right
                            // at the item's own top-left with no gap, so the
                            // menu's first row rendered directly over this
                            // button - a second click meant to close it (by
                            // clicking the same spot) actually landed on
                            // that top MenuItem instead. Same 4px gap
                            // TopHeader.qml's toolsPopup already uses.
                            deviceMenu.popup(terminalButton, 0, terminalButton.height + 4);
                        }
                    }

                    Menu {
                        id: deviceMenu

                        // Same Overlay-escape as ProfileEditorView's own
                        // "Options" menu (and for the same reason - this
                        // Item sits deep inside contentColumn/StackLayout,
                        // any of which clipping would otherwise cut the
                        // popup off) - width is required explicitly since
                        // Menu's default contentItem doesn't pick up
                        // AppMenuItem's implicitWidth on its own (verified
                        // the hard way: an unset width silently opens at
                        // width 0, i.e. invisible, not "not opening" at all).
                        parent: Overlay.overlay
                        z: 999
                        width: 220

                        onClosed: {
                            terminalButton.suppressReopen = true;
                            terminalReopenGuard.restart();
                        }

                        background: Item {
                            Rectangle {
                                id: deviceMenuBg
                                anchors.fill: parent
                                color: Qt.rgba(Theme.surface0.r, Theme.surface0.g, Theme.surface0.b, 0.95)
                                border.width: 1
                                border.color: Theme.accent
                            }
                            MultiEffect {
                                source: deviceMenuBg
                                anchors.fill: deviceMenuBg
                                shadowEnabled: true
                                shadowColor: Theme.accent
                                shadowBlur: 2.0
                                blurMax: 32
                            }
                        }

                        // Fase 14 (1:1 Map): binds every axis/hat/button on
                        // this device straight through to the same index on
                        // a chosen vJoy device - see
                        // ProfileEditorViewModel::create1to1Mapping(). Same
                        // diagonal-line glyph the old standalone "1:1 Map"
                        // ToolButton used before this action moved into the
                        // menu.
                        AppMenuItem {
                            text: qsTr("1:1 Map")
                            iconData: "M5 19L19 5"
                            onTriggered: oneToOnePopup.openFor(root.systemPath, root.deviceName, root.inputs)
                        }

                        // Fase SC-7.12: starts a Smart Swap (see swapToPopup
                        // below) with this device as the source.
                        AppMenuItem {
                            text: qsTr("Swap To...")
                            iconData: "M4 9h15l-4 -4 M19 9l-4 4 M20 15H5l4 -4 M5 15l4 4"
                            onTriggered: swapToPopup.open()
                        }

                        // Fase SC-7.5: clears every binding on this device
                        // outright - gated behind clearConfirmPopup, not
                        // fired straight from onTriggered (a single misclick
                        // used to wipe every binding with no way back).
                        AppMenuItem {
                            text: qsTr("Clear All")
                            iconData: "M8 5V4h8v1 M4 5h16 M5 5v15a2 2 0 0 0 2 2h10a2 2 0 0 0 2 -2V5 M9 9v9 M12 9v9 M15 9v9"
                            onTriggered: clearConfirmPopup.open()
                        }

                        // HidHide cloak/uncloak (Fase 11): omitted entirely
                        // (not just disabled) when HidHide isn't installed -
                        // there is nothing useful the user can do with this
                        // control in that case. Same open/closed-eye glyph
                        // pair the old standalone cloak button used, swapped
                        // live with root.cloaked same as before.
                        AppMenuItem {
                            visible: root.hidHideInstalled
                            iconData: root.cloaked
                                ? "M17.94 17.94A10.07 10.07 0 0 1 12 20c-7 0 -11 -8 -11 -8a18.45 18.45 0 0 1 5.06 -5.94M9.9 4.24A9.12 9.12 0 0 1 12 4c7 0 11 8 11 8a18.5 18.5 0 0 1 -2.16 3.19m-6.72 -1.07a3 3 0 1 1 -4.24 -4.24M1 1l22 22"
                                : "M1 12s4 -8 11 -8 11 8 11 8 -4 8 -11 8 -11 -8 -11 -8z M12 9a3 3 0 1 0 0 6 3 3 0 1 0 0 -6z"
                            text: root.cloaked ? qsTr("Cloaked") : qsTr("Visible")
                            onTriggered: {
                                const newState = !root.cloaked;
                                profileEditorViewModel.setDeviceCloaked(root.systemPath, newState);
                                root.cloaked = profileEditorViewModel.isDeviceCloaked(root.systemPath);
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Theme.surface1
            }

            // Owns the vertical scroll for this device's own axis/button
            // list - each tab used to share one Flickable across every
            // device (ProfileEditorView.qml's old scrollFlick); now that
            // only the active tab's DeviceCard is shown at a time, each one
            // needs to scroll its own potentially-hundreds-of-rows list
            // within the fixed height the StackLayout page gives it.
            Flickable {
                id: inputScrollFlick
                // UI pass: capped + centered instead of raw fillWidth - on
                // wide monitors the row content (300px name column + a
                // handful of binding controls, see InputRow.qml) never
                // comes close to using a full ultrawide card, so letting it
                // stretch just left a dead expanse of empty surface0 to the
                // right of every row. fillWidth stays on so this only
                // narrows on screens where 1200px wouldn't fit anyway.
                Layout.fillWidth: true
                Layout.maximumWidth: 1200
                Layout.alignment: Qt.AlignHCenter
                Layout.fillHeight: true
                Layout.topMargin: Theme.spacingXs
                clip: true
                contentWidth: width
                contentHeight: inputColumn.implicitHeight
                boundsBehavior: Flickable.StopAtBounds

                Behavior on contentY {
                    NumberAnimation { duration: Theme.animSlow; easing.type: Easing.OutCubic }
                }

                ScrollBar.vertical: ScrollBar { }

                /// Quick Bind auto-scroll (Fase 10.8, ported to Tabs): brings
                /// item (an InputRow returned by highlightInput() above)
                /// into view, clamped so it never scrolls past either end.
                function scrollToItem(item) {
                    if (!item) {
                        return;
                    }
                    const pt = item.mapToItem(inputColumn, 0, 0);
                    const maxContentY = Math.max(0, inputScrollFlick.contentHeight - inputScrollFlick.height);
                    inputScrollFlick.contentY = Math.max(0, Math.min(pt.y - Theme.spacingMd, maxContentY));
                }

                ColumnLayout {
                    id: inputColumn
                    width: inputScrollFlick.width
                    spacing: 4

                    Repeater {
                        id: inputRepeater
                        model: root.inputs
                        delegate: InputRow {
                            Layout.fillWidth: true
                            // Sprint Final: collapses the row's own layout
                            // footprint to nothing when hidden, on top of
                            // `visible` below - QtQuick.Layouts already
                            // excludes invisible items from sizing/spacing
                            // on its own, but this is cheap, explicit
                            // insurance against ever leaving a gap where a
                            // hidden row used to be.
                            Layout.preferredHeight: visible ? 28 : 0

                            // Fase SC-7.11: a "[Legacy]"/orphan-device row is
                            // now a lost-mappings tray (Fase SC-7.9's partial
                            // Smart Swap) rather than a full device, so its
                            // 8 axes/128 buttons worth of Unbound ghost
                            // entries just bury the handful that actually
                            // still need attention - hide anything unbound
                            // there. A real device is untouched
                            // (root.vendorProduct won't match), so every one
                            // of its physical inputs still always shows.
                            //
                            // Fase SC-7.13: reads this delegate's own
                            // hasBinding property (set two lines below,
                            // already wired up to update live as C++ pushes
                            // changes) instead of the raw
                            // modelData.hasBinding - QML has no way to know
                            // a plain modelData property changed underneath
                            // it unless the model itself emits a change
                            // signal for that role.
                            //
                            // Sprint Final: root.hideUnbound folds into this
                            // same condition - same reasoning, just
                            // user-controlled instead of hardcoded to the
                            // "[Legacy]" case.
                            visible: (root.vendorProduct !== "Offline / Imported Device" || hasBinding)
                                && (!root.hideUnbound || hasBinding)

                            devicePath: root.systemPath
                            inputName: modelData.name
                            inputKind: modelData.kind
                            hasBinding: modelData.hasBinding
                            bindingLabel: modelData.bindingLabel
                            actionNote: modelData.actionNote || ""
                            onClicked: root.inputClicked(root.systemPath, inputName, inputKind, hasBinding)
                            onCurvesRequested: root.curvesRequested(root.systemPath, inputName)
                        }
                    }
                }
            }
        }
    }

    MultiEffect {
        source: cardBackground
        anchors.fill: cardBackground
        shadowEnabled: true
        shadowColor: Theme.shadowColor
        shadowBlur: 0.6
        shadowVerticalOffset: 6
        blurMax: 24
    }

    OneToOnePopup {
        id: oneToOnePopup
    }

    ConfirmationPopup {
        id: clearConfirmPopup
        titleText: qsTr("Clear All Bindings")
        messageText: qsTr("Are you sure you want to clear ALL bindings on this device? This action cannot be undone.")
        onAccepted: profileEditorViewModel.clearDeviceBindings(root.systemPath)
    }

    // Fase SC-7.12: "Swap To..." destination picker - a checkbox-like list
    // (highlight + accent border on the selected row) rather than a
    // ComboBox, per the user's own request to make an accidental wrong-
    // device click harder than a dropdown makes it. swapDevices() itself is
    // still the Smart Swap from Fase SC-7.9 - only bindings that fit the
    // destination's real input count move; anything else stays behind here.
    Popup {
        id: swapToPopup
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: 400
        modal: true
        focus: true
        padding: Theme.spacingLg

        property int selectedIndex: -1
        property string selectedSystemPath: ""

        onClosed: {
            selectedIndex = -1;
            selectedSystemPath = "";
        }

        background: Rectangle {
            color: Theme.surface0
            radius: Theme.radiusLarge
            border.color: Theme.surface1
            border.width: 1
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.spacingMd

            Text {
                text: qsTr("Swap To...")
                color: Theme.text
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }
            Text {
                text: qsTr("Select the destination device. Only compatible bindings will be moved.")
                color: Theme.subtext0
                font.pixelSize: 12
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Theme.surface1
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingXs

                Repeater {
                    model: profileEditorViewModel
                    delegate: Rectangle {
                        visible: model.systemPath !== root.systemPath
                        Layout.fillWidth: true
                        Layout.preferredHeight: 40
                        color: swapToPopup.selectedIndex === index ? Theme.surface1 : "transparent"
                        radius: Theme.radiusMedium
                        border.color: swapToPopup.selectedIndex === index ? Theme.accent : "transparent"

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: Theme.spacingSm
                            Text {
                                text: model.deviceName
                                color: Theme.text
                                font.pixelSize: 14
                                Layout.fillWidth: true
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                swapToPopup.selectedIndex = index;
                                swapToPopup.selectedSystemPath = model.systemPath;
                            }
                        }
                    }
                }
            }

            Item { Layout.fillHeight: true } // spacer

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingMd
                Item { Layout.fillWidth: true }
                ToolButton {
                    label: qsTr("Cancel")
                    onClicked: swapToPopup.close()
                }
                ToolButton {
                    label: qsTr("Apply Swap")
                    enabled: swapToPopup.selectedIndex !== -1
                    opacity: enabled ? 1.0 : 0.5
                    onClicked: {
                        const fromRow = profileEditorViewModel.deviceRowForSystemPath(root.systemPath);
                        const toRow = profileEditorViewModel.deviceRowForSystemPath(swapToPopup.selectedSystemPath);
                        profileEditorViewModel.swapDevices(fromRow, toRow);
                        swapToPopup.close();
                    }
                }
            }
        }
    }
}
