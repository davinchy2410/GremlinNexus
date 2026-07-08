import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Effects
import GremblingEx

// Modal "Action Picker" (Fase 10.8, extended Fase 10.9): opened by clicking
// an InputRow in the Profiles screen, this is the single shared popup every
// DeviceCard/InputRow click routes through (one instance, re-targeted per
// click, rather than one Popup per row - see ProfileEditorView.qml). Lets
// the user choose what a physical axis/button should do, and Apply calls
// profileEditorViewModel.bindAction() with a JSON-encoded actionType/
// target*/parameters object matching ProfileManager's binding schema.
//
// Available action types depend on inputKind (Fase 10.9): Keyboard/Macro's
// quick timed-press both drive processButton() only (KeyboardHandler/
// MacroHandler ignore processAxis() entirely - see their own class docs),
// so they are meaningless for an axis input and are swapped out for
// "Axis-to-Button" (AxisSplitterHandler - see AxisSplitterPopup.qml)
// instead; "vJoy Remap" behaves correctly either way (CurveHandler for
// axes, ButtonRemapHandler for buttons) and stays available for both. The
// Macro tab's own quick vJoy timed-press shorthand (Fase 10.8) is still
// available for a simple case; "Open Macro Editor…" opens the full
// record/edit keyboard-macro editor (Fase 10.9, MacroEditorPopup.qml) for
// everything else.
Popup {
    id: root

    modal: true
    focus: true
    x: Overlay.overlay ? (Overlay.overlay.width - width) / 2 : 0
    y: Overlay.overlay ? (Overlay.overlay.height - height) / 2 : 0
    // Fase SC-7.7: widened from 380 - the bottom button row can now show up
    // to 5 ToolButtons at once (Copy, Paste, Unbind, Cancel, Apply), which
    // didn't comfortably fit the popup's old width.
    width: 440
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    property string devicePath: ""
    property string inputName: ""
    property string inputKind: "axis" // "axis" | "button"
    property bool hasBinding: false

    property int capturedScanCode: -1
    property string capturedKeyLabel: ""

    // Fase 20.21: whatever Sequence/Rotary steps are already bound to the
    // input openFor() was just called for (if any) - handed to
    // SequencePopup.openFor() so it can restore its own list instead of
    // always resetting to its two hardcoded default steps.
    property var existingSequenceSteps: null

    // Memoria para auto-incremento (Fase UX): recuerda la última asignación
    // de vJoy para que el usuario no tenga que cambiar manualmente "Botón 1"
    // a "Botón 2", "Botón 3", etc. al mapear múltiples inputs de corrido.
    property int lastVjoyDevice: 0
    property int lastVjoyButton: 0
    property int lastVjoyAxis: 0
    property int lastVjoyHat: 0

    readonly property var vjoyDeviceNames: Array.from({length: 16}, (_, i) => "vJoy Device " + (i + 1))
    readonly property var vjoyAxisNames: ["X", "Y", "Z", "Rx", "Ry", "Rz", "Slider", "Dial"]

    // Action types offered depend on inputKind - see the file-level comment
    // above for why Keyboard/Macro only make sense for a button input.
    readonly property var actionTypesForAxis: ["vJoy Remap", "Axis-to-Button", "Split Axis", "Merge Axis",
        "Condition / Modifier Check"]
    readonly property var actionTypesForButton: ["vJoy Remap", "vJoy Hat Remap", "Keyboard", "Macro",
        "Tempo (Short/Long Press)", "Sequence / Rotary", "Shift / Modifier", "Mode Toggle (Sticky)",
        "Condition / Modifier Check"]
    readonly property var hatDirectionNames: ["Up", "Right", "Down", "Left"]

    // Qt::Key -> PS/2 Set 1 hardware scan code (see KeyboardHandler /
    // IKeyboardBackend - a *hardware* scan code, not a virtual-key code).
    // Covers letters, digits, function keys and the common control keys -
    // not the full 104-key table, but enough for the overwhelming majority
    // of real joystick/HOTAS keyboard-emulation bindings; anything outside
    // this set reports as "Unsupported key" rather than silently binding
    // the wrong key.
    readonly property var keyScanCodes: ({
        [Qt.Key_A]: 0x1E, [Qt.Key_B]: 0x30, [Qt.Key_C]: 0x2E, [Qt.Key_D]: 0x20,
        [Qt.Key_E]: 0x12, [Qt.Key_F]: 0x21, [Qt.Key_G]: 0x22, [Qt.Key_H]: 0x23,
        [Qt.Key_I]: 0x17, [Qt.Key_J]: 0x24, [Qt.Key_K]: 0x25, [Qt.Key_L]: 0x26,
        [Qt.Key_M]: 0x32, [Qt.Key_N]: 0x31, [Qt.Key_O]: 0x18, [Qt.Key_P]: 0x19,
        [Qt.Key_Q]: 0x10, [Qt.Key_R]: 0x13, [Qt.Key_S]: 0x1F, [Qt.Key_T]: 0x14,
        [Qt.Key_U]: 0x16, [Qt.Key_V]: 0x2F, [Qt.Key_W]: 0x11, [Qt.Key_X]: 0x2D,
        [Qt.Key_Y]: 0x15, [Qt.Key_Z]: 0x2C,
        [Qt.Key_0]: 0x0B, [Qt.Key_1]: 0x02, [Qt.Key_2]: 0x03, [Qt.Key_3]: 0x04,
        [Qt.Key_4]: 0x05, [Qt.Key_5]: 0x06, [Qt.Key_6]: 0x07, [Qt.Key_7]: 0x08,
        [Qt.Key_8]: 0x09, [Qt.Key_9]: 0x0A,
        [Qt.Key_F1]: 0x3B, [Qt.Key_F2]: 0x3C, [Qt.Key_F3]: 0x3D, [Qt.Key_F4]: 0x3E,
        [Qt.Key_F5]: 0x3F, [Qt.Key_F6]: 0x40, [Qt.Key_F7]: 0x41, [Qt.Key_F8]: 0x42,
        [Qt.Key_F9]: 0x43, [Qt.Key_F10]: 0x44, [Qt.Key_F11]: 0x57, [Qt.Key_F12]: 0x58,
        [Qt.Key_Space]: 0x39, [Qt.Key_Return]: 0x1C, [Qt.Key_Enter]: 0x1C,
        [Qt.Key_Escape]: 0x01, [Qt.Key_Tab]: 0x0F, [Qt.Key_Backspace]: 0x0E,
        [Qt.Key_Shift]: 0x2A, [Qt.Key_Control]: 0x1D, [Qt.Key_Alt]: 0x38,
        [Qt.Key_Up]: 0x48, [Qt.Key_Down]: 0x50, [Qt.Key_Left]: 0x4B, [Qt.Key_Right]: 0x4D,
        [Qt.Key_CapsLock]: 0x3A,
    })

    // Small "-"/[value]/"+" stepper, reused for every bounded-integer field
    // below (target button, macro duration) - an inline component since
    // it's only ever used within this one popup.
    component IntStepper: RowLayout {
        id: stepper
        property int value: 0
        property int from: 0
        property int to: 127
        property int stepSize: 1
        spacing: Theme.spacingXs

        ToolButton {
            label: "−"
            Layout.preferredWidth: 28
            onClicked: stepper.value = Math.max(stepper.from, stepper.value - stepper.stepSize)
        }
        Text {
            text: stepper.value
            color: Theme.text
            font.pixelSize: 13
            font.weight: Font.DemiBold
            Layout.preferredWidth: 40
            horizontalAlignment: Text.AlignHCenter
        }
        ToolButton {
            label: "+"
            Layout.preferredWidth: 28
            onClicked: stepper.value = Math.min(stepper.to, stepper.value + stepper.stepSize)
        }
    }

    /// Resets every field to a sane default and opens the popup targeted at
    /// one specific physical input - called from ProfileEditorView.qml in
    /// response to a DeviceCard's inputClicked.
    function openFor(devicePath_, inputName_, inputKind_, hasBinding_) {
        root.devicePath = devicePath_;
        root.inputName = inputName_;
        root.inputKind = inputKind_;
        root.hasBinding = !!hasBinding_;
        actionTypeCombo.currentIndex = 0;
        vjoyDeviceCombo.currentIndex = root.lastVjoyDevice;
        vjoyAxisCombo.currentIndex = root.lastVjoyAxis;
        root.lastVjoyButton = profileEditorViewModel.nextAvailableVjoyButton(vjoyDeviceCombo.currentIndex + 1);
        vjoyTargetButton.value = root.lastVjoyButton + 1;
        macroDeviceCombo.currentIndex = 0;
        macroTargetButton.value = 1;
        macroDuration.value = 100;
        root.capturedScanCode = -1;
        root.capturedKeyLabel = "";
        keyCapturing = false;
        root.existingSequenceSteps = null;
        tempoThresholdMs.value = 300;
        tempoPulseMs.value = 50;
        tempoActionADeviceCombo.currentIndex = 0;
        tempoActionAButton.value = 1;
        tempoActionBDeviceCombo.currentIndex = 0;
        tempoActionBButton.value = 2;
        shiftModeCombo.currentIndex = 0;
        condDeviceCombo.currentIndex = 0;
        condButtonStepper.value = 0;
        condRequirePressed.currentIndex = 0;
        condVjoyDeviceCombo.currentIndex = 0;
        condVjoyAxisCombo.currentIndex = 0;
        condVjoyTargetButton.value = 1;
        hatDeviceCombo.currentIndex = 0;
        hatTargetCombo.currentIndex = 0;
        hatDirectionCombo.currentIndex = 0;
        splitModeCombo.currentIndex = 0;
        splitLowerVjoyCombo.currentIndex = 0;
        splitLowerAxisCombo.currentIndex = 0;
        splitLowerInvertCheck.checked = false;
        splitUpperVjoyCombo.currentIndex = 0;
        splitUpperAxisCombo.currentIndex = 1;
        splitUpperInvertCheck.checked = false;
        mergeDeviceCombo.currentIndex = 0;
        mergeAxisCombo.currentIndex = 0;
        mergeModeCombo.currentIndex = 0;
        invertAxisCheck.checked = false;

        // Fase 20.17: re-populate every field from whatever's actually
        // bound here already, instead of always resetting to blank
        // defaults - re-opening an input the user already configured now
        // shows (and lets them tweak) what they set last time, rather than
        // silently discarding it if they hit Apply again without noticing.
        let restoredExisting = false;
        const existingJsonStr = profileEditorViewModel.getActionDataJson(
            devicePath_, inputName_, profileEditorViewModel.currentMode);
        if (existingJsonStr !== "") {
            try {
                const actionData = JSON.parse(existingJsonStr);
                const actionType = actionData.actionType;

                if (actionType === "CurveHandler" || actionType === "SmoothingHandler") {
                    actionTypeCombo.currentIndex = Math.max(0, root.actionTypesForAxis.indexOf("vJoy Remap"));
                    const wrapped = actionType === "SmoothingHandler" ? (actionData.wrappedAction || {}) : actionData;
                    vjoyDeviceCombo.currentIndex = (wrapped.targetOutputId || 1) - 1;
                    vjoyAxisCombo.currentIndex = wrapped.targetAxis || 0;
                    smoothingToggle.checked = actionType === "SmoothingHandler";
                    if (actionType === "SmoothingHandler" && actionData.parameters &&
                            actionData.parameters.smoothingFactor !== undefined) {
                        smoothingFactorSlider.value = actionData.parameters.smoothingFactor;
                    }
                    // Fase (new): "invert" always lives on the wrapped
                    // CurveHandler's own parameters - wrapped already points
                    // at actionData.wrappedAction when SmoothingHandler-
                    // wrapped, or actionData itself for a plain CurveHandler,
                    // so this one read covers both.
                    invertAxisCheck.checked = !!(wrapped.parameters && wrapped.parameters.invert);
                    restoredExisting = true;
                } else if (actionType === "ButtonRemapHandler") {
                    actionTypeCombo.currentIndex = Math.max(0, root.actionTypesForButton.indexOf("vJoy Remap"));
                    vjoyDeviceCombo.currentIndex = (actionData.targetOutputId || 1) - 1;
                    vjoyTargetButton.value = (actionData.targetButton || 0) + 1;
                    restoredExisting = true;
                } else if (actionType === "HatRemapHandler") {
                    actionTypeCombo.currentIndex = Math.max(0, root.actionTypesForButton.indexOf("vJoy Hat Remap"));
                    hatDeviceCombo.currentIndex = (actionData.targetOutputId || 1) - 1;
                    hatTargetCombo.currentIndex = actionData.targetHat || 0;
                    hatDirectionCombo.currentIndex = actionData.targetDirection || 0;
                    restoredExisting = true;
                } else if (actionType === "KeyboardHandler") {
                    actionTypeCombo.currentIndex = Math.max(0, root.actionTypesForButton.indexOf("Keyboard"));
                    if (actionData.parameters && actionData.parameters.scanCode !== undefined) {
                        root.capturedScanCode = actionData.parameters.scanCode;
                        root.capturedKeyLabel = "Key 0x" + root.capturedScanCode.toString(16);
                    }
                    restoredExisting = true;
                } else if (actionType === "MacroHandler") {
                    actionTypeCombo.currentIndex = Math.max(0, root.actionTypesForButton.indexOf("Macro"));
                    macroDeviceCombo.currentIndex = (actionData.targetOutputId || 1) - 1;
                    macroTargetButton.value = (actionData.targetButton || 0) + 1;
                    if (actionData.parameters && actionData.parameters.waitMs !== undefined) {
                        macroDuration.value = actionData.parameters.waitMs;
                    }
                    restoredExisting = true;
                } else if (actionType === "TempoHandler") {
                    actionTypeCombo.currentIndex =
                        Math.max(0, root.actionTypesForButton.indexOf("Tempo (Short/Long Press)"));
                    if (actionData.parameters && actionData.parameters.longPressMs !== undefined) {
                        tempoThresholdMs.value = actionData.parameters.longPressMs;
                    }
                    if (actionData.parameters && actionData.parameters.pulseDurationMs !== undefined) {
                        tempoPulseMs.value = actionData.parameters.pulseDurationMs;
                    }
                    if (actionData.shortAction && actionData.shortAction.actionType === "ButtonRemapHandler") {
                        tempoActionADeviceCombo.currentIndex = (actionData.shortAction.targetOutputId || 1) - 1;
                        tempoActionAButton.value = (actionData.shortAction.targetButton || 0) + 1;
                    }
                    if (actionData.longAction && actionData.longAction.actionType === "ButtonRemapHandler") {
                        tempoActionBDeviceCombo.currentIndex = (actionData.longAction.targetOutputId || 1) - 1;
                        tempoActionBButton.value = (actionData.longAction.targetButton || 0) + 1;
                    }
                    restoredExisting = true;
                } else if (actionType === "SequenceHandler") {
                    actionTypeCombo.currentIndex = Math.max(0, root.actionTypesForButton.indexOf("Sequence / Rotary"));
                    if (actionData.parameters && actionData.parameters.actions) {
                        const parsedSteps = [];
                        for (let i = 0; i < actionData.parameters.actions.length; i++) {
                            const act = actionData.parameters.actions[i];
                            parsedSteps.push({
                                targetOutputId: (act.targetOutputId || 1),
                                targetButton: (act.targetButton || 0)
                            });
                        }
                        root.existingSequenceSteps = parsedSteps;
                    }
                    restoredExisting = true;
                } else if (actionType === "ConditionHandler") {
                    actionTypeCombo.currentIndex =
                        Math.max(0, root.actionTypesForButton.indexOf("Condition / Modifier Check"));
                    if (actionData.parameters) {
                        const modRow = profileEditorViewModel.deviceRowForSystemPath(
                            actionData.parameters.modSystemPath || "");
                        if (modRow >= 0) {
                            condDeviceCombo.currentIndex = modRow;
                        }
                        condButtonStepper.value = actionData.parameters.modButtonIndex || 0;
                        condRequirePressed.currentIndex = actionData.parameters.requirePressed === false ? 1 : 0;
                    }
                    const wrapped = actionData.wrappedAction || {};
                    condVjoyDeviceCombo.currentIndex = (wrapped.targetOutputId || 1) - 1;
                    if (wrapped.actionType === "CurveHandler") {
                        condVjoyAxisCombo.currentIndex = wrapped.targetAxis || 0;
                    } else if (wrapped.actionType === "ButtonRemapHandler") {
                        condVjoyTargetButton.value = (wrapped.targetButton || 0) + 1;
                    }
                    restoredExisting = true;
                } else if (actionType === "TemporaryModeSwitch") {
                    actionTypeCombo.currentIndex = Math.max(0, root.actionTypesForButton.indexOf("Shift / Modifier"));
                    if (actionData.parameters && actionData.parameters.targetMode) {
                        const modeIdx = profileEditorViewModel.modes.indexOf(actionData.parameters.targetMode);
                        if (modeIdx >= 0) {
                            shiftModeCombo.currentIndex = modeIdx;
                        }
                    }
                    restoredExisting = true;
                } else if (actionType === "ModeSwitch") {
                    actionTypeCombo.currentIndex = Math.max(0, root.actionTypesForButton.indexOf("Mode Toggle (Sticky)"));
                    if (actionData.parameters && actionData.parameters.targetMode) {
                        const modeIdx = profileEditorViewModel.modes.indexOf(actionData.parameters.targetMode);
                        if (modeIdx >= 0) {
                            toggleModeCombo.currentIndex = modeIdx;
                        }
                    }
                    restoredExisting = true;
                } else if (actionType === "SplitAxisHandler") {
                    actionTypeCombo.currentIndex = Math.max(0, root.actionTypesForAxis.indexOf("Split Axis"));
                    const p = actionData.parameters || {};
                    splitModeCombo.currentIndex = p.splitMode || 0;
                    splitLowerVjoyCombo.currentIndex = (p.lowerTargetOutputId || 1) - 1;
                    splitLowerAxisCombo.currentIndex = p.lowerTargetAxis || 0;
                    splitLowerInvertCheck.checked = p.lowerInvert || false;
                    splitUpperVjoyCombo.currentIndex = (p.upperTargetOutputId || 1) - 1;
                    splitUpperAxisCombo.currentIndex = p.upperTargetAxis || 1;
                    splitUpperInvertCheck.checked = p.upperInvert || false;
                    restoredExisting = true;
                } else if (actionType === "MergeAxisHandler") {
                    actionTypeCombo.currentIndex = Math.max(0, root.actionTypesForAxis.indexOf("Merge Axis"));
                    vjoyDeviceCombo.currentIndex = (actionData.targetOutputId || 1) - 1;
                    vjoyAxisCombo.currentIndex = actionData.targetAxis || 0;
                    const p = actionData.parameters || {};
                    const r = profileEditorViewModel.deviceRowForSystemPath(p.otherSystemPath || "");
                    if (r >= 0) {
                        mergeDeviceCombo.currentIndex = r;
                        mergeAxisCombo.currentIndex = p.otherAxisIndex || 0;
                    }
                    mergeModeCombo.currentIndex = p.isSubtraction ? 1 : 0;
                    restoredExisting = true;
                }
            } catch (e) {
                console.warn("ActionPickerPopup: failed to parse existing binding JSON:", e);
            }
        }

        // Fase 20.3: a physical POV hat's own synthetic buttons are named
        // "POV H <Direction>" (see ProfileEditorViewModel::buttonDisplayName()) -
        // preselecting "vJoy Hat Remap" with the last-used vJoy hat and the
        // matching direction saves re-selecting all three by hand every time.
        // Only applies when there was nothing already bound here to restore
        // instead (see above) - an already-bound hat direction should show
        // what it's actually bound to, not a re-guess from the input's name.
        if (!restoredExisting && root.inputName.includes("POV")) {
            actionTypeCombo.currentIndex = Math.max(0, root.actionTypesForButton.indexOf("vJoy Hat Remap"));
            hatTargetCombo.currentIndex = root.lastVjoyHat;
            if (root.inputName.includes("Up")) hatDirectionCombo.currentIndex = 0;
            else if (root.inputName.includes("Right")) hatDirectionCombo.currentIndex = 1;
            else if (root.inputName.includes("Down")) hatDirectionCombo.currentIndex = 2;
            else if (root.inputName.includes("Left")) hatDirectionCombo.currentIndex = 3;
        }

        root.open();
    }

    property bool keyCapturing: false

    background: Item {
        Rectangle {
            id: backgroundRect
            anchors.fill: parent
            color: Theme.surface0
            radius: Theme.radiusMedium
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.08)
        }

        MultiEffect {
            source: backgroundRect
            anchors.fill: backgroundRect
            shadowEnabled: true
            shadowColor: Theme.shadowColor
            shadowBlur: 1.0
            shadowVerticalOffset: 8
            blurMax: 32
        }
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacingMd

        Item { Layout.preferredHeight: Theme.spacingMd } // top padding

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            spacing: 2

            Text { text: "Bind Action"; color: Theme.text; font.pixelSize: 17; font.weight: Font.DemiBold }
            Text {
                text: root.inputName + " · mode: " + profileEditorViewModel.currentMode
                color: Theme.subtext0
                font.pixelSize: 12
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            spacing: 2

            Text { text: "Action Type"; color: Theme.subtext0; font.pixelSize: 11 }
            AppComboBox {
                id: actionTypeCombo
                Layout.fillWidth: true
                model: root.inputKind === "axis" ? root.actionTypesForAxis : root.actionTypesForButton
            }
        }

        // --- vJoy Remap ------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            spacing: Theme.spacingSm
            // Fase 20.39: also visible for "Merge Axis" - that action's own
            // output is the same vJoy device+axis a plain vJoy Remap
            // targets, so it reuses vjoyDeviceCombo/vjoyAxisCombo below
            // instead of duplicating a second output picker.
            visible: actionTypeCombo.currentText === "vJoy Remap" || actionTypeCombo.currentText === "Merge Axis"

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: "Target vJoy Device"; color: Theme.subtext0; font.pixelSize: 11 }
                AppComboBox { id: vjoyDeviceCombo; Layout.fillWidth: true; model: root.vjoyDeviceNames }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                visible: root.inputKind === "axis"
                Text { text: "Target Axis"; color: Theme.subtext0; font.pixelSize: 11 }
                AppComboBox { id: vjoyAxisCombo; Layout.fillWidth: true; model: root.vjoyAxisNames }
            }

            RowLayout {
                Layout.fillWidth: true
                visible: root.inputKind === "button"
                Text { text: "Target Button"; color: Theme.subtext0; font.pixelSize: 11; Layout.fillWidth: true }
                IntStepper { id: vjoyTargetButton; from: 1; to: 128 }
            }

            RowLayout {
                Layout.fillWidth: true
                visible: root.inputKind === "axis" && actionTypeCombo.currentText === "vJoy Remap"
                Text {
                    text: "Smoothing / Anti-Jitter"
                    color: Theme.subtext0
                    font.pixelSize: 11
                    Layout.fillWidth: true
                }
                ToggleSwitch { id: smoothingToggle }
            }

            ValueSlider {
                id: smoothingFactorSlider
                Layout.fillWidth: true
                visible: root.inputKind === "axis" && actionTypeCombo.currentText === "vJoy Remap" && smoothingToggle.checked
                label: "Smoothing Strength"
                from: 0.0
                to: 0.99
                value: 0.3
                onMoved: (v) => smoothingFactorSlider.value = v
            }

            // Fase (new): physically flips the axis' output polarity - unlike
            // "inverting" a curve/points in the graphical curve editor (which
            // only reshapes the response into a "V" on a bipolar axis), this
            // negates the whole deadzone/curve-adjusted value before it's
            // mapped to the output range, so a push in one physical
            // direction actually drives the output the other way.
            RowLayout {
                Layout.fillWidth: true
                visible: root.inputKind === "axis" && actionTypeCombo.currentText === "vJoy Remap"
                Text {
                    text: "Invert Axis"
                    color: Theme.subtext0
                    font.pixelSize: 11
                    Layout.fillWidth: true
                }
                ToggleSwitch { id: invertAxisCheck }
            }

            // --- Merge Axis extra fields (Fase 20.39): the "other" physical
            // axis this vJoy output should be combined with - the output
            // picker above is shared with plain vJoy Remap.
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm
                visible: actionTypeCombo.currentText === "Merge Axis"

                Rectangle {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spacingXs
                    height: 1
                    color: Qt.rgba(1, 1, 1, 0.1)
                }

                Text {
                    text: "Combines this axis with another physical axis into the vJoy output above - e.g. two brake pedals merged into one rudder axis. Bind both physical axes to Merge Axis, each pointing at the other as its counterpart."
                    color: Theme.overlay0
                    font.pixelSize: 11
                    font.italic: true
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text { text: "Other Physical Device"; color: Theme.subtext0; font.pixelSize: 11 }
                    AppComboBox { id: mergeDeviceCombo; Layout.fillWidth: true; model: profileEditorViewModel.deviceDisplayNames() }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text { text: "Other Physical Axis"; color: Theme.subtext0; font.pixelSize: 11 }
                    AppComboBox { id: mergeAxisCombo; Layout.fillWidth: true; model: profileEditorViewModel.axisNamesForDevice(mergeDeviceCombo.currentIndex) }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text { text: "Combine Mode"; color: Theme.subtext0; font.pixelSize: 11 }
                    AppComboBox { id: mergeModeCombo; Layout.fillWidth: true; model: ["Additive (average)", "Differential (centered)"] }
                }
            }
        }

        // --- Split Axis (Fase 20.39) ------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            spacing: Theme.spacingSm
            visible: actionTypeCombo.currentText === "Split Axis"

            Text {
                text: "Splits this axis' travel in half: the lower 50% drives one vJoy axis (spanning its full range), the upper 50% drives a second vJoy axis (also spanning its full range)."
                color: Theme.overlay0
                font.pixelSize: 11
                font.italic: true
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: "Split Mode"; color: Theme.subtext0; font.pixelSize: 11 }
                AppComboBox {
                    id: splitModeCombo
                    Layout.fillWidth: true
                    model: ["Center-to-Edges (Joystick/Rudder)", "Sequential (Throttle/Slider)"]
                }
            }

            Text { text: "Lower Half (0% - 50%)"; color: Theme.subtext0; font.pixelSize: 11; font.weight: Font.DemiBold }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: "Target vJoy Device"; color: Theme.subtext0; font.pixelSize: 11 }
                AppComboBox { id: splitLowerVjoyCombo; Layout.fillWidth: true; model: root.vjoyDeviceNames }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: "Target Axis"; color: Theme.subtext0; font.pixelSize: 11 }
                AppComboBox { id: splitLowerAxisCombo; Layout.fillWidth: true; model: root.vjoyAxisNames }
            }
            RowLayout {
                Layout.fillWidth: true
                Text { text: "Invert"; color: Theme.subtext0; font.pixelSize: 11; Layout.fillWidth: true }
                ToggleSwitch { id: splitLowerInvertCheck }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacingXs
                height: 1
                color: Qt.rgba(1, 1, 1, 0.1)
            }

            Text { text: "Upper Half (50% - 100%)"; color: Theme.subtext0; font.pixelSize: 11; font.weight: Font.DemiBold }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: "Target vJoy Device"; color: Theme.subtext0; font.pixelSize: 11 }
                AppComboBox { id: splitUpperVjoyCombo; Layout.fillWidth: true; model: root.vjoyDeviceNames }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: "Target Axis"; color: Theme.subtext0; font.pixelSize: 11 }
                AppComboBox { id: splitUpperAxisCombo; Layout.fillWidth: true; model: root.vjoyAxisNames }
            }
            RowLayout {
                Layout.fillWidth: true
                Text { text: "Invert"; color: Theme.subtext0; font.pixelSize: 11; Layout.fillWidth: true }
                ToggleSwitch { id: splitUpperInvertCheck }
            }
        }

        // --- vJoy Hat Remap (Fase 19) -----------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            spacing: Theme.spacingSm
            visible: actionTypeCombo.currentText === "vJoy Hat Remap"

            Text {
                text: "Fires a discrete direction on a vJoy POV hat while this button is held - typically bound to one of a physical hat's own 4 synthetic Up/Right/Down/Left buttons."
                color: Theme.overlay0
                font.pixelSize: 11
                font.italic: true
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: "Target vJoy Device"; color: Theme.subtext0; font.pixelSize: 11 }
                AppComboBox { id: hatDeviceCombo; Layout.fillWidth: true; model: root.vjoyDeviceNames }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: "Target Hat"; color: Theme.subtext0; font.pixelSize: 11 }
                AppComboBox { id: hatTargetCombo; Layout.fillWidth: true; model: ["POV 1", "POV 2", "POV 3", "POV 4"] }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: "Target Direction"; color: Theme.subtext0; font.pixelSize: 11 }
                AppComboBox { id: hatDirectionCombo; Layout.fillWidth: true; model: root.hatDirectionNames }
            }
        }

        // --- Keyboard ----------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            spacing: Theme.spacingSm
            visible: actionTypeCombo.currentText === "Keyboard"

            Text { text: "Key"; color: Theme.subtext0; font.pixelSize: 11 }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 36
                radius: Theme.radiusSmall
                color: root.keyCapturing ? Theme.accent : Theme.surface1
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.08)
                Behavior on color { ColorAnimation { duration: Theme.animFast } }

                Text {
                    anchors.centerIn: parent
                    text: root.keyCapturing
                        ? "Press a key…"
                        : (root.capturedKeyLabel !== "" ? root.capturedKeyLabel : "Click to set key")
                    color: root.keyCapturing ? Theme.crust : Theme.text
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        root.keyCapturing = true;
                        keyCaptureItem.forceActiveFocus();
                    }
                }
            }

            Item {
                id: keyCaptureItem
                width: 0
                height: 0
                focus: false

                Keys.onPressed: (event) => {
                    if (!root.keyCapturing) {
                        return;
                    }
                    const code = root.keyScanCodes[event.key];
                    if (code !== undefined) {
                        root.capturedScanCode = code;
                        root.capturedKeyLabel = (event.text && event.text.trim().length > 0)
                            ? event.text.toUpperCase()
                            : ("Key 0x" + event.key.toString(16));
                    } else {
                        root.capturedScanCode = -1;
                        root.capturedKeyLabel = "Unsupported key";
                    }
                    root.keyCapturing = false;
                    event.accepted = true;
                }
            }
        }

        // --- Macro ---------------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            spacing: Theme.spacingSm
            visible: actionTypeCombo.currentText === "Macro"

            Text {
                text: "Quick timed press: presses Target Button, holds it for Duration, then releases it."
                color: Theme.overlay0
                font.pixelSize: 11
                font.italic: true
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: "Target vJoy Device"; color: Theme.subtext0; font.pixelSize: 11 }
                AppComboBox { id: macroDeviceCombo; Layout.fillWidth: true; model: root.vjoyDeviceNames }
            }

            RowLayout {
                Layout.fillWidth: true
                Text { text: "Target Button"; color: Theme.subtext0; font.pixelSize: 11; Layout.fillWidth: true }
                IntStepper { id: macroTargetButton; from: 1; to: 128 }
            }

            RowLayout {
                Layout.fillWidth: true
                Text { text: "Duration (ms)"; color: Theme.subtext0; font.pixelSize: 11; Layout.fillWidth: true }
                IntStepper { id: macroDuration; from: 20; to: 5000; stepSize: 20; value: 100 }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacingXs
                height: 1
                color: Qt.rgba(1, 1, 1, 0.1)
            }

            ToolButton {
                label: "Open Macro Editor…"
                Layout.fillWidth: true
                onClicked: macroEditorPopup.openFor(root.devicePath, root.inputName)
            }
            Text {
                text: "Record real key-down/key-up sequences with real timing."
                color: Theme.overlay0
                font.pixelSize: 11
                font.italic: true
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }

        // --- Axis-to-Button --------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            spacing: Theme.spacingSm
            visible: actionTypeCombo.currentText === "Axis-to-Button"

            Text {
                text: "Fires a vJoy button while this axis sits inside one or more configured zones (e.g. the last 10% of a throttle triggering the afterburner)."
                color: Theme.overlay0
                font.pixelSize: 11
                font.italic: true
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            ToolButton {
                label: "Configure Zones…"
                Layout.fillWidth: true
                onClicked: axisSplitterPopup.openFor(root.devicePath, root.inputName)
            }
        }

        // --- Tempo (Short/Long Press) ---------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            spacing: Theme.spacingSm
            visible: actionTypeCombo.currentText === "Tempo (Short/Long Press)"

            Text {
                text: "Release before the threshold fires Action A; holding past it fires Action B immediately."
                color: Theme.overlay0
                font.pixelSize: 11
                font.italic: true
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.fillWidth: true
                Text { text: "Threshold (ms)"; color: Theme.subtext0; font.pixelSize: 11; Layout.fillWidth: true }
                IntStepper { id: tempoThresholdMs; from: 100; to: 2000; stepSize: 50; value: 300 }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: "Short Press Pulse (ms)"; color: Theme.subtext0; font.pixelSize: 11 }
                IntStepper { id: tempoPulseMs; from: 10; to: 1000; stepSize: 10; value: 50 }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacingXs
                height: 1
                color: Qt.rgba(1, 1, 1, 0.1)
            }

            Text { text: "Action A (short press)"; color: Theme.subtext0; font.pixelSize: 11 }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingXs
                AppComboBox { id: tempoActionADeviceCombo; Layout.preferredWidth: 150; model: root.vjoyDeviceNames }
                Text { text: "Button"; color: Theme.subtext0; font.pixelSize: 11 }
                IntStepper { id: tempoActionAButton; from: 1; to: 128 }
            }

            Text { text: "Action B (long press)"; color: Theme.subtext0; font.pixelSize: 11 }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingXs
                AppComboBox { id: tempoActionBDeviceCombo; Layout.preferredWidth: 150; model: root.vjoyDeviceNames }
                Text { text: "Button"; color: Theme.subtext0; font.pixelSize: 11 }
                IntStepper { id: tempoActionBButton; from: 1; to: 128; value: 2 }
            }
        }

        // --- Sequence / Rotary -----------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            spacing: Theme.spacingSm
            visible: actionTypeCombo.currentText === "Sequence / Rotary"

            Text {
                text: "Cycles through a list of vJoy button remaps - each press fires the next one in order."
                color: Theme.overlay0
                font.pixelSize: 11
                font.italic: true
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            ToolButton {
                label: "Configure Sequence…"
                Layout.fillWidth: true
                onClicked: sequencePopup.openFor(root.devicePath, root.inputName, root.existingSequenceSteps)
            }
        }

        // --- Shift / Modifier --------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            spacing: Theme.spacingSm
            visible: actionTypeCombo.currentText === "Shift / Modifier"

            Text {
                text: "Switches to Target Mode while this button is held, and restores the previous mode on release."
                color: Theme.overlay0
                font.pixelSize: 11
                font.italic: true
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: "Target Mode"; color: Theme.subtext0; font.pixelSize: 11 }
                AppComboBox { id: shiftModeCombo; Layout.fillWidth: true; model: profileEditorViewModel.modes }
            }
        }

        // --- Mode Toggle (Sticky) ----------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            spacing: Theme.spacingSm
            visible: actionTypeCombo.currentText === "Mode Toggle (Sticky)"

            Text {
                text: "Switches to Target Mode permanently when pressed. (No hold required)."
                color: Theme.overlay0
                font.pixelSize: 11
                font.italic: true
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: "Target Mode"; color: Theme.subtext0; font.pixelSize: 11 }
                AppComboBox { id: toggleModeCombo; Layout.fillWidth: true; model: profileEditorViewModel.modes }
            }
        }

        // --- Condition / Modifier Check -----------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            spacing: Theme.spacingSm
            visible: actionTypeCombo.currentText === "Condition / Modifier Check"

            Text {
                text: "Only forwards this input to the action below while the chosen modifier button is held (or released, if set to \"Released\")."
                color: Theme.overlay0
                font.pixelSize: 11
                font.italic: true
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: "Modifier Device"; color: Theme.subtext0; font.pixelSize: 11 }
                AppComboBox { id: condDeviceCombo; Layout.fillWidth: true; model: profileEditorViewModel.deviceDisplayNames() }
            }

            RowLayout {
                Layout.fillWidth: true
                Text { text: "Modifier Button"; color: Theme.subtext0; font.pixelSize: 11; Layout.fillWidth: true }
                IntStepper { id: condButtonStepper; from: 0; to: 127 }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: "Condition"; color: Theme.subtext0; font.pixelSize: 11 }
                AppComboBox { id: condRequirePressed; Layout.fillWidth: true; model: ["Pressed", "Released"] }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacingXs
                height: 1
                color: Qt.rgba(1, 1, 1, 0.1)
            }

            Text { text: "Wrapped Action (vJoy Remap)"; color: Theme.subtext0; font.pixelSize: 11 }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: "Target vJoy Device"; color: Theme.subtext0; font.pixelSize: 11 }
                AppComboBox { id: condVjoyDeviceCombo; Layout.fillWidth: true; model: root.vjoyDeviceNames }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                visible: root.inputKind === "axis"
                Text { text: "Target Axis"; color: Theme.subtext0; font.pixelSize: 11 }
                AppComboBox { id: condVjoyAxisCombo; Layout.fillWidth: true; model: root.vjoyAxisNames }
            }

            RowLayout {
                Layout.fillWidth: true
                visible: root.inputKind === "button"
                Text { text: "Target Button"; color: Theme.subtext0; font.pixelSize: 11; Layout.fillWidth: true }
                IntStepper { id: condVjoyTargetButton; from: 1; to: 128 }
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

            // Fase SC-7.7: lets the user copy one input's whole binding
            // (curve points, deadzone, remap target, ...) and paste it onto
            // a different physical input's own index in a couple of clicks -
            // e.g. rescuing a "[Legacy]" ghost axis' imported curve straight
            // onto the real hardware axis it belongs on, instead of having
            // to re-enter it point-by-point.
            ToolButton {
                label: "Copy"
                visible: root.hasBinding
                onClicked: {
                    profileEditorViewModel.copyAction(root.devicePath, root.inputName, profileEditorViewModel.currentMode, root.inputKind);
                    root.close();
                }
            }
            ToolButton {
                label: "Paste"
                // Fase SC-7.10: bound to the hasCopiedAxis/hasCopiedButton
                // Q_PROPERTYs (NOTIFY clipboardChanged) instead of calling
                // the plain hasCopiedAction() invokable directly - QML only
                // re-evaluates an invokable call when one of its own
                // directly-referenced arguments changes, and root.inputKind
                // stays identical across opening one axis' popup after
                // another, so this binding was frozen at whatever it first
                // evaluated to and never noticed a later Copy.
                visible: root.inputKind === "axis" ? profileEditorViewModel.hasCopiedAxis : profileEditorViewModel.hasCopiedButton
                onClicked: {
                    profileEditorViewModel.pasteAction(root.devicePath, root.inputName, profileEditorViewModel.currentMode, root.inputKind);
                    root.close();
                }
            }
            ToolButton {
                label: "Unbind"
                visible: root.hasBinding
                onClicked: {
                    profileEditorViewModel.unbindAction(root.devicePath, root.inputName, profileEditorViewModel.currentMode);
                    root.close();
                }
            }
            ToolButton {
                label: "Cancel"
                onClicked: root.close()
            }
            ToolButton {
                label: "Apply"
                visible: actionTypeCombo.currentText !== "Axis-to-Button"
                    && actionTypeCombo.currentText !== "Sequence / Rotary"
                enabled: actionTypeCombo.currentText !== "Keyboard" || root.capturedScanCode >= 0
                opacity: enabled ? 1.0 : 0.5
                onClicked: {
                    let actionData;
                    if (actionTypeCombo.currentText === "vJoy Remap") {
                        const targetOutputId = vjoyDeviceCombo.currentIndex + 1;
                        if (root.inputKind === "axis") {
                            const curveAction = { actionType: "CurveHandler", targetOutputId: targetOutputId,
                                                   targetAxis: vjoyAxisCombo.currentIndex,
                                                   parameters: { invert: invertAxisCheck.checked } };
                            actionData = smoothingToggle.checked
                                ? { actionType: "SmoothingHandler",
                                    parameters: { smoothingFactor: smoothingFactorSlider.value },
                                    wrappedAction: curveAction }
                                : curveAction;
                        } else {
                            actionData = { actionType: "ButtonRemapHandler", targetOutputId: targetOutputId,
                                            targetButton: vjoyTargetButton.value - 1 };
                        }
                    } else if (actionTypeCombo.currentText === "Keyboard") {
                        actionData = { actionType: "KeyboardHandler",
                                        parameters: { scanCode: root.capturedScanCode } };
                    } else if (actionTypeCombo.currentText === "Macro") {
                        actionData = { actionType: "MacroHandler",
                                        targetOutputId: macroDeviceCombo.currentIndex + 1,
                                        targetButton: macroTargetButton.value - 1,
                                        parameters: { waitMs: macroDuration.value } };
                    } else if (actionTypeCombo.currentText === "Tempo (Short/Long Press)") {
                        actionData = {
                            actionType: "TempoHandler",
                            parameters: { longPressMs: tempoThresholdMs.value, pulseDurationMs: tempoPulseMs.value },
                            shortAction: { actionType: "ButtonRemapHandler",
                                           targetOutputId: tempoActionADeviceCombo.currentIndex + 1,
                                           targetButton: tempoActionAButton.value - 1 },
                            longAction: { actionType: "ButtonRemapHandler",
                                          targetOutputId: tempoActionBDeviceCombo.currentIndex + 1,
                                          targetButton: tempoActionBButton.value - 1 }
                        };
                    } else if (actionTypeCombo.currentText === "Condition / Modifier Check") {
                        const condTargetOutputId = condVjoyDeviceCombo.currentIndex + 1;
                        const condWrappedAction = root.inputKind === "axis"
                            ? { actionType: "CurveHandler", targetOutputId: condTargetOutputId,
                                targetAxis: condVjoyAxisCombo.currentIndex }
                            : { actionType: "ButtonRemapHandler", targetOutputId: condTargetOutputId,
                                targetButton: condVjoyTargetButton.value - 1 };
                        actionData = {
                            actionType: "ConditionHandler",
                            parameters: {
                                modSystemPath: profileEditorViewModel.systemPathForDevice(condDeviceCombo.currentIndex),
                                modButtonIndex: condButtonStepper.value,
                                requirePressed: condRequirePressed.currentIndex === 0
                            },
                            wrappedAction: condWrappedAction
                        };
                    } else if (actionTypeCombo.currentText === "vJoy Hat Remap") {
                        actionData = { actionType: "HatRemapHandler",
                                        targetOutputId: hatDeviceCombo.currentIndex + 1,
                                        targetHat: hatTargetCombo.currentIndex,
                                        targetDirection: hatDirectionCombo.currentIndex };
                    } else if (actionTypeCombo.currentText === "Shift / Modifier") {
                        actionData = { actionType: "TemporaryModeSwitch",
                                        parameters: { targetMode: shiftModeCombo.currentText } };
                    } else if (actionTypeCombo.currentText === "Split Axis") {
                        actionData = {
                            actionType: "SplitAxisHandler",
                            parameters: {
                                lowerTargetOutputId: splitLowerVjoyCombo.currentIndex + 1,
                                lowerTargetAxis: splitLowerAxisCombo.currentIndex,
                                lowerInvert: splitLowerInvertCheck.checked,
                                upperTargetOutputId: splitUpperVjoyCombo.currentIndex + 1,
                                upperTargetAxis: splitUpperAxisCombo.currentIndex,
                                upperInvert: splitUpperInvertCheck.checked,
                                splitMode: splitModeCombo.currentIndex
                            }
                        };
                    } else if (actionTypeCombo.currentText === "Merge Axis") {
                        actionData = {
                            actionType: "MergeAxisHandler",
                            targetOutputId: vjoyDeviceCombo.currentIndex + 1,
                            targetAxis: vjoyAxisCombo.currentIndex,
                            parameters: {
                                otherSystemPath: profileEditorViewModel.systemPathForDevice(mergeDeviceCombo.currentIndex),
                                otherAxisIndex: mergeAxisCombo.currentIndex,
                                isSubtraction: mergeModeCombo.currentIndex === 1
                            }
                        };
                    } else {
                        actionData = { actionType: "ModeSwitch",
                                        parameters: { targetMode: toggleModeCombo.currentText } };
                    }

                    const applied = profileEditorViewModel.bindAction(
                        root.devicePath, root.inputName, profileEditorViewModel.currentMode,
                        JSON.stringify(actionData));
                    if (applied) {
                        // Guardar en memoria para auto-incremento
                        if (actionTypeCombo.currentText === "vJoy Remap") {
                            root.lastVjoyDevice = vjoyDeviceCombo.currentIndex;
                            if (root.inputKind === "axis") {
                                root.lastVjoyAxis = (vjoyAxisCombo.currentIndex + 1) % vjoyAxisNames.length;
                            } else {
                                // vjoyTargetButton.value is now the display value (1-128);
                                // wrap to 0 once it passes the last button (128).
                                root.lastVjoyButton = vjoyTargetButton.value % 128;
                            }
                        } else if (actionTypeCombo.currentText === "vJoy Hat Remap") {
                            root.lastVjoyHat = hatTargetCombo.currentIndex;
                        }
                        root.close();
                    }
                }
            }
        }
    }

    MacroEditorPopup {
        id: macroEditorPopup
        onApplied: root.close()
    }

    AxisSplitterPopup {
        id: axisSplitterPopup
        onApplied: root.close()
    }

    SequencePopup {
        id: sequencePopup
        onApplied: root.close()
    }
}
