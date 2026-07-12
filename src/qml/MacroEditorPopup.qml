import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Effects
import GremblingNexus

// Macro Editor (Fase 10.9): the "real" editor superseding the Action
// Picker's Fase 10.8 quick vJoy timed-press shorthand (still available
// separately - see ActionPickerPopup's Macro tab). Record live keyboard
// input via the C++ macroRecorder (MacroRecorderViewModel installs an
// application-wide event filter while recording - see its class docs for
// why a plain QML Keys.onPressed isn't enough here), edit delays inline,
// delete/reorder/manually-insert steps, then Apply.
//
// Two ways this gets used (Sprint 6 added the second):
//  - Standalone (openFor(), embeddedMode false): Apply calls
//    profileEditorViewModel.bindAction() directly and closes itself - the
//    original Fase 10.9 flow, still what ActionPickerPopup's top-level
//    "Macro" tab uses. openFor() now also restores whatever MacroHandler
//    steps are already bound at (devicePath, inputName) via
//    profileEditorViewModel.getActionDataJson() - the same read-side call
//    ActionPickerPopup itself uses to re-populate its own fields - instead
//    of always starting from a blank sequence.
//  - Embedded (openEmbedded(), embeddedMode true): there is no single
//    physical input to bind here - this popup is being used to record/edit
//    one step inside a TempoHandler's own shortActions/longActions cascade
//    (see ActionPickerPopup's "Macro" category on a Tempo row). Apply
//    instead emits macroReady(json) with the built actionType JSON and
//    closes; the opener (a Tempo row) is responsible for storing it.
//
// Step storage (UI overhaul): steps live in stepsModel, a real ListModel,
// not a plain JS array - ListModel.move() relocates an existing delegate
// instance in place (preserving its in-flight state), where reassigning a
// plain JS array to a ListView's model destroys and recreates every
// delegate on every edit. That distinction is what makes live
// click-and-drag reordering (see the delegate's DragHandler below) actually
// work: swapping the dragged row into a new slot mid-gesture would kill the
// drag itself if the delegate were destroyed and rebuilt in the process.
// Every row always carries the same seven roles - type/scanCode/waitMs/
// targetAction/buttonIndex/targetOutputId/label - regardless of which of
// the eight MacroStep kinds it represents (unused roles for a given kind
// just sit at their placeholder default), because ListModel fixes its role
// set from the very first append() and silently drops any key not present
// in that first row.
//
// Joystick buttons (feature request): a PressButton/ReleaseButton step
// targets a *specific* vJoy/ViGEm device (buttonIndex + targetOutputId),
// not just "whatever's bound" - MacroHandler itself only has one such
// target for its entire step sequence (see MacroHandler.h), so
// root.macroTargetOutputId/macroTargetIsVigem track whichever device the
// first recorded button step locked in; a later button that resolves to a
// *different* device is rejected (see onButtonStepRecorded below) rather
// than silently producing a macro that can't represent what was recorded.
Popup {
    id: root

    modal: true
    focus: true
    parent: Overlay.overlay // Fase 20.15: escape the opening popup's own coordinate system
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    width: 420
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    property string devicePath: ""
    property string inputName: ""

    /// Sprint 6: see the file-level comment above for what this toggles -
    /// set by openFor()/openEmbedded(), read only by the Apply button below.
    property bool embeddedMode: false

    /// The vJoy/ViGEm device this macro's PressButton/ReleaseButton steps
    /// (if any) target - 0 means "none yet" (see the file-level comment
    /// above on why there can only be one). Set from the restored binding's
    /// own top-level "targetOutputId" in openFor(), or from the first
    /// recorded button step in onButtonStepRecorded below.
    property int macroTargetOutputId: 0
    property bool macroTargetIsVigem: false

    /// Short-lived warning surfaced in place of the recording status line
    /// (see recorderWarningTimer below) - e.g. "that button isn't a direct
    /// vJoy assignment" from MacroRecorderViewModel::buttonRecordSkipped(),
    /// or a same-macro device-conflict rejection from onButtonStepRecorded.
    property string recorderWarning: ""

    Timer {
        id: recorderWarningTimer
        interval: 2500
        onTriggered: root.recorderWarning = ""
    }

    /// Fixed delegate height - both the ListView delegate itself and the
    /// drag-reorder threshold math below key off this same constant, so a
    /// drag swaps rows exactly when the dragged item crosses a neighbor's
    /// boundary, never early or late.
    readonly property int rowHeight: 36

    /// Qt::Key -> PS/2 Set 1 hardware scan code (see KeyboardHandler /
    /// IKeyboardBackend - a *hardware* scan code, not a virtual-key code).
    /// Copied from ActionPickerPopup.qml's own table (same rationale: this
    /// popup is self-contained, like every other popup in this codebase -
    /// see the file-level comment). Backs the "+ Key" manual-insert capture
    /// below - a one-shot Keys.onPressed capture, deliberately NOT the
    /// macroRecorder (see insertKeyPopup's own docs for why).
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

    /// Fires after a successful bindAction() + self-close - lets
    /// ActionPickerPopup close itself too, the same "opener reacts to
    /// completion" pattern used nowhere else yet in this codebase but
    /// necessary now that a bind can complete from a popup nested inside
    /// another popup.
    signal applied()

    /// Sprint 6: fired instead of applied() when embeddedMode is true -
    /// carries the built {"actionType":"MacroHandler","parameters":{"steps":[...]}}
    /// JSON string back to whoever called openEmbedded(), which owns storing
    /// it (there's no devicePath/inputName to bindAction() against here).
    signal macroReady(string actionDataJson)

    /// Converts one MacroHandler::toJson() "parameters.steps" wire array
    /// (whatever shape a round-tripped profile, or a Tempo cascade row's own
    /// stored macroSteps, actually carries) into stepsModel's five-role
    /// display shape - synthesizing a human-readable "label" for whichever
    /// steps don't carry one on the wire (every step except a freshly
    /// live-recorded key, which is never what this function is fed).
    function stepsFromWireArray(rawSteps, targetOutputId) {
        return (rawSteps || []).map((s) => {
            if (s.type === "Wait") {
                return { type: "Wait", scanCode: 0, waitMs: s.waitMs || 0, targetAction: "",
                    buttonIndex: 0, targetOutputId: 0, label: "" };
            }
            if (s.type === "PressKey" || s.type === "ReleaseKey") {
                const scanCode = s.scanCode || 0;
                return { type: s.type, scanCode: scanCode, waitMs: 0, targetAction: "",
                    buttonIndex: 0, targetOutputId: 0, label: "Key 0x" + scanCode.toString(16) };
            }
            if (s.type === "PressMouseButton" || s.type === "ReleaseMouseButton" || s.type === "MouseScroll") {
                const action = s.targetAction || "Left";
                return { type: s.type, scanCode: 0, waitMs: 0, targetAction: action,
                    buttonIndex: 0, targetOutputId: 0, label: action };
            }
            if (s.type === "PressButton" || s.type === "ReleaseButton") {
                const btn = s.buttonIndex || 0;
                return { type: s.type, scanCode: 0, waitMs: 0, targetAction: "",
                    buttonIndex: btn, targetOutputId: targetOutputId || 0, label: qsTr("Button ") + (btn + 1) };
            }
            return null; // Unrecognized step type - dropped rather than crashing the popup open.
        }).filter((s) => s !== null);
    }

    /// Replaces stepsModel's entire contents with list (already in
    /// stepsModel's five-role shape - see stepsFromWireArray()).
    function loadStepsIntoModel(list) {
        stepsModel.clear();
        for (let i = 0; i < list.length; i++) {
            stepsModel.append(list[i]);
        }
    }

    /// The inverse of stepsFromWireArray() - rebuilds the actual
    /// MacroHandler::toJson()-shaped "steps" array (Apply/macroReady's own
    /// payload) from stepsModel's live state, dropping the display-only
    /// "label" role and whichever of scanCode/waitMs/targetAction doesn't
    /// apply to a given step's type.
    function wireStepsFromModel() {
        const result = [];
        for (let i = 0; i < stepsModel.count; i++) {
            const s = stepsModel.get(i);
            if (s.type === "Wait") {
                result.push({ type: "Wait", waitMs: s.waitMs });
            } else if (s.type === "PressKey" || s.type === "ReleaseKey") {
                result.push({ type: s.type, scanCode: s.scanCode });
            } else if (s.type === "PressButton" || s.type === "ReleaseButton") {
                result.push({ type: s.type, buttonIndex: s.buttonIndex });
            } else {
                result.push({ type: s.type, targetAction: s.targetAction });
            }
        }
        return result;
    }

    /// Delegate display text for one row - the "Key Down: G" / "Mouse Up:
    /// Left" / "Scroll Up" labeling the old inline code used to build
    /// straight in the delegate, now shared so every append site (recorder,
    /// restore, manual insert) only has to store a bare label/targetAction.
    function stepDisplayText(type, label, targetAction) {
        switch (type) {
        case "PressKey": return qsTr("Key Down: ") + label;
        case "ReleaseKey": return qsTr("Key Up: ") + label;
        case "PressMouseButton": return qsTr("Mouse Down: ") + label;
        case "ReleaseMouseButton": return qsTr("Mouse Up: ") + label;
        case "MouseScroll": return targetAction === "ScrollUp" ? qsTr("Scroll Up") : qsTr("Scroll Down");
        case "PressButton": return qsTr("vJoy Down: ") + label;
        case "ReleaseButton": return qsTr("vJoy Up: ") + label;
        default: return label;
        }
    }

    /// Identifies which Press/Release step a given row would pair with -
    /// same scan code for keyboard, same targetAction for mouse, same
    /// (targetOutputId, buttonIndex) for a vJoy button (Feature: reorder
    /// guard below) - or null for a step with no press/release concept at
    /// all (Wait, MouseScroll), which orderKeepsPressBeforeRelease() below
    /// simply ignores.
    function stepGroupId(s) {
        if (s.type === "PressKey" || s.type === "ReleaseKey") return "key:" + s.scanCode;
        if (s.type === "PressMouseButton" || s.type === "ReleaseMouseButton") return "mouse:" + s.targetAction;
        if (s.type === "PressButton" || s.type === "ReleaseButton") return "btn:" + s.targetOutputId + ":" + s.buttonIndex;
        return null;
    }

    /// True if, reading orderedSteps top to bottom, every Release is
    /// preceded by a still-unmatched Press of the same key/button/mouse
    /// action (running per-group balance, like matching parentheses - two
    /// Press/Release pairs of the *same* key can coexist in one sequence,
    /// e.g. a double-tap macro, so this can't just check "is there a Press
    /// anywhere earlier"). Used by the drag-reorder handler below to refuse
    /// a swap that would put a Release before its own Press - playing back
    /// a Release with no Press first would leave that key/button/mouse
    /// action stuck logically "held" in whatever backend receives it,
    /// exactly the risk a manually-dragged step could otherwise introduce.
    function orderKeepsPressBeforeRelease(orderedSteps) {
        const balance = {};
        for (const s of orderedSteps) {
            const groupId = stepGroupId(s);
            if (groupId === null) {
                continue;
            }
            const isPress = s.type === "PressKey" || s.type === "PressMouseButton" || s.type === "PressButton";
            if (isPress) {
                balance[groupId] = (balance[groupId] || 0) + 1;
            } else {
                const current = balance[groupId] || 0;
                if (current <= 0) {
                    return false;
                }
                balance[groupId] = current - 1;
            }
        }
        return true;
    }

    /// Finds the step that pairs with steps[index] - the matching Release
    /// for a Press, or the matching Press for a Release - using proper
    /// bracket-matching (a running per-group depth counter, not just "the
    /// nearest one"), so it still finds the *correct* partner when the same
    /// key/button/mouse action appears more than once in the sequence (e.g.
    /// a double-tap macro: Press,Release,Press,Release - deleting the
    /// second Press must not grab the first Release). -1 if steps[index]
    /// has no pairing concept (Wait, MouseScroll) or is already unpaired
    /// (e.g. a malformed/legacy sequence). Used by the delete button below
    /// so removing either half of a pair removes both, instead of leaving
    /// a lone Release with nothing to precede it (see
    /// orderKeepsPressBeforeRelease()'s own docs on why that's a real risk,
    /// not just cosmetic) or a lone Press that never releases.
    function findPairedStepIndex(steps, index) {
        const s = steps[index];
        const groupId = stepGroupId(s);
        if (groupId === null) {
            return -1;
        }
        const isPress = s.type === "PressKey" || s.type === "PressMouseButton" || s.type === "PressButton";
        let depth = 1;
        if (isPress) {
            for (let i = index + 1; i < steps.length; i++) {
                if (stepGroupId(steps[i]) !== groupId) {
                    continue;
                }
                const otherIsPress = steps[i].type === "PressKey" || steps[i].type === "PressMouseButton" || steps[i].type === "PressButton";
                depth += otherIsPress ? 1 : -1;
                if (depth === 0) {
                    return i;
                }
            }
        } else {
            for (let i = index - 1; i >= 0; i--) {
                if (stepGroupId(steps[i]) !== groupId) {
                    continue;
                }
                const otherIsPress = steps[i].type === "PressKey" || steps[i].type === "PressMouseButton" || steps[i].type === "PressButton";
                depth += otherIsPress ? -1 : 1;
                if (depth === 0) {
                    return i;
                }
            }
        }
        return -1;
    }

    /// Deletes index from stepsModel - and, if it's one half of a Press/
    /// Release pair (see findPairedStepIndex()), its partner too, in one
    /// click, so the "×" button can never leave a dangling unpaired step
    /// behind. Shared by every macro editor surface (standalone Macro tab
    /// and Tempo-embedded), since both use this same delegate.
    function deleteStepWithPartner(index) {
        const snapshot = [];
        for (let i = 0; i < stepsModel.count; i++) {
            snapshot.push(stepsModel.get(i));
        }
        const partnerIndex = findPairedStepIndex(snapshot, index);
        if (partnerIndex >= 0) {
            const first = Math.min(index, partnerIndex);
            const second = Math.max(index, partnerIndex);
            stepsModel.remove(second);
            stepsModel.remove(first);
        } else {
            stepsModel.remove(index);
        }
    }

    function openFor(devicePath_, inputName_) {
        root.devicePath = devicePath_;
        root.inputName = inputName_;
        root.embeddedMode = false;
        root.macroTargetOutputId = 0;
        root.macroTargetIsVigem = false;
        root.recorderWarning = "";
        macroRecorder.stopRecording();

        // Fase (UI overhaul): re-populate from whatever MacroHandler is
        // already bound here, instead of always starting blank - the same
        // getActionDataJson() read-side call ActionPickerPopup.qml's own
        // openFor() uses for every other action type (Fase 20.17).
        let restored = [];
        const existingJsonStr = profileEditorViewModel.getActionDataJson(
            devicePath_, inputName_, profileEditorViewModel.currentMode);
        if (existingJsonStr !== "") {
            try {
                const actionData = JSON.parse(existingJsonStr);
                if (actionData.actionType === "MacroHandler" && actionData.parameters) {
                    restored = stepsFromWireArray(actionData.parameters.steps, actionData.targetOutputId || 0);
                    root.macroTargetOutputId = actionData.targetOutputId || 0;
                    root.macroTargetIsVigem = actionData.targetDeviceType === "vigem";
                }
            } catch (e) {
                restored = []; // Malformed JSON - same tolerant fallback as every other restore path.
            }
        }
        loadStepsIntoModel(restored);
        root.open();
    }

    /// Sprint 6: opened from a "Macro" category row inside ActionPickerPopup's
    /// Tempo (Short/Long Press) cascade editor instead of the top-level
    /// "Macro" action tab. existingSteps is that row's own previously-saved
    /// macroSteps (the same wire shape MacroHandler::toJson() writes - see
    /// extractTempoActions() - not the live-recording shape), so re-opening
    /// an already-recorded row shows what's there instead of starting blank.
    function openEmbedded(existingSteps, targetOutputId, targetIsVigem) {
        root.devicePath = "";
        root.inputName = qsTr("Tempo cascade step");
        root.embeddedMode = true;
        root.macroTargetOutputId = targetOutputId || 0;
        root.macroTargetIsVigem = targetIsVigem || false;
        root.recorderWarning = "";
        loadStepsIntoModel(stepsFromWireArray(existingSteps || [], root.macroTargetOutputId));
        macroRecorder.stopRecording();
        root.open();
    }

    onClosed: macroRecorder.stopRecording()

    ListModel {
        id: stepsModel
    }

    Connections {
        target: macroRecorder
        function onStepRecorded(kind, scanCode, waitMs, label) {
            if (kind === "Wait") {
                stepsModel.append({ type: "Wait", scanCode: 0, waitMs: waitMs, targetAction: "",
                    buttonIndex: 0, targetOutputId: 0, label: "" });
            } else {
                stepsModel.append({ type: kind, scanCode: scanCode, waitMs: 0, targetAction: "",
                    buttonIndex: 0, targetOutputId: 0, label: label });
            }
        }

        /// Joystick buttons (feature request): only reached for a button
        /// that resolved to a direct ButtonRemapHandler (see
        /// MacroRecorderViewModel::onButtonPressed()) - a plain vJoy target/
        /// button pair. Rejects (with a warning, not a silently-wrong step)
        /// a button that would point this macro's PressButton/ReleaseButton
        /// steps at a *different* device than one already locked in - see
        /// the file-level comment on why MacroHandler can't represent that.
        function onButtonStepRecorded(kind, targetOutputId, isVigemTarget, buttonIndex, label) {
            if (root.macroTargetOutputId !== 0 && root.macroTargetOutputId !== targetOutputId) {
                root.recorderWarning = qsTr("That button targets a different vJoy device than this macro already uses - not recorded.");
                recorderWarningTimer.restart();
                return;
            }
            root.macroTargetOutputId = targetOutputId;
            root.macroTargetIsVigem = isVigemTarget;
            stepsModel.append({ type: kind, scanCode: 0, waitMs: 0, targetAction: "",
                buttonIndex: buttonIndex, targetOutputId: targetOutputId, label: label });
        }

        function onButtonRecordSkipped(reason) {
            root.recorderWarning = reason;
            recorderWarningTimer.restart();
        }
    }

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

        Item { Layout.preferredHeight: Theme.spacingMd }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            spacing: 2

            Text { text: qsTr("Macro Editor"); color: Theme.text; font.pixelSize: 17; font.weight: Font.DemiBold }
            Text {
                text: root.inputName + " · mode: " + profileEditorViewModel.currentMode
                color: Theme.subtext0
                font.pixelSize: 12
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            spacing: Theme.spacingSm

            Rectangle {
                Layout.preferredWidth: 130
                implicitHeight: 34
                radius: Theme.radiusSmall
                color: macroRecorder.recording ? Theme.success : (recordArea.containsMouse ? Theme.surface2 : Theme.surface1)
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.08)
                Behavior on color { ColorAnimation { duration: Theme.animFast } }

                RowLayout {
                    anchors.centerIn: parent
                    spacing: 6
                    Rectangle {
                        width: 8; height: 8; radius: 4
                        color: macroRecorder.recording ? Theme.crust : "#f38ba8"
                    }
                    Text {
                        text: macroRecorder.recording ? qsTr("Stop") : qsTr("Record")
                        color: macroRecorder.recording ? Theme.crust : Theme.text
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }
                }

                MouseArea {
                    id: recordArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: macroRecorder.recording ? macroRecorder.stopRecording() : macroRecorder.startRecording()
                }
            }

            Text {
                text: root.recorderWarning !== "" ? root.recorderWarning
                    : macroRecorder.recording
                        ? qsTr("Recording… press keys or joystick buttons now")
                        : qsTr("Click Record, then press keys or joystick buttons for this macro")
                color: root.recorderWarning !== "" ? Theme.warning : (macroRecorder.recording ? Theme.success : Theme.subtext0)
                font.pixelSize: 12
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            Layout.preferredHeight: 220
            radius: Theme.radiusSmall
            color: Theme.surface1
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.08)
            clip: true

            Text {
                anchors.centerIn: parent
                visible: stepsModel.count === 0
                text: qsTr("No steps recorded yet")
                color: Theme.overlay0
                font.pixelSize: 12
                font.italic: true
            }

            ListView {
                id: stepsListView
                anchors.fill: parent
                anchors.margins: Theme.spacingXs
                clip: true
                spacing: 2
                model: stepsModel

                delegate: Rectangle {
                    id: stepRow

                    required property int index
                    required property string type
                    required property int scanCode
                    required property int waitMs
                    required property string targetAction
                    required property string label

                    width: stepsListView.width
                    height: root.rowHeight
                    radius: Theme.radiusSmall
                    color: dragHandler.active ? Theme.surface2 : "transparent"
                    z: dragHandler.active ? 10 : 1

                    // Only a cosmetic offset moves during a drag - stepRow's
                    // real y stays under ListView's own control throughout,
                    // so stepsModel.move() (which relocates this exact
                    // delegate instance rather than destroying/recreating
                    // it) is what actually reorders the list; this Translate
                    // just tracks the sub-row leftover distance between
                    // swaps so the drag still feels continuous instead of
                    // snapping row-by-row.
                    transform: Translate { id: dragTranslate; y: 0 }

                    Item {
                        id: gripHandle
                        width: 22
                        height: parent.height
                        anchors.left: parent.left

                        Text {
                            anchors.centerIn: parent
                            text: "⠿"
                            color: Theme.overlay0
                            font.pixelSize: 14
                        }

                        DragHandler {
                            id: dragHandler
                            target: null
                            xAxis.enabled: false
                            property real consumedTranslation: 0

                            onActiveChanged: {
                                consumedTranslation = 0;
                                if (!active) {
                                    dragTranslate.y = 0;
                                }
                            }

                            onTranslationChanged: {
                                if (!active) {
                                    return;
                                }
                                const rawOffset = translation.y - consumedTranslation;
                                dragTranslate.y = rawOffset;
                                const shift = Math.round(rawOffset / root.rowHeight);
                                if (shift === 0) {
                                    return;
                                }
                                const from = stepRow.index;
                                const to = Math.max(0, Math.min(stepsModel.count - 1, from + shift));
                                if (to !== from) {
                                    // Reorder guard (Feature): simulate the move first - if
                                    // it would put some Release before its own matching
                                    // Press (see orderKeepsPressBeforeRelease()'s own docs),
                                    // refuse it. The row still visually follows the pointer
                                    // (dragTranslate.y is already set above) - it just won't
                                    // swap past this particular boundary.
                                    const snapshot = [];
                                    for (let i = 0; i < stepsModel.count; i++) {
                                        snapshot.push(stepsModel.get(i));
                                    }
                                    const simulated = snapshot.slice();
                                    const [movedItem] = simulated.splice(from, 1);
                                    simulated.splice(to, 0, movedItem);
                                    if (root.orderKeepsPressBeforeRelease(simulated)) {
                                        stepsModel.move(from, to, 1);
                                        consumedTranslation += (to - from) * root.rowHeight;
                                        dragTranslate.y = translation.y - consumedTranslation;
                                    }
                                }
                            }
                        }
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: gripHandle.width + Theme.spacingXs
                        anchors.rightMargin: Theme.spacingXs
                        spacing: Theme.spacingSm

                        Text {
                            text: stepRow.index + 1 + "."
                            color: Theme.overlay0
                            font.pixelSize: 11
                            Layout.preferredWidth: 18
                        }

                        Text {
                            visible: stepRow.type !== "Wait"
                            text: root.stepDisplayText(stepRow.type, stepRow.label, stepRow.targetAction)
                            color: Theme.text
                            font.pixelSize: 12
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }

                        RowLayout {
                            visible: stepRow.type === "Wait"
                            Layout.fillWidth: true
                            spacing: 4

                            Text { text: qsTr("Delay:"); color: Theme.subtext0; font.pixelSize: 12 }
                            TextField {
                                text: stepRow.waitMs
                                Layout.preferredWidth: 56
                                implicitHeight: 24
                                color: Theme.text
                                font.pixelSize: 12
                                validator: IntValidator { bottom: 0; top: 60000 }
                                background: Rectangle {
                                    color: Theme.surface0
                                    radius: Theme.radiusSmall
                                    border.width: 1
                                    border.color: Qt.rgba(1, 1, 1, 0.08)
                                }
                                onEditingFinished: stepsModel.setProperty(stepRow.index, "waitMs", parseInt(text) || 0)
                            }
                            Text { text: qsTr("ms"); color: Theme.subtext0; font.pixelSize: 12 }
                        }

                        ToolButton {
                            label: qsTr("×")
                            Layout.preferredWidth: 24
                            onClicked: root.deleteStepWithPartner(stepRow.index)
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            spacing: Theme.spacingSm

            ToolButton {
                label: qsTr("+ Wait")
                Layout.fillWidth: true
                onClicked: stepsModel.append({ type: "Wait", scanCode: 0, waitMs: 100, targetAction: "",
                    buttonIndex: 0, targetOutputId: 0, label: "" })
            }
            ToolButton {
                label: qsTr("+ Key")
                Layout.fillWidth: true
                onClicked: insertKeyPopup.open()
            }
            ToolButton {
                label: qsTr("+ Mouse")
                Layout.fillWidth: true
                onClicked: insertMousePopup.open()
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
                enabled: stepsModel.count > 0
                opacity: enabled ? 1.0 : 0.5
                onClicked: {
                    const actionData = {actionType: "MacroHandler", parameters: {steps: root.wireStepsFromModel()}};
                    if (root.macroTargetOutputId !== 0) {
                        actionData.targetOutputId = root.macroTargetOutputId;
                        if (root.macroTargetIsVigem) {
                            actionData.targetDeviceType = "vigem";
                        }
                    }

                    if (root.embeddedMode) {
                        // No physical input to bindAction() against - hand
                        // the JSON back to the Tempo row that opened us.
                        root.close();
                        root.macroReady(JSON.stringify(actionData));
                        return;
                    }

                    const applied = profileEditorViewModel.bindAction(
                        root.devicePath, root.inputName, profileEditorViewModel.currentMode,
                        JSON.stringify(actionData));
                    if (applied) {
                        root.close();
                        root.applied();
                    }
                }
            }
        }
    }

    /// Manual "+ Key" insertion (feature request: "without needing the live
    /// recorder"): a one-shot Keys.onPressed capture using root's own
    /// keyScanCodes lookup table, deliberately NOT macroRecorder - the
    /// recorder's whole reason for existing is capturing REAL press/release
    /// timing for a live performance (see MacroRecorderViewModel's own
    /// docs), which is the opposite of what a single manually-picked key
    /// needs here. Captures one key, then inserts a Press+Release pair (a
    /// "tap") at the end of the sequence - if the caller wants the release
    /// to happen somewhere other than immediately after the press, the
    /// drag-and-drop reordering above moves either half wherever it needs
    /// to go.
    Popup {
        id: insertKeyPopup
        modal: true
        focus: true
        parent: Overlay.overlay
        x: parent ? Math.round((parent.width - width) / 2) : 0
        y: parent ? Math.round((parent.height - height) / 2) : 0
        width: 260
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        property int capturedScanCode: -1
        property string capturedLabel: ""

        onOpened: {
            insertKeyPopup.capturedScanCode = -1;
            insertKeyPopup.capturedLabel = "";
            keyCaptureTarget.forceActiveFocus();
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
                text: qsTr("Insert Keyboard Key")
                color: Theme.text
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 40
                radius: Theme.radiusSmall
                color: Theme.surface1
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.08)

                Text {
                    anchors.centerIn: parent
                    text: insertKeyPopup.capturedLabel !== "" ? insertKeyPopup.capturedLabel : qsTr("Press a key…")
                    color: insertKeyPopup.capturedLabel !== "" ? Theme.text : Theme.overlay0
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }

                Item {
                    id: keyCaptureTarget
                    anchors.fill: parent
                    focus: true
                    Keys.onPressed: (event) => {
                        const code = root.keyScanCodes[event.key];
                        if (code !== undefined) {
                            insertKeyPopup.capturedScanCode = code;
                            insertKeyPopup.capturedLabel = (event.text && event.text.trim().length > 0)
                                ? event.text.toUpperCase()
                                : ("Key 0x" + event.key.toString(16));
                        } else {
                            insertKeyPopup.capturedScanCode = -1;
                            insertKeyPopup.capturedLabel = qsTr("Unsupported key");
                        }
                        event.accepted = true;
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm
                Item { Layout.fillWidth: true }
                ToolButton { label: qsTr("Cancel"); onClicked: insertKeyPopup.close() }
                ToolButton {
                    label: qsTr("Add")
                    enabled: insertKeyPopup.capturedScanCode >= 0
                    opacity: enabled ? 1.0 : 0.5
                    onClicked: {
                        const scanCode = insertKeyPopup.capturedScanCode;
                        const lbl = insertKeyPopup.capturedLabel;
                        stepsModel.append({ type: "PressKey", scanCode: scanCode, waitMs: 0, targetAction: "",
                            buttonIndex: 0, targetOutputId: 0, label: lbl });
                        stepsModel.append({ type: "ReleaseKey", scanCode: scanCode, waitMs: 0, targetAction: "",
                            buttonIndex: 0, targetOutputId: 0, label: lbl });
                        insertKeyPopup.close();
                    }
                }
            }
        }
    }

    /// Manual "+ Mouse" insertion: a plain pick-list, unlike "+ Key" above,
    /// since there are only 5 possible targets (no capture needed - see
    /// Win32MouseInjector::sendMouseButton()/sendMouseScroll()). A click
    /// target inserts a Press+Release pair (a "tap", same convention as "+
    /// Key"); a scroll target inserts a single MouseScroll step - it has no
    /// release counterpart, matching MacroHandler::executeStep()'s own
    /// handling of a real scroll wheel tick.
    Popup {
        id: insertMousePopup
        modal: true
        focus: true
        parent: Overlay.overlay
        x: parent ? Math.round((parent.width - width) / 2) : 0
        y: parent ? Math.round((parent.height - height) / 2) : 0
        width: 220
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: Theme.surface0
            radius: Theme.radiusMedium
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.08)
        }

        contentItem: ColumnLayout {
            spacing: Theme.spacingXs

            Text {
                text: qsTr("Insert Mouse Action")
                color: Theme.text
                font.pixelSize: 14
                font.weight: Font.DemiBold
                Layout.bottomMargin: Theme.spacingXs
            }

            Repeater {
                model: [
                    { label: qsTr("Left Click"), action: "Left", scroll: false },
                    { label: qsTr("Right Click"), action: "Right", scroll: false },
                    { label: qsTr("Middle Click"), action: "Middle", scroll: false },
                    { label: qsTr("Scroll Up"), action: "ScrollUp", scroll: true },
                    { label: qsTr("Scroll Down"), action: "ScrollDown", scroll: true },
                ]
                delegate: ToolButton {
                    required property var modelData
                    Layout.fillWidth: true
                    label: modelData.label
                    onClicked: {
                        if (modelData.scroll) {
                            stepsModel.append({ type: "MouseScroll", scanCode: 0, waitMs: 0,
                                targetAction: modelData.action, buttonIndex: 0, targetOutputId: 0,
                                label: modelData.action });
                        } else {
                            stepsModel.append({ type: "PressMouseButton", scanCode: 0, waitMs: 0,
                                targetAction: modelData.action, buttonIndex: 0, targetOutputId: 0,
                                label: modelData.action });
                            stepsModel.append({ type: "ReleaseMouseButton", scanCode: 0, waitMs: 0,
                                targetAction: modelData.action, buttonIndex: 0, targetOutputId: 0,
                                label: modelData.action });
                        }
                        insertMousePopup.close();
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ToolButton { label: qsTr("Cancel"); onClicked: insertMousePopup.close() }
            }
        }
    }
}
