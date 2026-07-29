import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GremblingNexus

// 1:1 Map (Sprint 1 refactor): moves DeviceCard's old inline target-picker +
// "1:1 MAP" button pair into its own modal with a live Preview of exactly
// which physical input lands on which vJoy slot before anything is applied -
// see profileEditorViewModel.create1to1Mapping() for the actual binding
// logic this only previews and then triggers. Self-contained like
// AxisSplitterPopup/SequencePopup: DeviceCard just calls openFor() and this
// closes itself on Apply.
Popup {
    id: root

    modal: true
    focus: true
    parent: Overlay.overlay // Fase 20.15: escape the opening popup's own coordinate system
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    width: 460
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    property string devicePath: ""
    property string deviceName: ""

    /// Snapshot of the device's own "inputs" model-role array ({name, kind,
    /// inputIndex, ...} per physical axis/button/hat) - the exact same data
    /// DeviceCard already receives from profileEditorViewModel and feeds to
    /// its InputRow Repeater, just handed down one level. Reusing it means
    /// this popup needs no new C++ (per the strict "don't touch C++" rule):
    /// every name the Preview shows already comes straight from the
    /// ViewModel's own model.
    property var inputs: []

    /// Per-input target index overrides (Sprint: 1:1 Map manual override) -
    /// keyed by the input's own "name" (matches create1to1Mapping()'s own
    /// targetOverrides docs), value is the 0-based target axis/button index
    /// to use instead of the input's own inputIndex. Pre-seeded to
    /// inputIndex for every axis/button row in openFor() below, so a row the
    /// user never touches still round-trips through Apply exactly as before
    /// this override feature existed - only rows the user actually edits
    /// diverge from straight 1:1. POV hat rows are deliberately never
    /// added here (see create1to1Mapping()'s own docs on why a hat isn't
    /// overridable), so they always keep going through straight 1:1.
    property var overrides: ({})

    readonly property var vjoyAxisNames: ["X", "Y", "Z", "Rx", "Ry", "Rz", "Slider", "Dial"]

    function isHatRow(name) {
        return name.indexOf("POV ") === 0;
    }

    /// Capped like ActionPickerPopup's actionFieldsScroll (see Memory.md's
    /// "ScrollView dentro de un Popup no encoge" entry): capping the
    /// ScrollView's own Layout.preferredHeight against its inner content's
    /// real implicitHeight is what actually makes the scrollbar appear once
    /// content overflows - capping the Popup itself does not reliably work.
    readonly property int maxPreviewAreaHeight: Overlay.overlay
        ? Math.max(160, Math.round(Overlay.overlay.height * 0.4))
        : 260

    function openFor(devicePath_, deviceName_, inputs_) {
        root.devicePath = devicePath_;
        root.deviceName = deviceName_;
        root.inputs = inputs_ || [];
        targetCombo.setFromTarget(null);

        const seeded = {};
        for (const input of root.inputs) {
            if (!root.isHatRow(input.name)) {
                seeded[input.name] = input.inputIndex;
            }
        }
        root.overrides = seeded;
        buttonShiftStartField.text = "1";
        buttonShiftEndField.text = "128";
        axisShiftCombo.currentIndex = 0;

        root.open();
    }

    /// Bulk-shift (Sprint: 1:1 Map manual override, QoL): rewrites every
    /// non-hat BUTTON row's override in one shot, so the whole block starts
    /// at buttonShiftStartField's value instead of typing each row by hand -
    /// button inputIndex is already a 0-based sequential rank within its own
    /// kind (see makeDeviceEntry()), so "target = (start-1) + inputIndex"
    /// alone reproduces the same relative order the auto 1:1 pass would have
    /// used, just offset. Reads both fields directly (rather than taking
    /// parameters) since Start and End jointly determine the range and
    /// either one changing needs to re-run the exact same computation.
    ///
    /// A row whose computed target would land past buttonShiftEndField's
    /// value (Sprint: range cap) gets the -1 "skip" sentinel instead (see
    /// create1to1Mapping()'s own docs) - left unmapped rather than
    /// overflowing onto a target slot past what the user asked for.
    ///
    /// Unconditionally overwrites EVERY non-hat button row, including ones
    /// already hand-edited via a single row's own field below - a bulk
    /// shift is meant to re-lay-out the whole block, and silently sparing
    /// "already touched" rows would leave root.overrides (what Apply
    /// actually submits) disagreeing with a stale row that LOOKS untouched.
    /// Forces the Preview to fully rebuild (root.inputs reassigned to a
    /// fresh array, not just root.overrides) so every row's control
    /// re-binds live against the new values - reassigning root.overrides
    /// alone would only refresh rows whose own binding hadn't already been
    /// torn off by a prior hand-edit (see those bindings' own docs), leaving
    /// their displayed text stale relative to what actually gets submitted.
    function applyButtonShift() {
        const start1Based = parseInt(buttonShiftStartField.text) || 1;
        const end1Based = parseInt(buttonShiftEndField.text) || 128;
        const next = Object.assign({}, root.overrides);
        for (const input of root.inputs) {
            if (input.kind !== "axis" && !root.isHatRow(input.name)) {
                const target0Based = (start1Based - 1) + input.inputIndex;
                next[input.name] = target0Based > (end1Based - 1) ? -1 : target0Based;
            }
        }
        root.overrides = next;
        root.inputs = root.inputs.slice();
    }

    /// Same idea as applyButtonShift() above, for AXIS rows - startAxisIndex
    /// is 0-based (an index into vjoyAxisNames), clamped so a device with
    /// more physical axes than vJoy has slots for just piles the overflow
    /// onto the last axis rather than going out of range.
    function applyAxisShift(startAxisIndex) {
        const next = Object.assign({}, root.overrides);
        for (const input of root.inputs) {
            if (input.kind === "axis") {
                next[input.name] = Math.min(root.vjoyAxisNames.length - 1, startAxisIndex + input.inputIndex);
            }
        }
        root.overrides = next;
        root.inputs = root.inputs.slice();
    }

    /// "vJoy 3 : Axis X" for an axis, "vJoy 3 : Btn 1" for anything else
    /// (buttons AND hat directions alike - a hat's own name already reads
    /// e.g. "Hat 1 Up", so only the "Button " -> "Btn " abbreviation
    /// actually needs special-casing here, matching the Architect's own
    /// example row format).
    function targetLabel(name, kind) {
        const shortName = kind === "axis" ? name : name.replace("Button ", "Btn ");
        return qsTr("vJoy %1 : %2").arg(targetCombo.targetOutputId).arg(shortName);
    }

    background: Rectangle {
        color: Theme.surface0
        radius: Theme.radiusMedium
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.08)
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacingMd

        Item { Layout.preferredHeight: Theme.spacingMd }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            spacing: 2

            Text { text: qsTr("1:1 Map"); color: Theme.text; font.pixelSize: 17; font.weight: Font.DemiBold }
            Text {
                text: root.deviceName
                color: Theme.subtext0
                font.pixelSize: 12
            }
        }

        // --- Target device -------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            spacing: 2

            Text { text: qsTr("Target Output Device"); color: Theme.subtext0; font.pixelSize: 11 }
            OutputDeviceCombo { id: targetCombo }

            // 1:1 Map is vJoy-only - create1to1Mapping() has no Xbox target
            // parameter at all (a HOTAS' up to 8 axes/4 hats/128 buttons has
            // no safe direct passthrough onto an Xbox 360 pad's 6 axes/15
            // buttons), so Apply below disables instead of silently mapping
            // to vJoy anyway while the combo shows "Xbox 360".
            Text {
                visible: targetCombo.isXbox
                text: qsTr("1:1 Map only supports vJoy targets - pick a vJoy device above.")
                color: Theme.danger
                font.pixelSize: 11
                font.italic: true
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }

        // --- Bulk shift (QoL: faster than editing every row by hand) ------
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            spacing: Theme.spacingMd

            ColumnLayout {
                spacing: 2
                Text { text: qsTr("Buttons start at"); color: Theme.subtext0; font.pixelSize: 11 }
                RowLayout {
                    spacing: Theme.spacingXs
                    TextField {
                        id: buttonShiftStartField
                        Layout.preferredWidth: 56
                        implicitHeight: 26
                        text: "1"
                        horizontalAlignment: Text.AlignHCenter
                        color: Theme.text
                        font.pixelSize: 12
                        validator: IntValidator { bottom: 1; top: 128 }
                        background: Rectangle {
                            color: Theme.surface0
                            radius: Theme.radiusSmall
                            border.width: 1
                            border.color: Qt.rgba(1, 1, 1, 0.08)
                        }
                        onEditingFinished: {
                            const parsed = parseInt(buttonShiftStartField.text);
                            buttonShiftStartField.text = isNaN(parsed) ? 1 : Math.max(1, Math.min(128, parsed));
                            root.applyButtonShift();
                            buttonShiftStartField.focus = false;
                        }
                    }
                    Text { text: qsTr("to"); color: Theme.overlay0; font.pixelSize: 11 }
                    TextField {
                        id: buttonShiftEndField
                        Layout.preferredWidth: 56
                        implicitHeight: 26
                        text: "128"
                        horizontalAlignment: Text.AlignHCenter
                        color: Theme.text
                        font.pixelSize: 12
                        validator: IntValidator { bottom: 1; top: 128 }
                        background: Rectangle {
                            color: Theme.surface0
                            radius: Theme.radiusSmall
                            border.width: 1
                            border.color: Qt.rgba(1, 1, 1, 0.08)
                        }
                        // "End" caps the range instead of just offsetting it -
                        // any button this shift would otherwise push past
                        // this number is left unmapped (see
                        // applyButtonShift()'s own docs) rather than
                        // overflowing onto a target slot the user never
                        // asked for.
                        onEditingFinished: {
                            const parsed = parseInt(buttonShiftEndField.text);
                            buttonShiftEndField.text = isNaN(parsed) ? 128 : Math.max(1, Math.min(128, parsed));
                            root.applyButtonShift();
                            buttonShiftEndField.focus = false;
                        }
                    }
                }
            }

            ColumnLayout {
                spacing: 2
                Text { text: qsTr("Axes start at"); color: Theme.subtext0; font.pixelSize: 11 }
                AppComboBox {
                    id: axisShiftCombo
                    Layout.preferredWidth: 90
                    model: root.vjoyAxisNames
                    currentIndex: 0
                    onActivated: (idx) => root.applyAxisShift(idx)
                }
            }

            Item { Layout.fillWidth: true }

            Text {
                Layout.preferredWidth: 150
                text: qsTr("Buttons past \"to\" are left unmapped. Any row edited by hand can still be changed individually below.")
                color: Theme.overlay0
                font.pixelSize: 10
                font.italic: true
                wrapMode: Text.WordWrap
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            height: 1
            color: Qt.rgba(1, 1, 1, 0.1)
        }

        // --- Preview ---------------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            spacing: 2

            Text { text: qsTr("Preview"); color: Theme.subtext0; font.pixelSize: 11; font.weight: Font.DemiBold }

            ScrollView {
                id: previewScroll
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(previewContent.implicitHeight, root.maxPreviewAreaHeight)
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                contentWidth: availableWidth

                ColumnLayout {
                    id: previewContent
                    width: previewScroll.availableWidth
                    spacing: 1

                    Repeater {
                        model: root.inputs
                        delegate: RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingXs

                            Text {
                                text: modelData.name
                                color: Theme.text
                                font.pixelSize: 12
                                Layout.preferredWidth: 140
                                elide: Text.ElideRight
                            }
                            Text {
                                text: "--->"
                                color: Theme.overlay0
                                font.pixelSize: 11
                            }

                            // POV hat directions always go straight through
                            // 1:1 (see overrides' own docs) - just the same
                            // read-only label this row always showed.
                            Text {
                                visible: root.isHatRow(modelData.name)
                                text: root.targetLabel(modelData.name, modelData.kind)
                                color: Theme.accent
                                font.pixelSize: 12
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }

                            // Editable target for axes/buttons - lets the
                            // user redirect this one input to a different
                            // vJoy axis/button than its own index, instead
                            // of always straight-through 1:1.
                            RowLayout {
                                visible: !root.isHatRow(modelData.name)
                                Layout.fillWidth: true
                                spacing: Theme.spacingXs

                                Text {
                                    text: qsTr("vJoy %1 :").arg(targetCombo.targetOutputId)
                                    color: Theme.accent
                                    font.pixelSize: 12
                                }

                                AppComboBox {
                                    visible: modelData.kind === "axis"
                                    Layout.preferredWidth: 90
                                    model: root.vjoyAxisNames
                                    // Live binding (not read once at creation)
                                    // so applyAxisShift()'s bulk rewrite of
                                    // root.overrides visibly moves this row
                                    // too, as long as the user hasn't picked
                                    // a value here by hand yet - selecting one
                                    // (onActivated below) is itself an
                                    // interactive write that tears this
                                    // binding off, same as any QML Control.
                                    currentIndex: root.overrides[modelData.name] !== undefined
                                        ? Math.min(root.vjoyAxisNames.length - 1, root.overrides[modelData.name])
                                        : Math.min(root.vjoyAxisNames.length - 1, modelData.inputIndex)
                                    onActivated: (idx) => root.overrides[modelData.name] = idx
                                }

                                TextField {
                                    id: targetButtonField
                                    visible: modelData.kind !== "axis"
                                    Layout.preferredWidth: 56
                                    implicitHeight: 26
                                    // Same live-binding-until-hand-edited
                                    // story as the axis combo above. The -1
                                    // skip sentinel (see applyButtonShift()'s
                                    // "End" range cap and create1to1Mapping()'s
                                    // own docs) shows as an empty field with
                                    // a placeholder instead of a bogus "0" -
                                    // typing a real number here still
                                    // manually un-skips just this one row.
                                    text: root.overrides[modelData.name] === -1 ? ""
                                        : (root.overrides[modelData.name] !== undefined
                                            ? root.overrides[modelData.name] : modelData.inputIndex) + 1
                                    placeholderText: root.overrides[modelData.name] === -1 ? qsTr("skip") : ""
                                    horizontalAlignment: Text.AlignHCenter
                                    color: Theme.text
                                    font.pixelSize: 12
                                    validator: IntValidator { bottom: 1; top: 128 }
                                    background: Rectangle {
                                        color: Theme.surface0
                                        radius: Theme.radiusSmall
                                        border.width: 1
                                        border.color: Qt.rgba(1, 1, 1, 0.08)
                                    }
                                    // Displayed 1-based ("Btn 1" onward, same
                                    // convention as ActionPickerPopup's own
                                    // Target Button stepper) - stored 0-based
                                    // to match inputIndex/create1to1Mapping().
                                    // Left blank (rather than reverting to
                                    // straight-through) keeps this row
                                    // skipped, matching the placeholder text.
                                    onEditingFinished: {
                                        if (targetButtonField.text === "") {
                                            root.overrides[modelData.name] = -1;
                                            return;
                                        }
                                        const parsed = parseInt(targetButtonField.text);
                                        const clamped = isNaN(parsed) ? (modelData.inputIndex + 1)
                                            : Math.max(1, Math.min(128, parsed));
                                        targetButtonField.text = clamped;
                                        root.overrides[modelData.name] = clamped - 1;
                                        targetButtonField.focus = false;
                                    }
                                }
                            }
                        }
                    }

                    Text {
                        visible: root.inputs.length === 0
                        text: qsTr("No inputs on this device.")
                        color: Theme.overlay0
                        font.pixelSize: 12
                        font.italic: true
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            Layout.topMargin: Theme.spacingXs
            height: 1
            color: Qt.rgba(1, 1, 1, 0.1)
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            Layout.bottomMargin: Theme.spacingMd
            spacing: Theme.spacingSm

            Item { Layout.fillWidth: true }

            ToolButton {
                label: qsTr("Cancel")
                onClicked: root.close()
            }
            ToolButton {
                label: qsTr("Apply")
                enabled: !targetCombo.isXbox && root.inputs.length > 0
                opacity: enabled ? 1.0 : 0.5
                onClicked: {
                    if (profileEditorViewModel.create1to1Mapping(root.devicePath, targetCombo.targetOutputId,
                                                                   root.overrides)) {
                        root.close();
                    }
                }
            }
        }
    }
}
