import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Effects
import QtQuick.Dialogs
import GremblingNexus

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
    Behavior on y { NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutQuad } }
    Behavior on height { NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutQuad } }
    // Fase SC-7.7: widened from 380 - the bottom button row can now show up
    // to 5 ToolButtons at once (Copy, Paste, Unbind, Cancel, Apply), which
    // didn't comfortably fit the popup's old width.
    width: 440
    padding: 0
    // Fase (bugfix, take 2): the popup's own height is deliberately left as
    // plain implicit sizing again - a previous attempt tried to cap IT
    // directly (height: Math.min(implicitHeight, ...)) and rely on
    // Layout.fillHeight to make the ScrollView below absorb the difference,
    // but that makes the ScrollView's own contribution to implicitHeight
    // circular/unreliable (a fillHeight item's "natural" size going into a
    // Popup's implicitHeight computation isn't guaranteed to reflect its
    // true unclipped content height for every Qt Quick Controls version).
    // Capping the ScrollView directly instead (see actionFieldsScroll's own
    // Layout.preferredHeight below) sidesteps that entirely: the popup's
    // implicitHeight is just the plain sum of fixed header + an
    // ALREADY-bounded ScrollView + fixed footer, so it can never itself
    // exceed maxActionsAreaHeight + a small constant - no separate
    // Popup-level cap needed, and no risk of the two caps disagreeing.
    readonly property int maxActionsAreaHeight: Overlay.overlay
        ? Math.max(220, Math.round(Overlay.overlay.height * 0.55))
        : 420
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

    /// Toggle / Latching Switch draft wrapped-action - a single object, not
    /// a list (ToggleHandler wraps exactly one action - see its own class
    /// docs), but the same "category picks which fields apply" shape
    /// SequencePopup.qml's own per-row objects use, restricted to the three
    /// categories that actually have real press/release semantics
    /// (ButtonRemapHandler/KeyboardHandler/MouseButton's click targets all
    /// forward evt.pressed straight through - see toggleActionToWire()'s own
    /// docs on why Macro/ModeSwitch/Audio/TTS/Delay/Scroll are deliberately
    /// NOT offered here).
    property var toggleWrappedAction: ({ category: "device", targetDeviceType: "vjoy", targetOutputId: 1,
        targetButton: 0, scanCode: 0, keyLabel: "", mouseAction: "Left" })

    /// Toggle's inverse pair to SequencePopup.qml's own stepFromWireAction()/
    /// stepToWireAction() - same idea, restricted to this popup's own three
    /// supported categories.
    function toggleActionFromWire(action) {
        const type = action.actionType;
        const params = action.parameters || {};
        const base = { category: "device", targetDeviceType: "vjoy", targetOutputId: 1, targetButton: 0,
            scanCode: 0, keyLabel: "", mouseAction: "Left" };
        if (type === "KeyboardHandler") {
            const scanCode = params.scanCode || 0;
            return Object.assign({}, base, { category: "keyboard", scanCode: scanCode,
                keyLabel: "Key 0x" + scanCode.toString(16) });
        }
        if (type === "MouseButton") {
            return Object.assign({}, base, { category: "mouse", mouseAction: params.targetAction || "Left" });
        }
        // ButtonRemapHandler, or anything this popup doesn't offer a category
        // for (e.g. a hand-authored profile wrapping something else) - falls
        // back to "device" with whatever target* fields are actually present,
        // the same tolerant policy SequencePopup's own restore path uses.
        return Object.assign({}, base, { category: "device",
            targetDeviceType: action.targetDeviceType || "vjoy",
            targetOutputId: action.targetOutputId || 1,
            targetButton: action.targetButton || 0 });
    }

    /// Only ButtonRemapHandler/KeyboardHandler/a mouse CLICK genuinely have
    /// press/release semantics that a toggle's alternating held/released
    /// state maps onto correctly - every other handler kind in this
    /// codebase's own processButton() only acts "if (evt.pressed)" and
    /// silently no-ops on the release-shaped call ToggleHandler forwards on
    /// every SECOND physical press (MacroHandler, ModeSwitchHandler,
    /// DelayAction, AudioAction, TTSAction, and MouseButtonHandler's own
    /// ScrollUp/ScrollDown branch all follow this same "fires once, ignores
    /// the pair's other half" shape - see their own processButton() bodies).
    /// Wrapping any of those in a Toggle would silently do nothing on every
    /// other press, so this popup deliberately does not offer them as a
    /// category here at all (same reasoning SequencePopup.qml's own file
    /// comment gives for leaving "vJoy Axis" out of ITS category list).
    function toggleActionToWire(a) {
        const category = a.category || "device";
        if (category === "keyboard") {
            return { actionType: "KeyboardHandler", parameters: { scanCode: a.scanCode || 0 } };
        }
        if (category === "mouse") {
            return { actionType: "MouseButton", parameters: { targetAction: a.mouseAction || "Left" } };
        }
        return { actionType: "ButtonRemapHandler", targetDeviceType: a.targetDeviceType || "vjoy",
            targetOutputId: a.targetOutputId || 1, targetButton: a.targetButton || 0 };
    }

    /// Tempo (Short/Long Press) draft action lists - plain JS objects
    /// {targetDeviceType, targetOutputId, targetButton}, one per cascaded
    /// ButtonRemapHandler action. Mirrors SequencePopup's own "steps" list
    /// pattern (reassign the whole array on every edit, never mutate
    /// in-place, so the Repeater below reliably picks up the change).
    property var tempoShortActions: []
    property var tempoLongActions: []

    // Memoria para auto-incremento (Fase UX): recuerda la última asignación
    // de vJoy para que el usuario no tenga que cambiar manualmente "Botón 1"
    // a "Botón 2", "Botón 3", etc. al mapear múltiples inputs de corrido.
    property var lastVjoyTarget: ({targetDeviceType: "vjoy", targetOutputId: 1})
    property int lastVjoyButton: 0
    property int lastVjoyAxis: 0
    property int lastVjoyHat: 0

    readonly property var vjoyDeviceNames: Array.from({length: 16}, (_, i) => "vJoy Device " + (i + 1))
    readonly property var vjoyAxisNames: ["X", "Y", "Z", "Rx", "Ry", "Rz", "Slider", "Dial"]

    // Fase (UX refactor): the old single flat 20-entry vJoy+Xbox ComboBox
    // (outputDeviceNames/isXboxTarget()/resolveOutputTarget()/
    // outputTargetIndex()) forced scrolling past all 16 vJoy entries to
    // reach an Xbox slot - every target-device combo below is now an
    // OutputDeviceCombo.qml instance instead (two steps: type, then id),
    // read via its own .targetDeviceType/.targetOutputId/.isXbox properties
    // and restored via its own setFromTarget().
    readonly property var xboxAxisNames: ["Left Stick X", "Left Stick Y", "Right Stick X", "Right Stick Y",
        "Left Trigger", "Right Trigger"]
    readonly property var xboxButtonNames: [
        "D-Pad Up (0)", "D-Pad Down (1)", "D-Pad Left (2)", "D-Pad Right (3)",
        "Start (4)", "Back (5)", "Left Thumb (6)", "Right Thumb (7)",
        "LB (8)", "RB (9)", "Guide (10)", "A (11)", "B (12)", "X (13)", "Y (14)"
    ]

    // Action types offered depend on inputKind - see the file-level comment
    // above for why Keyboard/Macro only make sense for a button input.
    readonly property var actionTypesForAxis: ["vJoy Remap", "Axis-to-Button", "Split Axis", "Merge Axis",
        "Condition / Modifier Check", "Mouse X Axis", "Mouse Y Axis"]
    readonly property var actionTypesForButton: ["vJoy Remap", "vJoy Hat Remap", "Keyboard", "Macro",
        "Tempo (Short/Long Press)", "Sequence / Rotary", "Toggle / Latching Switch", "Shift / Modifier",
        "Mode Toggle (Sticky)", "Condition / Modifier Check", "Delay (Pause)", "Play Audio", "Text-to-Speech",
        "Mouse Left Click", "Mouse Right Click", "Mouse Middle Click", "Mouse Scroll Up", "Mouse Scroll Down"]
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

        // QoL: lets the mouse wheel step the value up/down without having to
        // click the +/- buttons repeatedly. accepted = true stops the event
        // from bubbling to any ScrollView/Flickable this stepper happens to
        // sit inside, so hovering a number and scrolling only changes that
        // number instead of also scrolling the popup underneath it.
        WheelHandler {
            onWheel: (event) => {
                if (event.angleDelta.y > 0) {
                    stepper.value = Math.min(stepper.to, stepper.value + stepper.stepSize);
                } else if (event.angleDelta.y < 0) {
                    stepper.value = Math.max(stepper.from, stepper.value - stepper.stepSize);
                }
                event.accepted = true;
            }
        }

        ToolButton {
            label: qsTr("−")
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
            label: qsTr("+")
            Layout.preferredWidth: 28
            onClicked: stepper.value = Math.min(stepper.to, stepper.value + stepper.stepSize)
        }
    }

    /// Extracts a TempoHandler gesture's cascaded action list back out of
    /// actionData for restoring this popup's own draft state - prefers the
    /// current "shortActions"/"longActions" array schema (arrayKey), falling
    /// back to the pre-cascade single-object "shortAction"/"longAction"
    /// (legacyKey) for profiles saved before TempoHandler supported multiple
    /// actions per gesture. Each entry becomes one row's draft object -
    /// "category" ("device"/"delay"/"audio"/"tts") picks which field-group
    /// the Repeater delegate shows, and every row always carries all four
    /// kinds' fields (unused ones just sit at their defaults) so switching
    /// category in the UI never has to invent missing keys. Unrecognized
    /// actionTypes are silently skipped - this popup's Tempo tab only ever
    /// authors these four kinds, so there is nothing to restore them into.
    function extractTempoActions(actionData, arrayKey, legacyKey) {
        const rawList = Array.isArray(actionData[arrayKey]) ? actionData[arrayKey]
            : (actionData[legacyKey] ? [actionData[legacyKey]] : []);
        return rawList.map((action) => {
            if (!action) return null;
            const params = action.parameters || {};
            if (action.actionType === "ButtonRemapHandler") {
                return { category: "device", targetDeviceType: action.targetDeviceType || "vjoy",
                    targetOutputId: action.targetOutputId || 1, targetButton: action.targetButton || 0,
                    delayMs: 100, filePath: "", text: "", macroSteps: [], targetMode: "" };
            } else if (action.actionType === "DelayAction") {
                return { category: "delay", targetDeviceType: "vjoy", targetOutputId: 1, targetButton: 0,
                    delayMs: params.delayMs !== undefined ? params.delayMs : 100, filePath: "", text: "", macroSteps: [], targetMode: "" };
            } else if (action.actionType === "AudioAction") {
                return { category: "audio", targetDeviceType: "vjoy", targetOutputId: 1, targetButton: 0,
                    delayMs: 100, filePath: params.filePath || "", text: "", macroSteps: [], targetMode: "" };
            } else if (action.actionType === "TTSAction") {
                return { category: "tts", targetDeviceType: "vjoy", targetOutputId: 1, targetButton: 0,
                    delayMs: 100, filePath: "", text: params.text || "", macroSteps: [], targetMode: "" };
            } else if (action.actionType === "MacroHandler") {
                // Sprint 6: a macro recorded via MacroEditorPopup.openEmbedded()
                // and nested inside this Tempo gesture - params.steps is
                // already the exact {type, scanCode}/{type:"Wait", waitMs}
                // shape MacroHandler::toJson() writes, so it round-trips
                // straight back into the row as-is; toCascadeAction() below
                // wraps it back into the same actionType JSON on Apply.
                return { category: "macro", targetDeviceType: "vjoy", targetOutputId: 1, targetButton: 0,
                    delayMs: 100, filePath: "", text: "", macroSteps: params.steps || [], targetMode: "" };
            } else if (action.actionType === "ModeSwitch") {
                // Mode Switch inside a Tempo cascade: "ModeSwitch" is the
                // same actionType/JSON shape the top-level "Mode Toggle
                // (Sticky)" action type already builds (see toggleModeCombo
                // below and this popup's own Apply handler) - the C++ side
                // needed no changes at all for this to nest inside a Tempo
                // cascade: TempoHandler's own shortHandlers/longHandlers are
                // already a plain std::vector<std::shared_ptr<IActionHandler>>,
                // and ProfileManager::instantiateActionList() already
                // dispatches each cascade entry through the fully generic
                // instantiateHandler() (which already knows "ModeSwitch") -
                // same polymorphism that already let a Macro nest inside a
                // Tempo cascade since Sprint 6.
                return { category: "modeSwitch", targetDeviceType: "vjoy", targetOutputId: 1, targetButton: 0,
                    delayMs: 100, filePath: "", text: "", macroSteps: [], targetMode: params.targetMode || "" };
            }
            return null;
        }).filter((row) => row !== null);
    }

    /// True if every row in list has whatever its own category requires
    /// (Audio needs a non-empty file path, TTS needs non-empty text; Device
    /// Output/Delay have no further requirement beyond existing at all).
    function tempoActionsValid(list) {
        return list.every((a) => {
            const category = a.category || "device";
            if (category === "audio") return (a.filePath || "").trim().length > 0;
            if (category === "tts") return (a.text || "").trim().length > 0;
            if (category === "macro") return (a.macroSteps || []).length > 0;
            if (category === "modeSwitch") return (a.targetMode || "").trim().length > 0;
            return true;
        });
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
        vjoyDeviceCombo.setFromTarget(root.lastVjoyTarget);
        vjoyAxisCombo.currentIndex = root.lastVjoyAxis;
        xboxAxisCombo.currentIndex = 0;
        xboxButtonCombo.currentIndex = 0;
        if (vjoyDeviceCombo.isXbox) {
            root.lastVjoyButton = 0;
        } else {
            root.lastVjoyButton = profileEditorViewModel.nextAvailableVjoyButton(vjoyDeviceCombo.targetOutputId);
        }
        vjoyTargetButton.value = root.lastVjoyButton + 1;
        macroDeviceCombo.setFromTarget(null);
        macroTargetButton.value = 1;
        macroDuration.value = 100;
        root.capturedScanCode = -1;
        root.capturedKeyLabel = "";
        keyCapturing = false;
        root.existingSequenceSteps = null;
        root.toggleWrappedAction = { category: "device", targetDeviceType: "vjoy", targetOutputId: 1,
            targetButton: 0, scanCode: 0, keyLabel: "", mouseAction: "Left" };
        toggleDeviceCombo.setFromTarget(root.toggleWrappedAction);
        tempoThresholdMs.value = 300;
        tempoPulseMs.value = 50;
        root.tempoShortActions = [{category: "device", targetDeviceType: "vjoy", targetOutputId: 1, targetButton: 0,
            delayMs: 100, filePath: "", text: "", macroSteps: [], targetMode: ""}];
        root.tempoLongActions = [{category: "device", targetDeviceType: "vjoy", targetOutputId: 1, targetButton: 1,
            delayMs: 100, filePath: "", text: "", macroSteps: [], targetMode: ""}];
        delayMsStepper.value = 100;
        audioFilePathField.text = "";
        ttsTextField.text = "";
        shiftModeCombo.currentIndex = 0;
        condDeviceCombo.currentIndex = 0;
        condButtonStepper.value = 0;
        condRequirePressed.currentIndex = 0;
        condVjoyDeviceCombo.setFromTarget(null);
        condVjoyAxisCombo.currentIndex = 0;
        condVjoyTargetButton.value = 1;
        hatDeviceCombo.setFromTarget(null);
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
        actionNoteField.text = "";

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

                // Sprint QoL Part 2: "Nota / Descripción" is generic - it
                // lives in whatever the OUTERMOST action's own
                // "parameters.note" is (see the Apply handler's own
                // post-processing step below), regardless of which
                // actionType branch below actually restores the rest of
                // this popup's fields, so it's read once here up front
                // instead of duplicated into every branch.
                actionNoteField.text = (actionData.parameters && actionData.parameters.note) || "";

                if (actionType === "CurveHandler" || actionType === "SmoothingHandler") {
                    actionTypeCombo.currentIndex = Math.max(0, root.actionTypesForAxis.indexOf("vJoy Remap"));
                    const wrapped = actionType === "SmoothingHandler" ? (actionData.wrappedAction || {}) : actionData;
                    vjoyDeviceCombo.setFromTarget(wrapped);
                    vjoyAxisCombo.currentIndex = wrapped.targetAxis || 0;
                    xboxAxisCombo.currentIndex = wrapped.targetAxis || 0;
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
                    vjoyDeviceCombo.setFromTarget(actionData);
                    vjoyTargetButton.value = (actionData.targetButton || 0) + 1;
                    xboxButtonCombo.currentIndex = actionData.targetButton || 0;
                    restoredExisting = true;
                } else if (actionType === "HatRemapHandler") {
                    actionTypeCombo.currentIndex = Math.max(0, root.actionTypesForButton.indexOf("vJoy Hat Remap"));
                    hatDeviceCombo.setFromTarget(actionData);
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
                    macroDeviceCombo.setFromTarget(actionData);
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
                    root.tempoShortActions = extractTempoActions(actionData, "shortActions", "shortAction");
                    root.tempoLongActions = extractTempoActions(actionData, "longActions", "longAction");
                    restoredExisting = true;
                } else if (actionType === "SequenceHandler") {
                    actionTypeCombo.currentIndex = Math.max(0, root.actionTypesForButton.indexOf("Sequence / Rotary"));
                    if (actionData.parameters && actionData.parameters.actions) {
                        const parsedSteps = [];
                        for (let i = 0; i < actionData.parameters.actions.length; i++) {
                            const act = actionData.parameters.actions[i];
                            parsedSteps.push({
                                targetDeviceType: (act.targetDeviceType || "vjoy"),
                                targetOutputId: (act.targetOutputId || 1),
                                targetButton: (act.targetButton || 0)
                            });
                        }
                        root.existingSequenceSteps = parsedSteps;
                    }
                    restoredExisting = true;
                } else if (actionType === "ToggleHandler") {
                    actionTypeCombo.currentIndex =
                        Math.max(0, root.actionTypesForButton.indexOf("Toggle / Latching Switch"));
                    root.toggleWrappedAction = root.toggleActionFromWire(actionData.wrappedAction || {});
                    toggleDeviceCombo.setFromTarget(root.toggleWrappedAction);
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
                    condVjoyDeviceCombo.setFromTarget(wrapped);
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
                    vjoyDeviceCombo.setFromTarget(actionData);
                    vjoyAxisCombo.currentIndex = actionData.targetAxis || 0;
                    xboxAxisCombo.currentIndex = actionData.targetAxis || 0;
                    const p = actionData.parameters || {};
                    const r = profileEditorViewModel.deviceRowForSystemPath(p.otherSystemPath || "");
                    if (r >= 0) {
                        mergeDeviceCombo.currentIndex = r;
                        mergeAxisCombo.currentIndex = p.otherAxisIndex || 0;
                    }
                    mergeModeCombo.currentIndex = p.isSubtraction ? 1 : 0;
                    restoredExisting = true;
                } else if (actionType === "DelayAction") {
                    actionTypeCombo.currentIndex = Math.max(0, root.actionTypesForButton.indexOf("Delay (Pause)"));
                    if (actionData.parameters && actionData.parameters.delayMs !== undefined) {
                        delayMsStepper.value = actionData.parameters.delayMs;
                    }
                    restoredExisting = true;
                } else if (actionType === "AudioAction") {
                    actionTypeCombo.currentIndex = Math.max(0, root.actionTypesForButton.indexOf("Play Audio"));
                    if (actionData.parameters && actionData.parameters.filePath !== undefined) {
                        audioFilePathField.text = actionData.parameters.filePath;
                    }
                    restoredExisting = true;
                } else if (actionType === "TTSAction") {
                    actionTypeCombo.currentIndex = Math.max(0, root.actionTypesForButton.indexOf("Text-to-Speech"));
                    if (actionData.parameters && actionData.parameters.text !== undefined) {
                        ttsTextField.text = actionData.parameters.text;
                    }
                    restoredExisting = true;
                } else if (actionType === "MouseRelativeAxis") {
                    const p = actionData.parameters || {};
                    actionTypeCombo.currentIndex = Math.max(0, root.actionTypesForAxis.indexOf(
                        p.targetMouseAxis === "Y" ? "Mouse Y Axis" : "Mouse X Axis"));
                    if (p.sensitivity !== undefined) {
                        mouseSensitivitySlider.value = p.sensitivity;
                    }
                    if (p.deadzone !== undefined) {
                        mouseDeadzoneSlider.value = p.deadzone;
                    }
                    restoredExisting = true;
                } else if (actionType === "MouseButton") {
                    const mouseButtonLabels = {
                        "Left": "Mouse Left Click",
                        "Right": "Mouse Right Click",
                        "Middle": "Mouse Middle Click",
                        "ScrollUp": "Mouse Scroll Up",
                        "ScrollDown": "Mouse Scroll Down"
                    };
                    const targetAction = (actionData.parameters && actionData.parameters.targetAction) || "Left";
                    actionTypeCombo.currentIndex = Math.max(0, root.actionTypesForButton.indexOf(
                        mouseButtonLabels[targetAction] || "Mouse Left Click"));
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

            Text { text: qsTr("Bind Action"); color: Theme.text; font.pixelSize: 17; font.weight: Font.DemiBold }
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

            Text { text: qsTr("Action Type"); color: Theme.subtext0; font.pixelSize: 11 }
            AppComboBox {
                id: actionTypeCombo
                Layout.fillWidth: true
                model: root.inputKind === "axis" ? root.actionTypesForAxis : root.actionTypesForButton
            }
        }

        // Sprint QoL Part 2: generic free-text note, independent of
        // actionType - injected into whatever actionData ends up being
        // built (parameters.note) by the Apply handler below, and restored
        // the same way regardless of which branch actually built it (see
        // openFor()'s own restore block above). Deliberately outside
        // actionFieldsScroll below (not one more type-specific section) so
        // it stays visible no matter which Action Type is selected.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            spacing: 2

            TextField {
                id: actionNoteField
                Layout.fillWidth: true
                implicitHeight: 30
                color: Theme.subtext0
                font.pixelSize: 12
                font.italic: true
                placeholderText: qsTr("Note / Description (Optional)")
                background: Rectangle {
                    color: Theme.surface1
                    radius: Theme.radiusSmall
                    border.width: 1
                    border.color: Qt.rgba(1, 1, 1, 0.06)
                }
            }
        }

        // Fase (bugfix): everything type-specific below (vJoy Remap through
        // Condition/Modifier Check, including Tempo's own Short/Long Press
        // cascade lists) now scrolls independently of the header above and
        // the Copy/Paste/Unbind/Cancel/Apply row below - both of which stay
        // fixed. Layout.preferredHeight (not fillHeight - see root's
        // maxActionsAreaHeight comment above) directly caps this
        // ScrollView's own height against its inner content's real
        // implicitHeight, so it only ever shrinks (and its scrollbar only
        // ever appears) once actionFieldsContent genuinely can't fit -
        // small content still keeps the whole popup compact, exactly as
        // before this cascade UI existed.
        ScrollView {
            id: actionFieldsScroll
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(actionFieldsContent.implicitHeight, root.maxActionsAreaHeight)
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            contentWidth: availableWidth

            ColumnLayout {
                id: actionFieldsContent
                width: actionFieldsScroll.availableWidth
                spacing: Theme.spacingMd

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
                Text { text: qsTr("Target Output Device"); color: Theme.subtext0; font.pixelSize: 11 }
                OutputDeviceCombo { id: vjoyDeviceCombo }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                visible: root.inputKind === "axis" && !vjoyDeviceCombo.isXbox
                Text { text: qsTr("Target Axis"); color: Theme.subtext0; font.pixelSize: 11 }
                AppComboBox { id: vjoyAxisCombo; Layout.fillWidth: true; model: root.vjoyAxisNames }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                visible: root.inputKind === "axis" && vjoyDeviceCombo.isXbox
                Text { text: qsTr("Target Axis (Xbox 360)"); color: Theme.subtext0; font.pixelSize: 11 }
                AppComboBox { id: xboxAxisCombo; Layout.fillWidth: true; model: root.xboxAxisNames }
            }

            RowLayout {
                Layout.fillWidth: true
                visible: root.inputKind === "button" && !vjoyDeviceCombo.isXbox
                Text { text: qsTr("Target Button"); color: Theme.subtext0; font.pixelSize: 11; Layout.fillWidth: true }
                IntStepper { id: vjoyTargetButton; from: 1; to: 128 }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                visible: root.inputKind === "button" && vjoyDeviceCombo.isXbox
                Text { text: qsTr("Target Button (Xbox 360)"); color: Theme.subtext0; font.pixelSize: 11 }
                AppComboBox { id: xboxButtonCombo; Layout.fillWidth: true; model: root.xboxButtonNames }
            }

            RowLayout {
                Layout.fillWidth: true
                visible: root.inputKind === "axis" && actionTypeCombo.currentText === "vJoy Remap"
                Text {
                    text: qsTr("Smoothing / Anti-Jitter")
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
                label: qsTr("Smoothing Strength")
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
                    text: qsTr("Invert Axis")
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
                    text: qsTr("Combines this axis with another physical axis into the vJoy output above - e.g. two brake pedals merged into one rudder axis. Bind both physical axes to Merge Axis, each pointing at the other as its counterpart.")
                    color: Theme.overlay0
                    font.pixelSize: 11
                    font.italic: true
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text { text: qsTr("Other Physical Device"); color: Theme.subtext0; font.pixelSize: 11 }
                    AppComboBox { id: mergeDeviceCombo; Layout.fillWidth: true; model: profileEditorViewModel.deviceDisplayNames() }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text { text: qsTr("Other Physical Axis"); color: Theme.subtext0; font.pixelSize: 11 }
                    AppComboBox { id: mergeAxisCombo; Layout.fillWidth: true; model: profileEditorViewModel.axisNamesForDevice(mergeDeviceCombo.currentIndex) }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text { text: qsTr("Combine Mode"); color: Theme.subtext0; font.pixelSize: 11 }
                    AppComboBox { id: mergeModeCombo; Layout.fillWidth: true; model: [qsTr("Additive (average)"), qsTr("Differential (centered)")] }
                }
            }
        }

        // --- Mouse X/Y Axis (relative cursor movement) ------------------
        // Drives the OS cursor directly through the shared MouseWorkerThread
        // (see MouseRelativeAxisHandler's own docs) - no vJoy output at all,
        // so unlike vJoy Remap above this has no OutputDeviceCombo/target
        // axis picker of its own.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            spacing: Theme.spacingSm
            visible: actionTypeCombo.currentText === "Mouse X Axis" || actionTypeCombo.currentText === "Mouse Y Axis"

            Text {
                text: qsTr("Drives the OS mouse cursor directly, relative to its current position - no vJoy output involved. Bind the other screen axis to a second physical axis for full 2D cursor control.")
                color: Theme.overlay0
                font.pixelSize: 11
                font.italic: true
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            ValueSlider {
                id: mouseSensitivitySlider
                Layout.fillWidth: true
                label: qsTr("Sensitivity")
                from: 1.0
                to: 100.0
                value: 20.0
                onMoved: (v) => mouseSensitivitySlider.value = v
            }

            ValueSlider {
                id: mouseDeadzoneSlider
                Layout.fillWidth: true
                label: qsTr("Deadzone")
                from: 0.0
                to: 0.9
                value: 0.1
                onMoved: (v) => mouseDeadzoneSlider.value = v
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
                text: qsTr("Splits this axis' travel in half: the lower 50% drives one vJoy axis (spanning its full range), the upper 50% drives a second vJoy axis (also spanning its full range).")
                color: Theme.overlay0
                font.pixelSize: 11
                font.italic: true
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: qsTr("Split Mode"); color: Theme.subtext0; font.pixelSize: 11 }
                AppComboBox {
                    id: splitModeCombo
                    Layout.fillWidth: true
                    model: [qsTr("Center-to-Edges (Joystick/Rudder)"), qsTr("Sequential (Throttle/Slider)")]
                }
            }

            Text { text: qsTr("Lower Half (0% - 50%)"); color: Theme.subtext0; font.pixelSize: 11; font.weight: Font.DemiBold }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: qsTr("Target vJoy Device"); color: Theme.subtext0; font.pixelSize: 11 }
                AppComboBox { id: splitLowerVjoyCombo; Layout.fillWidth: true; model: root.vjoyDeviceNames }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: qsTr("Target Axis"); color: Theme.subtext0; font.pixelSize: 11 }
                AppComboBox { id: splitLowerAxisCombo; Layout.fillWidth: true; model: root.vjoyAxisNames }
            }
            RowLayout {
                Layout.fillWidth: true
                Text { text: qsTr("Invert"); color: Theme.subtext0; font.pixelSize: 11; Layout.fillWidth: true }
                ToggleSwitch { id: splitLowerInvertCheck }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacingXs
                height: 1
                color: Qt.rgba(1, 1, 1, 0.1)
            }

            Text { text: qsTr("Upper Half (50% - 100%)"); color: Theme.subtext0; font.pixelSize: 11; font.weight: Font.DemiBold }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: qsTr("Target vJoy Device"); color: Theme.subtext0; font.pixelSize: 11 }
                AppComboBox { id: splitUpperVjoyCombo; Layout.fillWidth: true; model: root.vjoyDeviceNames }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: qsTr("Target Axis"); color: Theme.subtext0; font.pixelSize: 11 }
                AppComboBox { id: splitUpperAxisCombo; Layout.fillWidth: true; model: root.vjoyAxisNames }
            }
            RowLayout {
                Layout.fillWidth: true
                Text { text: qsTr("Invert"); color: Theme.subtext0; font.pixelSize: 11; Layout.fillWidth: true }
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
                text: qsTr("Fires a discrete direction on a vJoy POV hat while this button is held - typically bound to one of a physical hat's own 4 synthetic Up/Right/Down/Left buttons.")
                color: Theme.overlay0
                font.pixelSize: 11
                font.italic: true
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: qsTr("Target Output Device"); color: Theme.subtext0; font.pixelSize: 11 }
                OutputDeviceCombo { id: hatDeviceCombo }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: qsTr("Target Hat"); color: Theme.subtext0; font.pixelSize: 11 }
                AppComboBox { id: hatTargetCombo; Layout.fillWidth: true; model: ["POV 1", "POV 2", "POV 3", "POV 4"] }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: qsTr("Target Direction"); color: Theme.subtext0; font.pixelSize: 11 }
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

            Text { text: qsTr("Key"); color: Theme.subtext0; font.pixelSize: 11 }

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
                        ? qsTr("Press a key…")
                        : (root.capturedKeyLabel !== "" ? root.capturedKeyLabel : qsTr("Click to set key"))
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
                text: qsTr("Quick timed press: presses Target Button, holds it for Duration, then releases it.")
                color: Theme.overlay0
                font.pixelSize: 11
                font.italic: true
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: qsTr("Target Output Device"); color: Theme.subtext0; font.pixelSize: 11 }
                OutputDeviceCombo { id: macroDeviceCombo }
            }

            RowLayout {
                Layout.fillWidth: true
                Text { text: qsTr("Target Button"); color: Theme.subtext0; font.pixelSize: 11; Layout.fillWidth: true }
                IntStepper { id: macroTargetButton; from: 1; to: 128 }
            }

            RowLayout {
                Layout.fillWidth: true
                Text { text: qsTr("Duration (ms)"); color: Theme.subtext0; font.pixelSize: 11; Layout.fillWidth: true }
                IntStepper { id: macroDuration; from: 20; to: 5000; stepSize: 20; value: 100 }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacingXs
                height: 1
                color: Qt.rgba(1, 1, 1, 0.1)
            }

            ToolButton {
                label: qsTr("Open Macro Editor…")
                Layout.fillWidth: true
                onClicked: macroEditorPopup.openFor(root.devicePath, root.inputName)
            }
            Text {
                text: qsTr("Record real key-down/key-up sequences with real timing.")
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
                text: qsTr("Fires a vJoy button while this axis sits inside one or more configured zones (e.g. the last 10% of a throttle triggering the afterburner).")
                color: Theme.overlay0
                font.pixelSize: 11
                font.italic: true
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            ToolButton {
                label: qsTr("Configure Zones…")
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
                text: qsTr("Release before the threshold fires every Short Press action in cascade; holding past it fires every Long Press action instead.")
                color: Theme.overlay0
                font.pixelSize: 11
                font.italic: true
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.fillWidth: true
                Text { text: qsTr("Threshold (ms)"); color: Theme.subtext0; font.pixelSize: 11; Layout.fillWidth: true }
                IntStepper { id: tempoThresholdMs; from: 100; to: 2000; stepSize: 50; value: 300 }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: qsTr("Short Press Pulse (ms)"); color: Theme.subtext0; font.pixelSize: 11 }
                IntStepper { id: tempoPulseMs; from: 10; to: 1000; stepSize: 10; value: 50 }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacingXs
                height: 1
                color: Qt.rgba(1, 1, 1, 0.1)
            }

            // --- Short Press: cascaded action list --------------------------
            // Fase (UX): each row is a modular block - a "Category" combo
            // (Device Output / Delay / Audio / Text-to-Speech) picks which
            // one field-group below is visible, so a single cascade can
            // freely mix a vJoy remap, a pause, a sound, and a spoken line.
            // Every row's JS object carries all four kinds' fields at once
            // (unused ones just sit unread) so switching category never
            // loses data already entered in the others, and so this stays a
            // stable object shape across a Repeater model reassignment.
            Text { text: qsTr("Short Press Actions"); color: Theme.subtext0; font.pixelSize: 11; font.weight: Font.DemiBold }
            Repeater {
                model: root.tempoShortActions
                delegate: ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingXs

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        Layout.bottomMargin: 2
                        visible: index > 0
                        color: Qt.rgba(1, 1, 1, 0.06)
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingXs

                        Text { text: (index + 1) + "."; color: Theme.subtext0; font.pixelSize: 11 }

                        AppComboBox {
                            id: tempoShortCategoryCombo
                            Layout.preferredWidth: 140
                            model: [qsTr("Device Output"), qsTr("Delay"), qsTr("Audio"), qsTr("Text-to-Speech"), qsTr("Macro"), qsTr("Mode Switch")]
                            currentIndex: ["device", "delay", "audio", "tts", "macro", "modeSwitch"].indexOf(modelData.category || "device")
                            // Fase (bugfix): ComboBox's own activated(int index) signal
                            // parameter shadows this delegate's Repeater-provided "index"
                            // context property when left as a bare `onActivated: {...}`
                            // block - both are named "index", and the signal's own
                            // argument wins inside this scope, so "arr[index]" was
                            // silently writing the new category into whichever row
                            // number happened to match the chosen category's position
                            // in this combo's own model (e.g. picking "Delay", position
                            // 1, wrote to row 1) instead of the row actually being
                            // edited. Naming the signal argument explicitly leaves the
                            // delegate's own "index" reachable below untouched.
                            onActivated: (categoryIndex) => {
                                const categories = ["device", "delay", "audio", "tts", "macro", "modeSwitch"];
                                const arr = root.tempoShortActions.slice();
                                arr[index] = Object.assign({}, arr[index], {category: categories[categoryIndex]});
                                root.tempoShortActions = arr;
                            }
                        }

                        Item { Layout.fillWidth: true }

                        ToolButton {
                            label: qsTr("×")
                            Layout.preferredWidth: 24
                            onClicked: {
                                const arr = root.tempoShortActions.slice();
                                arr.splice(index, 1);
                                root.tempoShortActions = arr;
                            }
                        }
                    }

                    // --- Device Output fields ---
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.spacingLg
                        spacing: Theme.spacingXs
                        visible: (modelData.category || "device") === "device"

                        OutputDeviceCombo {
                            id: tempoShortDeviceCombo
                            Component.onCompleted: tempoShortDeviceCombo.setFromTarget(modelData)
                            onChanged: {
                                const arr = root.tempoShortActions.slice();
                                arr[index] = Object.assign({}, arr[index], {
                                    targetDeviceType: tempoShortDeviceCombo.targetDeviceType,
                                    targetOutputId: tempoShortDeviceCombo.targetOutputId
                                });
                                root.tempoShortActions = arr;
                            }
                        }

                        Text { text: qsTr("Button"); color: Theme.subtext0; font.pixelSize: 11 }
                        TextField {
                            text: (modelData.targetButton || 0) + 1
                            Layout.preferredWidth: 40
                            implicitHeight: 28
                            color: Theme.text
                            font.pixelSize: 12
                            validator: IntValidator { bottom: 1; top: 128 }
                            background: Rectangle {
                                color: Theme.surface1; radius: Theme.radiusSmall
                                border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.08)
                            }
                            onEditingFinished: {
                                const arr = root.tempoShortActions.slice();
                                arr[index] = Object.assign({}, arr[index], {targetButton: (parseInt(text) || 1) - 1});
                                root.tempoShortActions = arr;
                            }
                        }
                    }

                    // --- Delay fields ---
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.spacingLg
                        spacing: Theme.spacingXs
                        visible: (modelData.category || "device") === "delay"

                        Text { text: qsTr("Milliseconds"); color: Theme.subtext0; font.pixelSize: 11 }
                        TextField {
                            text: modelData.delayMs !== undefined ? modelData.delayMs : 100
                            Layout.preferredWidth: 60
                            implicitHeight: 28
                            color: Theme.text
                            font.pixelSize: 12
                            validator: IntValidator { bottom: 0; top: 60000 }
                            background: Rectangle {
                                color: Theme.surface1; radius: Theme.radiusSmall
                                border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.08)
                            }
                            onEditingFinished: {
                                const arr = root.tempoShortActions.slice();
                                arr[index] = Object.assign({}, arr[index], {delayMs: parseInt(text) || 0});
                                root.tempoShortActions = arr;
                            }
                        }
                    }

                    // --- Audio fields ---
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.spacingLg
                        spacing: Theme.spacingXs
                        visible: (modelData.category || "device") === "audio"

                        TextField {
                            Layout.fillWidth: true
                            implicitHeight: 28
                            color: Theme.text
                            font.pixelSize: 12
                            text: modelData.filePath || ""
                            placeholderText: qsTr("C:/Sounds/click.wav")
                            background: Rectangle {
                                color: Theme.surface1; radius: Theme.radiusSmall
                                border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.08)
                            }
                            onEditingFinished: {
                                const arr = root.tempoShortActions.slice();
                                arr[index] = Object.assign({}, arr[index], {filePath: text});
                                root.tempoShortActions = arr;
                            }
                        }
                        ToolButton {
                            label: qsTr("Browse…")
                            onClicked: {
                                tempoAudioDialog.targetArrayName = "short";
                                tempoAudioDialog.targetIndex = index;
                                tempoAudioDialog.open();
                            }
                        }
                    }

                    // --- Text-to-Speech fields ---
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.spacingLg
                        spacing: Theme.spacingXs
                        visible: (modelData.category || "device") === "tts"

                        TextField {
                            Layout.fillWidth: true
                            implicitHeight: 28
                            color: Theme.text
                            font.pixelSize: 12
                            text: modelData.text || ""
                            placeholderText: qsTr("Gear down")
                            background: Rectangle {
                                color: Theme.surface1; radius: Theme.radiusSmall
                                border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.08)
                            }
                            onEditingFinished: {
                                const arr = root.tempoShortActions.slice();
                                arr[index] = Object.assign({}, arr[index], {text: text});
                                root.tempoShortActions = arr;
                            }
                        }
                    }

                    // --- Macro fields (Sprint 6) ---
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.spacingLg
                        spacing: Theme.spacingXs
                        visible: (modelData.category || "device") === "macro"

                        Text {
                            text: (modelData.macroSteps || []).length > 0
                                ? qsTr("%1 step(s) recorded").arg((modelData.macroSteps || []).length)
                                : qsTr("No macro recorded yet")
                            color: Theme.subtext0
                            font.pixelSize: 11
                            Layout.fillWidth: true
                        }
                        ToolButton {
                            label: (modelData.macroSteps || []).length > 0 ? qsTr("Edit Macro…") : qsTr("Record Macro…")
                            onClicked: {
                                tempoMacroEditorPopup.targetArrayName = "short";
                                tempoMacroEditorPopup.targetIndex = index;
                                tempoMacroEditorPopup.openEmbedded(modelData.macroSteps || []);
                            }
                        }
                    }

                    // --- Mode Switch fields ---
                    // Same "ModeSwitch"/"parameters.targetMode" JSON shape
                    // the top-level "Mode Toggle (Sticky)" action type builds
                    // (see toggleModeCombo below) - reusing that actionType
                    // nested inside a cascade needs no C++ changes at all,
                    // see extractTempoActions()'s own docs on the "ModeSwitch"
                    // branch above for why.
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.spacingLg
                        spacing: Theme.spacingXs
                        visible: (modelData.category || "device") === "modeSwitch"

                        Text { text: qsTr("Switch to Mode"); color: Theme.subtext0; font.pixelSize: 11 }
                        AppComboBox {
                            id: tempoShortModeCombo
                            Layout.fillWidth: true
                            model: profileEditorViewModel.modes
                            // Keeps the row's own draft object in sync with
                            // whatever this combo actually displays - if
                            // modelData.targetMode is "" (a freshly-added row,
                            // or a row that just switched INTO this category),
                            // there's no matching mode to select, so this
                            // falls back to index 0 AND writes that mode's
                            // name back into the row, rather than leaving the
                            // row's targetMode "" while the combo visibly
                            // shows a mode - same "widget and draft state
                            // never silently disagree" rule every other field
                            // in this popup already follows.
                            //
                            // Bugfix (binding loop): this AppComboBox is
                            // constructed for EVERY row the Short Press
                            // Repeater builds - not just "modeSwitch" rows -
                            // since QML instantiates a delegate's whole
                            // object tree regardless of a visible: false
                            // ancestor. Two guards, both required:
                            //  1. modelData.category === "modeSwitch": skips
                            //     every other row outright, so this can only
                            //     ever fire for a row that actually needs it.
                            //  2. Qt.callLater(...): writing root.tempoShortActions
                            //     straight from Component.onCompleted - even
                            //     for just the one relevant row - reassigns
                            //     the very array the Repeater above is still
                            //     mid-instantiation from, which re-enters
                            //     Repeater's own model processing and trips
                            //     Qt's binding-loop detector ("Binding loop
                            //     detected for property 'model'"). Deferring
                            //     the write to the next event-loop tick lets
                            //     this instantiation pass finish first, so
                            //     the eventual reassignment (still a normal,
                            //     one-time Repeater rebuild) is an ordinary
                            //     write, not a re-entrant one.
                            Component.onCompleted: {
                                const modeIdx = profileEditorViewModel.modes.indexOf(modelData.targetMode || "");
                                tempoShortModeCombo.currentIndex = modeIdx >= 0 ? modeIdx : 0;
                                if (modelData.category === "modeSwitch" && modeIdx < 0
                                        && profileEditorViewModel.modes.length > 0) {
                                    const rowIndex = index;
                                    const fallbackMode = tempoShortModeCombo.textAt(0);
                                    Qt.callLater(() => {
                                        const arr = root.tempoShortActions.slice();
                                        if (rowIndex < 0 || rowIndex >= arr.length) {
                                            return;
                                        }
                                        arr[rowIndex] = Object.assign({}, arr[rowIndex], {targetMode: fallbackMode});
                                        root.tempoShortActions = arr;
                                    });
                                }
                            }
                            onActivated: (modeIndex) => {
                                const arr = root.tempoShortActions.slice();
                                arr[index] = Object.assign({}, arr[index], {targetMode: tempoShortModeCombo.textAt(modeIndex)});
                                root.tempoShortActions = arr;
                            }
                        }
                    }
                }
            }
            ToolButton {
                label: qsTr("+ Add Action")
                Layout.alignment: Qt.AlignLeft
                onClicked: {
                    const arr = root.tempoShortActions.slice();
                    arr.push({category: "device", targetDeviceType: "vjoy", targetOutputId: 1, targetButton: 0,
                        delayMs: 100, filePath: "", text: "", macroSteps: [], targetMode: ""});
                    root.tempoShortActions = arr;
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacingXs
                height: 1
                color: Qt.rgba(1, 1, 1, 0.1)
            }

            // --- Long Press: cascaded action list ---------------------------
            Text { text: qsTr("Long Press Actions"); color: Theme.subtext0; font.pixelSize: 11; font.weight: Font.DemiBold }
            Repeater {
                model: root.tempoLongActions
                delegate: ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingXs

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        Layout.bottomMargin: 2
                        visible: index > 0
                        color: Qt.rgba(1, 1, 1, 0.06)
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingXs

                        Text { text: (index + 1) + "."; color: Theme.subtext0; font.pixelSize: 11 }

                        AppComboBox {
                            id: tempoLongCategoryCombo
                            Layout.preferredWidth: 140
                            model: [qsTr("Device Output"), qsTr("Delay"), qsTr("Audio"), qsTr("Text-to-Speech"), qsTr("Macro"), qsTr("Mode Switch")]
                            currentIndex: ["device", "delay", "audio", "tts", "macro", "modeSwitch"].indexOf(modelData.category || "device")
                            // Fase (bugfix): see the matching comment on tempoShortCategoryCombo
                            // above - same activated(int index) vs. delegate "index" shadowing bug.
                            onActivated: (categoryIndex) => {
                                const categories = ["device", "delay", "audio", "tts", "macro", "modeSwitch"];
                                const arr = root.tempoLongActions.slice();
                                arr[index] = Object.assign({}, arr[index], {category: categories[categoryIndex]});
                                root.tempoLongActions = arr;
                            }
                        }

                        Item { Layout.fillWidth: true }

                        ToolButton {
                            label: qsTr("×")
                            Layout.preferredWidth: 24
                            onClicked: {
                                const arr = root.tempoLongActions.slice();
                                arr.splice(index, 1);
                                root.tempoLongActions = arr;
                            }
                        }
                    }

                    // --- Device Output fields ---
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.spacingLg
                        spacing: Theme.spacingXs
                        visible: (modelData.category || "device") === "device"

                        OutputDeviceCombo {
                            id: tempoLongDeviceCombo
                            Component.onCompleted: tempoLongDeviceCombo.setFromTarget(modelData)
                            onChanged: {
                                const arr = root.tempoLongActions.slice();
                                arr[index] = Object.assign({}, arr[index], {
                                    targetDeviceType: tempoLongDeviceCombo.targetDeviceType,
                                    targetOutputId: tempoLongDeviceCombo.targetOutputId
                                });
                                root.tempoLongActions = arr;
                            }
                        }

                        Text { text: qsTr("Button"); color: Theme.subtext0; font.pixelSize: 11 }
                        TextField {
                            text: (modelData.targetButton || 0) + 1
                            Layout.preferredWidth: 40
                            implicitHeight: 28
                            color: Theme.text
                            font.pixelSize: 12
                            validator: IntValidator { bottom: 1; top: 128 }
                            background: Rectangle {
                                color: Theme.surface1; radius: Theme.radiusSmall
                                border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.08)
                            }
                            onEditingFinished: {
                                const arr = root.tempoLongActions.slice();
                                arr[index] = Object.assign({}, arr[index], {targetButton: (parseInt(text) || 1) - 1});
                                root.tempoLongActions = arr;
                            }
                        }
                    }

                    // --- Delay fields ---
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.spacingLg
                        spacing: Theme.spacingXs
                        visible: (modelData.category || "device") === "delay"

                        Text { text: qsTr("Milliseconds"); color: Theme.subtext0; font.pixelSize: 11 }
                        TextField {
                            text: modelData.delayMs !== undefined ? modelData.delayMs : 100
                            Layout.preferredWidth: 60
                            implicitHeight: 28
                            color: Theme.text
                            font.pixelSize: 12
                            validator: IntValidator { bottom: 0; top: 60000 }
                            background: Rectangle {
                                color: Theme.surface1; radius: Theme.radiusSmall
                                border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.08)
                            }
                            onEditingFinished: {
                                const arr = root.tempoLongActions.slice();
                                arr[index] = Object.assign({}, arr[index], {delayMs: parseInt(text) || 0});
                                root.tempoLongActions = arr;
                            }
                        }
                    }

                    // --- Audio fields ---
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.spacingLg
                        spacing: Theme.spacingXs
                        visible: (modelData.category || "device") === "audio"

                        TextField {
                            Layout.fillWidth: true
                            implicitHeight: 28
                            color: Theme.text
                            font.pixelSize: 12
                            text: modelData.filePath || ""
                            placeholderText: qsTr("C:/Sounds/click.wav")
                            background: Rectangle {
                                color: Theme.surface1; radius: Theme.radiusSmall
                                border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.08)
                            }
                            onEditingFinished: {
                                const arr = root.tempoLongActions.slice();
                                arr[index] = Object.assign({}, arr[index], {filePath: text});
                                root.tempoLongActions = arr;
                            }
                        }
                        ToolButton {
                            label: qsTr("Browse…")
                            onClicked: {
                                tempoAudioDialog.targetArrayName = "long";
                                tempoAudioDialog.targetIndex = index;
                                tempoAudioDialog.open();
                            }
                        }
                    }

                    // --- Text-to-Speech fields ---
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.spacingLg
                        spacing: Theme.spacingXs
                        visible: (modelData.category || "device") === "tts"

                        TextField {
                            Layout.fillWidth: true
                            implicitHeight: 28
                            color: Theme.text
                            font.pixelSize: 12
                            text: modelData.text || ""
                            placeholderText: qsTr("Gear down")
                            background: Rectangle {
                                color: Theme.surface1; radius: Theme.radiusSmall
                                border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.08)
                            }
                            onEditingFinished: {
                                const arr = root.tempoLongActions.slice();
                                arr[index] = Object.assign({}, arr[index], {text: text});
                                root.tempoLongActions = arr;
                            }
                        }
                    }

                    // --- Macro fields (Sprint 6) ---
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.spacingLg
                        spacing: Theme.spacingXs
                        visible: (modelData.category || "device") === "macro"

                        Text {
                            text: (modelData.macroSteps || []).length > 0
                                ? qsTr("%1 step(s) recorded").arg((modelData.macroSteps || []).length)
                                : qsTr("No macro recorded yet")
                            color: Theme.subtext0
                            font.pixelSize: 11
                            Layout.fillWidth: true
                        }
                        ToolButton {
                            label: (modelData.macroSteps || []).length > 0 ? qsTr("Edit Macro…") : qsTr("Record Macro…")
                            onClicked: {
                                tempoMacroEditorPopup.targetArrayName = "long";
                                tempoMacroEditorPopup.targetIndex = index;
                                tempoMacroEditorPopup.openEmbedded(modelData.macroSteps || []);
                            }
                        }
                    }

                    // --- Mode Switch fields ---
                    // Same "ModeSwitch"/"parameters.targetMode" JSON shape
                    // the top-level "Mode Toggle (Sticky)" action type builds
                    // (see toggleModeCombo below) - see the matching Short
                    // Press block above and extractTempoActions()'s own docs
                    // for why nesting this inside a Tempo cascade needed no
                    // C++ changes at all.
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.spacingLg
                        spacing: Theme.spacingXs
                        visible: (modelData.category || "device") === "modeSwitch"

                        Text { text: qsTr("Switch to Mode"); color: Theme.subtext0; font.pixelSize: 11 }
                        AppComboBox {
                            id: tempoLongModeCombo
                            Layout.fillWidth: true
                            model: profileEditorViewModel.modes
                            // See tempoShortModeCombo's own docs above for
                            // why this writes back a fallback targetMode
                            // instead of just selecting index 0, AND why
                            // that write is guarded to "modeSwitch" rows
                            // only and deferred via Qt.callLater - both are
                            // required to avoid a Repeater "Binding loop
                            // detected for property 'model'" warning.
                            Component.onCompleted: {
                                const modeIdx = profileEditorViewModel.modes.indexOf(modelData.targetMode || "");
                                tempoLongModeCombo.currentIndex = modeIdx >= 0 ? modeIdx : 0;
                                if (modelData.category === "modeSwitch" && modeIdx < 0
                                        && profileEditorViewModel.modes.length > 0) {
                                    const rowIndex = index;
                                    const fallbackMode = tempoLongModeCombo.textAt(0);
                                    Qt.callLater(() => {
                                        const arr = root.tempoLongActions.slice();
                                        if (rowIndex < 0 || rowIndex >= arr.length) {
                                            return;
                                        }
                                        arr[rowIndex] = Object.assign({}, arr[rowIndex], {targetMode: fallbackMode});
                                        root.tempoLongActions = arr;
                                    });
                                }
                            }
                            onActivated: (modeIndex) => {
                                const arr = root.tempoLongActions.slice();
                                arr[index] = Object.assign({}, arr[index], {targetMode: tempoLongModeCombo.textAt(modeIndex)});
                                root.tempoLongActions = arr;
                            }
                        }
                    }
                }
            }
            ToolButton {
                label: qsTr("+ Add Action")
                Layout.alignment: Qt.AlignLeft
                onClicked: {
                    const arr = root.tempoLongActions.slice();
                    arr.push({category: "device", targetDeviceType: "vjoy", targetOutputId: 1, targetButton: 0,
                        delayMs: 100, filePath: "", text: "", macroSteps: [], targetMode: ""});
                    root.tempoLongActions = arr;
                }
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
                text: qsTr("Cycles through a list of vJoy button remaps - each press fires the next one in order.")
                color: Theme.overlay0
                font.pixelSize: 11
                font.italic: true
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            ToolButton {
                label: qsTr("Configure Sequence…")
                Layout.fillWidth: true
                onClicked: sequencePopup.openFor(root.devicePath, root.inputName, root.existingSequenceSteps)
            }
        }

        // --- Toggle / Latching Switch ---------------------------------------
        // Category list deliberately limited to vJoy Button / Keyboard Key /
        // Mouse Click - see toggleActionToWire()'s own docs above for why
        // every other action kind in this codebase would silently do
        // nothing on every other press if wrapped here.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            spacing: Theme.spacingSm
            visible: actionTypeCombo.currentText === "Toggle / Latching Switch"

            Text {
                text: qsTr("Converts a latching physical switch into a proper hold: the first press holds the target action down, the second press releases it.")
                color: Theme.overlay0
                font.pixelSize: 11
                font.italic: true
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: qsTr("Target Action"); color: Theme.subtext0; font.pixelSize: 11 }
                AppComboBox {
                    id: toggleCategoryCombo
                    Layout.fillWidth: true
                    model: [qsTr("vJoy Button"), qsTr("Keyboard Key"), qsTr("Mouse Click")]
                    currentIndex: Math.max(0, ["device", "keyboard", "mouse"]
                        .indexOf(root.toggleWrappedAction.category || "device"))
                    onActivated: (categoryIndex) => {
                        const categories = ["device", "keyboard", "mouse"];
                        root.toggleWrappedAction =
                            Object.assign({}, root.toggleWrappedAction, {category: categories[categoryIndex]});
                    }
                }
            }

            // --- vJoy Button fields ---
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingXs
                visible: (root.toggleWrappedAction.category || "device") === "device"

                OutputDeviceCombo {
                    id: toggleDeviceCombo
                    onChanged: {
                        root.toggleWrappedAction = Object.assign({}, root.toggleWrappedAction, {
                            targetDeviceType: toggleDeviceCombo.targetDeviceType,
                            targetOutputId: toggleDeviceCombo.targetOutputId
                        });
                    }
                }

                Text { text: qsTr("Button"); color: Theme.subtext0; font.pixelSize: 11 }
                TextField {
                    text: (root.toggleWrappedAction.targetButton || 0) + 1
                    Layout.preferredWidth: 40
                    implicitHeight: 28
                    color: Theme.text
                    font.pixelSize: 12
                    validator: IntValidator { bottom: 1; top: 128 }
                    background: Rectangle {
                        color: Theme.surface1; radius: Theme.radiusSmall
                        border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.08)
                    }
                    onEditingFinished: {
                        root.toggleWrappedAction =
                            Object.assign({}, root.toggleWrappedAction, {targetButton: (parseInt(text) || 1) - 1});
                    }
                }
            }

            // --- Keyboard Key fields ---
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingXs
                visible: (root.toggleWrappedAction.category || "device") === "keyboard"

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 32
                    radius: Theme.radiusSmall
                    color: toggleKeyCaptureItem.activeFocus ? Theme.accent : Theme.surface1
                    border.width: 1
                    border.color: Qt.rgba(1, 1, 1, 0.08)
                    Behavior on color { ColorAnimation { duration: Theme.animFast } }

                    Text {
                        anchors.centerIn: parent
                        text: toggleKeyCaptureItem.activeFocus
                            ? qsTr("Press a key…")
                            : (root.toggleWrappedAction.keyLabel || qsTr("Click to set key"))
                        color: toggleKeyCaptureItem.activeFocus ? Theme.crust : Theme.text
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }

                    Item {
                        id: toggleKeyCaptureItem
                        anchors.fill: parent

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: toggleKeyCaptureItem.forceActiveFocus()
                        }

                        Keys.onPressed: (event) => {
                            const code = root.keyScanCodes[event.key];
                            if (code !== undefined) {
                                const lbl = (event.text && event.text.trim().length > 0)
                                    ? event.text.toUpperCase() : ("Key 0x" + event.key.toString(16));
                                root.toggleWrappedAction =
                                    Object.assign({}, root.toggleWrappedAction, {scanCode: code, keyLabel: lbl});
                            } else {
                                root.toggleWrappedAction = Object.assign({}, root.toggleWrappedAction,
                                    {scanCode: -1, keyLabel: qsTr("Unsupported key")});
                            }
                            event.accepted = true;
                        }
                    }
                }
            }

            // --- Mouse Click fields --- (Left/Right/Middle only - see this
            // section's own file comment on why Scroll isn't offered here)
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingXs
                visible: (root.toggleWrappedAction.category || "device") === "mouse"

                Text { text: qsTr("Button"); color: Theme.subtext0; font.pixelSize: 11 }
                AppComboBox {
                    id: toggleMouseActionCombo
                    Layout.fillWidth: true
                    model: [qsTr("Left"), qsTr("Right"), qsTr("Middle")]
                    currentIndex: Math.max(0, ["Left", "Right", "Middle"]
                        .indexOf(root.toggleWrappedAction.mouseAction || "Left"))
                    onActivated: (comboIndex) => {
                        const actions = ["Left", "Right", "Middle"];
                        root.toggleWrappedAction =
                            Object.assign({}, root.toggleWrappedAction, {mouseAction: actions[comboIndex]});
                    }
                }
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
                text: qsTr("Switches to Target Mode while this button is held, and restores the previous mode on release.")
                color: Theme.overlay0
                font.pixelSize: 11
                font.italic: true
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: qsTr("Target Mode"); color: Theme.subtext0; font.pixelSize: 11 }
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
                text: qsTr("Switches to Target Mode permanently when pressed. (No hold required).")
                color: Theme.overlay0
                font.pixelSize: 11
                font.italic: true
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: qsTr("Target Mode"); color: Theme.subtext0; font.pixelSize: 11 }
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
                text: qsTr("Only forwards this input to the action below while the chosen modifier button is held (or released, if set to \"Released\").")
                color: Theme.overlay0
                font.pixelSize: 11
                font.italic: true
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: qsTr("Modifier Device"); color: Theme.subtext0; font.pixelSize: 11 }
                AppComboBox { id: condDeviceCombo; Layout.fillWidth: true; model: profileEditorViewModel.deviceDisplayNames() }
            }

            RowLayout {
                Layout.fillWidth: true
                Text { text: qsTr("Modifier Button"); color: Theme.subtext0; font.pixelSize: 11; Layout.fillWidth: true }
                IntStepper { id: condButtonStepper; from: 0; to: 127 }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: qsTr("Condition"); color: Theme.subtext0; font.pixelSize: 11 }
                AppComboBox { id: condRequirePressed; Layout.fillWidth: true; model: [qsTr("Pressed"), qsTr("Released")] }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacingXs
                height: 1
                color: Qt.rgba(1, 1, 1, 0.1)
            }

            Text { text: qsTr("Wrapped Action (vJoy Remap)"); color: Theme.subtext0; font.pixelSize: 11 }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: qsTr("Target Output Device"); color: Theme.subtext0; font.pixelSize: 11 }
                OutputDeviceCombo { id: condVjoyDeviceCombo }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                visible: root.inputKind === "axis"
                Text { text: qsTr("Target Axis"); color: Theme.subtext0; font.pixelSize: 11 }
                AppComboBox { id: condVjoyAxisCombo; Layout.fillWidth: true; model: root.vjoyAxisNames }
            }

            RowLayout {
                Layout.fillWidth: true
                visible: root.inputKind === "button"
                Text { text: qsTr("Target Button"); color: Theme.subtext0; font.pixelSize: 11; Layout.fillWidth: true }
                IntStepper { id: condVjoyTargetButton; from: 1; to: 128 }
            }
        }

        // --- Delay (Pause) ------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            spacing: Theme.spacingSm
            visible: actionTypeCombo.currentText === "Delay (Pause)"

            Text {
                text: qsTr("Pauses this input's own action pipeline for a fixed duration without blocking any other binding.")
                color: Theme.overlay0
                font.pixelSize: 11
                font.italic: true
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.fillWidth: true
                Text { text: qsTr("Milliseconds"); color: Theme.subtext0; font.pixelSize: 11; Layout.fillWidth: true }
                IntStepper { id: delayMsStepper; from: 0; to: 10000; stepSize: 50; value: 100 }
            }
        }

        // --- Play Audio ------------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            spacing: Theme.spacingSm
            visible: actionTypeCombo.currentText === "Play Audio"

            Text {
                text: qsTr("Plays a .wav or .mp3 file on press.")
                color: Theme.overlay0
                font.pixelSize: 11
                font.italic: true
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: qsTr("Audio File"); color: Theme.subtext0; font.pixelSize: 11 }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingXs
                    TextField {
                        id: audioFilePathField
                        Layout.fillWidth: true
                        implicitHeight: 32
                        color: Theme.text
                        font.pixelSize: 12
                        placeholderText: qsTr("C:/Sounds/click.wav")
                        background: Rectangle {
                            color: Theme.surface1; radius: Theme.radiusSmall
                            border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.08)
                        }
                    }
                    ToolButton {
                        label: qsTr("Browse…")
                        onClicked: audioFileDialog.open()
                    }
                }
            }
        }

        // --- Text-to-Speech --------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            spacing: Theme.spacingSm
            visible: actionTypeCombo.currentText === "Text-to-Speech"

            Text {
                text: qsTr("Reads the text below out loud on press, using Windows' native Text-to-Speech engine.")
                color: Theme.overlay0
                font.pixelSize: 11
                font.italic: true
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: qsTr("Text to Speak"); color: Theme.subtext0; font.pixelSize: 11 }
                TextField {
                    id: ttsTextField
                    Layout.fillWidth: true
                    implicitHeight: 32
                    color: Theme.text
                    font.pixelSize: 12
                    placeholderText: qsTr("Gear down")
                    background: Rectangle {
                        color: Theme.surface1; radius: Theme.radiusSmall
                        border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.08)
                    }
                }
            }
        }

            } // end ColumnLayout (actionFieldsScroll's content)
        } // end ScrollView (actionFieldsScroll)

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
                label: qsTr("Copy")
                visible: root.hasBinding
                onClicked: {
                    profileEditorViewModel.copyAction(root.devicePath, root.inputName, profileEditorViewModel.currentMode, root.inputKind);
                    root.close();
                }
            }
            ToolButton {
                label: qsTr("Paste")
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
                label: qsTr("Unbind")
                visible: root.hasBinding
                onClicked: {
                    profileEditorViewModel.unbindAction(root.devicePath, root.inputName, profileEditorViewModel.currentMode);
                    root.close();
                }
            }
            ToolButton {
                label: qsTr("Cancel")
                onClicked: root.close()
            }
            ToolButton {
                label: qsTr("Apply")
                visible: actionTypeCombo.currentText !== "Axis-to-Button"
                    && actionTypeCombo.currentText !== "Sequence / Rotary"
                enabled: (actionTypeCombo.currentText !== "Keyboard" || root.capturedScanCode >= 0)
                    && (actionTypeCombo.currentText !== "Tempo (Short/Long Press)"
                        || ((root.tempoShortActions.length > 0 || root.tempoLongActions.length > 0)
                            && root.tempoActionsValid(root.tempoShortActions)
                            && root.tempoActionsValid(root.tempoLongActions)))
                    && (actionTypeCombo.currentText !== "Play Audio" || audioFilePathField.text.trim().length > 0)
                    && (actionTypeCombo.currentText !== "Text-to-Speech" || ttsTextField.text.trim().length > 0)
                    && (actionTypeCombo.currentText !== "Toggle / Latching Switch"
                        || root.toggleWrappedAction.category !== "keyboard"
                        || root.toggleWrappedAction.scanCode > 0)
                opacity: enabled ? 1.0 : 0.5
                onClicked: {
                    let actionData;
                    if (actionTypeCombo.currentText === "vJoy Remap") {
                        const target = { targetDeviceType: vjoyDeviceCombo.targetDeviceType,
                                          targetOutputId: vjoyDeviceCombo.targetOutputId };
                        const isXbox = vjoyDeviceCombo.isXbox;
                        if (root.inputKind === "axis") {
                            const curveAction = { actionType: "CurveHandler",
                                                   targetDeviceType: target.targetDeviceType,
                                                   targetOutputId: target.targetOutputId,
                                                   targetAxis: isXbox ? xboxAxisCombo.currentIndex : vjoyAxisCombo.currentIndex,
                                                   parameters: { invert: invertAxisCheck.checked } };
                            actionData = smoothingToggle.checked
                                ? { actionType: "SmoothingHandler",
                                    parameters: { smoothingFactor: smoothingFactorSlider.value },
                                    wrappedAction: curveAction }
                                : curveAction;
                        } else {
                            actionData = { actionType: "ButtonRemapHandler",
                                            targetDeviceType: target.targetDeviceType,
                                            targetOutputId: target.targetOutputId,
                                            targetButton: isXbox ? xboxButtonCombo.currentIndex : vjoyTargetButton.value - 1 };
                        }
                    } else if (actionTypeCombo.currentText === "Keyboard") {
                        actionData = { actionType: "KeyboardHandler",
                                        parameters: { scanCode: root.capturedScanCode } };
                    } else if (actionTypeCombo.currentText === "Macro") {
                        actionData = { actionType: "MacroHandler",
                                        targetDeviceType: macroDeviceCombo.targetDeviceType,
                                        targetOutputId: macroDeviceCombo.targetOutputId,
                                        targetButton: macroTargetButton.value - 1,
                                        parameters: { waitMs: macroDuration.value } };
                    } else if (actionTypeCombo.currentText === "Tempo (Short/Long Press)") {
                        // Each row's "category" picks which of the 4 action
                        // JSON shapes it becomes - the same actionTypes the
                        // main Action Type dropdown itself builds (see
                        // "Delay (Pause)"/"Play Audio"/"Text-to-Speech"
                        // below), so ProfileManager's generic
                        // instantiateHandler() dispatch needs no Tempo-
                        // specific handling at all.
                        const toCascadeAction = (a) => {
                            const category = a.category || "device";
                            if (category === "delay") {
                                return { actionType: "DelayAction", parameters: { delayMs: a.delayMs || 0 } };
                            } else if (category === "audio") {
                                return { actionType: "AudioAction", parameters: { filePath: (a.filePath || "").trim() } };
                            } else if (category === "tts") {
                                return { actionType: "TTSAction", parameters: { text: (a.text || "").trim() } };
                            } else if (category === "macro") {
                                // Sprint 6: a.macroSteps is already the exact
                                // {type, scanCode}/{type:"Wait", waitMs} shape
                                // MacroHandler::toJson() itself writes (see
                                // MacroEditorPopup's macroReady() and
                                // extractTempoActions() above) - just rewrap it.
                                return { actionType: "MacroHandler", parameters: { steps: a.macroSteps || [] } };
                            } else if (category === "modeSwitch") {
                                return { actionType: "ModeSwitch", parameters: { targetMode: a.targetMode || "" } };
                            }
                            return { actionType: "ButtonRemapHandler",
                                targetDeviceType: a.targetDeviceType || "vjoy",
                                targetOutputId: a.targetOutputId || 1,
                                targetButton: a.targetButton || 0 };
                        };
                        actionData = {
                            actionType: "TempoHandler",
                            parameters: { longPressMs: tempoThresholdMs.value, pulseDurationMs: tempoPulseMs.value },
                            shortActions: root.tempoShortActions.map(toCascadeAction),
                            longActions: root.tempoLongActions.map(toCascadeAction)
                        };
                    } else if (actionTypeCombo.currentText === "Condition / Modifier Check") {
                        const condTarget = { targetDeviceType: condVjoyDeviceCombo.targetDeviceType,
                                             targetOutputId: condVjoyDeviceCombo.targetOutputId };
                        const condWrappedAction = root.inputKind === "axis"
                            ? { actionType: "CurveHandler", targetDeviceType: condTarget.targetDeviceType,
                                targetOutputId: condTarget.targetOutputId,
                                targetAxis: condVjoyAxisCombo.currentIndex }
                            : { actionType: "ButtonRemapHandler", targetDeviceType: condTarget.targetDeviceType,
                                targetOutputId: condTarget.targetOutputId,
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
                                        targetDeviceType: hatDeviceCombo.targetDeviceType,
                                        targetOutputId: hatDeviceCombo.targetOutputId,
                                        targetHat: hatTargetCombo.currentIndex,
                                        targetDirection: hatDirectionCombo.currentIndex };
                    } else if (actionTypeCombo.currentText === "Toggle / Latching Switch") {
                        // "wrappedAction" is a top-level sibling of "actionType",
                        // NOT nested under "parameters" - matches ProfileManager::
                        // instantiateToggleHandler()'s actual schema (same
                        // convention ConditionHandler's own wrappedAction uses
                        // above), not the shape a "parameters"-nested reading of
                        // the schema might suggest.
                        actionData = { actionType: "ToggleHandler",
                                        wrappedAction: root.toggleActionToWire(root.toggleWrappedAction) };
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
                            targetDeviceType: vjoyDeviceCombo.targetDeviceType,
                            targetOutputId: vjoyDeviceCombo.targetOutputId,
                            targetAxis: vjoyDeviceCombo.isXbox ? xboxAxisCombo.currentIndex : vjoyAxisCombo.currentIndex,
                            parameters: {
                                otherSystemPath: profileEditorViewModel.systemPathForDevice(mergeDeviceCombo.currentIndex),
                                otherAxisIndex: mergeAxisCombo.currentIndex,
                                isSubtraction: mergeModeCombo.currentIndex === 1
                            }
                        };
                    } else if (actionTypeCombo.currentText === "Mouse X Axis" ||
                               actionTypeCombo.currentText === "Mouse Y Axis") {
                        actionData = {
                            actionType: "MouseRelativeAxis",
                            parameters: {
                                targetMouseAxis: actionTypeCombo.currentText === "Mouse Y Axis" ? "Y" : "X",
                                sensitivity: mouseSensitivitySlider.value,
                                deadzone: mouseDeadzoneSlider.value
                            }
                        };
                    } else if (actionTypeCombo.currentText === "Delay (Pause)") {
                        actionData = { actionType: "DelayAction",
                                        parameters: { delayMs: delayMsStepper.value } };
                    } else if (actionTypeCombo.currentText === "Play Audio") {
                        actionData = { actionType: "AudioAction",
                                        parameters: { filePath: audioFilePathField.text.trim() } };
                    } else if (actionTypeCombo.currentText === "Text-to-Speech") {
                        actionData = { actionType: "TTSAction",
                                        parameters: { text: ttsTextField.text.trim() } };
                    } else if (actionTypeCombo.currentText === "Mouse Left Click") {
                        actionData = { actionType: "MouseButton", parameters: { targetAction: "Left" } };
                    } else if (actionTypeCombo.currentText === "Mouse Right Click") {
                        actionData = { actionType: "MouseButton", parameters: { targetAction: "Right" } };
                    } else if (actionTypeCombo.currentText === "Mouse Middle Click") {
                        actionData = { actionType: "MouseButton", parameters: { targetAction: "Middle" } };
                    } else if (actionTypeCombo.currentText === "Mouse Scroll Up") {
                        actionData = { actionType: "MouseButton", parameters: { targetAction: "ScrollUp" } };
                    } else if (actionTypeCombo.currentText === "Mouse Scroll Down") {
                        actionData = { actionType: "MouseButton", parameters: { targetAction: "ScrollDown" } };
                    } else {
                        actionData = { actionType: "ModeSwitch",
                                        parameters: { targetMode: toggleModeCombo.currentText } };
                    }

                    // Sprint QoL Part 2: applies to whatever actionData just
                    // got built above, regardless of actionType - only
                    // touches "parameters" (creating it if this type had
                    // none of its own, e.g. a plain ButtonRemapHandler) when
                    // there's actually a note to store, and removes any
                    // stale one instead of writing an empty string when the
                    // field was cleared, so a binding with no note keeps the
                    // exact same JSON shape it always had.
                    const noteText = actionNoteField.text.trim();
                    if (noteText.length > 0) {
                        actionData.parameters = actionData.parameters || {};
                        actionData.parameters.note = noteText;
                    } else if (actionData.parameters && actionData.parameters.note !== undefined) {
                        delete actionData.parameters.note;
                    }

                    const applied = profileEditorViewModel.bindAction(
                        root.devicePath, root.inputName, profileEditorViewModel.currentMode,
                        JSON.stringify(actionData));
                    if (applied) {
                        // Guardar en memoria para auto-incremento
                        if (actionTypeCombo.currentText === "vJoy Remap") {
                            root.lastVjoyTarget = { targetDeviceType: vjoyDeviceCombo.targetDeviceType,
                                                     targetOutputId: vjoyDeviceCombo.targetOutputId };
                            if (root.inputKind === "axis") {
                                root.lastVjoyAxis = (vjoyAxisCombo.currentIndex + 1) % vjoyAxisNames.length;
                            } else {
                                // vjoyTargetButton.value is now the display value (1-128);
                                // wrap to 0 once it passes the last button (128).
                                root.lastVjoyButton = vjoyTargetButton.value % 128;

                                // Bugfix: feeds the C++-side "last assigned"
                                // memory (per vJoy device id) that
                                // nextAvailableVjoyButton() now searches
                                // forward from - root.lastVjoyButton above is
                                // clobbered by openFor()'s own
                                // nextAvailableVjoyButton() call the very
                                // next time this popup opens for ANY input,
                                // so it can't be what makes a manual jump
                                // down to a lower button "stick" for the
                                // next Add; this call is what actually
                                // persists it across the whole session.
                                // Xbox targets (isXbox) are skipped - vJoy's
                                // targetOutputId space (1-16) and Xbox's
                                // ad-hoc slot space (1-4) aren't the same
                                // numbering, and nextAvailableVjoyButton()
                                // only ever means the former.
                                if (!vjoyDeviceCombo.isXbox) {
                                    profileEditorViewModel.recordVjoyButtonAssigned(
                                        vjoyDeviceCombo.targetOutputId, vjoyTargetButton.value - 1);
                                }
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

    // Sprint 6: shared by every "Macro" category row inside the Tempo Short/
    // Long cascade lists - targetArrayName/targetIndex say which row (set
    // right before .openEmbedded()) gets the recorded steps written back to
    // it, same pattern as tempoAudioDialog below. A SEPARATE instance from
    // macroEditorPopup above: that one calls bindAction() and closes this
    // whole ActionPickerPopup on Apply (correct for the top-level "Macro"
    // action type, wrong here - see MacroEditorPopup's own file-level
    // comment on embeddedMode/openEmbedded()/macroReady()).
    MacroEditorPopup {
        id: tempoMacroEditorPopup
        property string targetArrayName: "short"
        property int targetIndex: -1
        onMacroReady: (actionDataJson) => {
            if (targetIndex < 0) {
                return;
            }
            const steps = JSON.parse(actionDataJson).parameters.steps;
            if (targetArrayName === "long") {
                const arr = root.tempoLongActions.slice();
                arr[targetIndex] = Object.assign({}, arr[targetIndex], {macroSteps: steps});
                root.tempoLongActions = arr;
            } else {
                const arr = root.tempoShortActions.slice();
                arr[targetIndex] = Object.assign({}, arr[targetIndex], {macroSteps: steps});
                root.tempoShortActions = arr;
            }
        }
    }

    AxisSplitterPopup {
        id: axisSplitterPopup
        onApplied: root.close()
    }

    SequencePopup {
        id: sequencePopup
        onApplied: root.close()
    }

    FileDialog {
        id: audioFileDialog
        title: qsTr("Select Audio File")
        nameFilters: [qsTr("Audio Files (*.wav *.mp3)")]
        fileMode: FileDialog.OpenFile
        onAccepted: {
            audioFilePathField.text = selectedFile.toString().replace("file:///", "");
        }
    }

    // Shared by every "Audio" row's own "Browse…" button inside the Tempo
    // Short/Long cascade lists - targetArrayName/targetIndex say which row
    // (set right before .open()) gets the picked path written back to it.
    FileDialog {
        id: tempoAudioDialog
        property string targetArrayName: "short"
        property int targetIndex: -1
        title: qsTr("Select Audio File")
        nameFilters: [qsTr("Audio Files (*.wav *.mp3)")]
        fileMode: FileDialog.OpenFile
        onAccepted: {
            if (targetIndex < 0) {
                return;
            }
            const path = selectedFile.toString().replace("file:///", "");
            if (targetArrayName === "long") {
                const arr = root.tempoLongActions.slice();
                arr[targetIndex] = Object.assign({}, arr[targetIndex], {filePath: path});
                root.tempoLongActions = arr;
            } else {
                const arr = root.tempoShortActions.slice();
                arr[targetIndex] = Object.assign({}, arr[targetIndex], {filePath: path});
                root.tempoShortActions = arr;
            }
        }
    }
}
