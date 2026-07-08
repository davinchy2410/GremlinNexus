import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Effects
import QtQuick.Dialogs
import GremblingNexus

// Sequence / Rotary (Fase 13, action-diversity overhaul): configures a
// SequenceHandler - an ordered list of actions of ANY kind that rotate on
// each physical press (press 1 fires the first action, press 2 the second,
// ..., wrapping back to the first after the last - see
// SequenceHandler::processButton()'s own docs; note it only ever forwards
// to an action's processButton(), never processAxis(), which is why "vJoy
// Axis" is deliberately NOT one of the categories below - an axis-only
// handler's processButton() is a documented no-op, so it would silently do
// nothing when its turn came up).
//
// Each row's own "Category" combo picks which one of seven action JSON
// shapes it becomes - the same modular-row pattern ActionPickerPopup's own
// Tempo (Short/Long Press) cascade already uses (borrowed directly: same
// "every row's JS object carries every category's fields at once" trick,
// same Qt.callLater fix for the Mode Switch combo's binding-loop hazard).
// Every row is a plain JS object with ALL of the following fields present
// regardless of its own category (unused ones just sit unread) so switching
// a row's category never loses whatever was already entered in the others:
//   category, targetDeviceType, targetOutputId, targetButton (vJoy Button),
//   scanCode, keyLabel (Keyboard Key), mouseAction (Mouse Action),
//   macroSteps (Macro), targetMode (Mode Switch), filePath (Play Audio),
//   text (Text-to-Speech).
//
// Opened from ActionPickerPopup's "Sequence / Rotary" tab (button-kind
// inputs only). Self-contained like AxisSplitterPopup/MacroEditorPopup:
// calls profileEditorViewModel.bindAction() and closes itself directly.
// openFor() now reads and restores whatever SequenceHandler is already
// bound at (devicePath, inputName) itself, via getActionDataJson() - the
// same self-contained restore path MacroEditorPopup.openFor() uses - so it
// no longer needs the caller to have pre-parsed anything; there is no
// openEmbedded() (unlike MacroEditorPopup), since SequenceHandler is never
// nested inside another cascade anywhere in this codebase's own schema.
Popup {
    id: root

    modal: true
    focus: true
    parent: Overlay.overlay // Fase 20.15: escape the opening popup's own coordinate system
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    width: 440
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    property string devicePath: ""
    property string inputName: ""

    /// Draft step list - plain JS objects, see the file-level comment above
    /// for the full field shape every row always carries.
    property var steps: []

    signal applied()

    /// Qt::Key -> PS/2 Set 1 hardware scan code (see KeyboardHandler /
    /// IKeyboardBackend - a *hardware* scan code, not a virtual-key code).
    /// Copied from ActionPickerPopup.qml's own table - this popup is
    /// self-contained, like every other popup in this codebase (see the
    /// file-level comment above). Backs the "Keyboard Key" category's
    /// inline one-shot Keys.onPressed capture below.
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

    /// A freshly-added row's default shape - "vJoy Button" targeting device
    /// 1, button targetButton, every other category's fields at a sane
    /// empty default so switching this row's category later never reads
    /// undefined.
    function defaultStep(targetButton) {
        return { category: "device", targetDeviceType: "vjoy", targetOutputId: 1, targetButton: targetButton,
            scanCode: 0, keyLabel: "", mouseAction: "Left", macroSteps: [], targetMode: "", filePath: "", text: "" };
    }

    /// Converts one already-instantiated action's JSON (whatever
    /// IActionHandler::toJson() produced for it - the same
    /// "parameters.actions[i]" entries ProfileManager::instantiateSequenceHandler()
    /// consumes) back into one row's draft shape. Falls back to the "device"
    /// (ButtonRemapHandler) category for any actionType this popup doesn't
    /// know how to edit - notably every sequence saved before this overhaul,
    /// which are always ButtonRemapHandler entries - so an old profile still
    /// restores exactly as it used to.
    function stepFromWireAction(action) {
        const type = action.actionType;
        const params = action.parameters || {};
        const base = defaultStep(0);
        if (type === "KeyboardHandler") {
            const scanCode = params.scanCode || 0;
            return Object.assign({}, base, { category: "keyboard", scanCode: scanCode,
                keyLabel: "Key 0x" + scanCode.toString(16) });
        }
        if (type === "MouseButton") {
            return Object.assign({}, base, { category: "mouse", mouseAction: params.targetAction || "Left" });
        }
        if (type === "MacroHandler") {
            return Object.assign({}, base, { category: "macro", macroSteps: params.steps || [] });
        }
        if (type === "ModeSwitch") {
            return Object.assign({}, base, { category: "modeSwitch", targetMode: params.targetMode || "" });
        }
        if (type === "AudioAction") {
            return Object.assign({}, base, { category: "audio", filePath: params.filePath || "" });
        }
        if (type === "TTSAction") {
            return Object.assign({}, base, { category: "tts", text: params.text || "" });
        }
        // ButtonRemapHandler, or anything unrecognized - "device" category.
        return Object.assign({}, base, { category: "device",
            targetDeviceType: action.targetDeviceType || "vjoy",
            targetOutputId: action.targetOutputId || 1,
            targetButton: action.targetButton || 0 });
    }

    /// The inverse of stepFromWireAction() - one row's draft shape back into
    /// the actionType/parameters JSON ProfileManager's generic
    /// instantiateHandler() dispatch expects, exactly like
    /// ActionPickerPopup's own toCascadeAction() does for a Tempo row.
    function stepToWireAction(s) {
        const category = s.category || "device";
        if (category === "keyboard") {
            return { actionType: "KeyboardHandler", parameters: { scanCode: s.scanCode || 0 } };
        }
        if (category === "mouse") {
            return { actionType: "MouseButton", parameters: { targetAction: s.mouseAction || "Left" } };
        }
        if (category === "macro") {
            return { actionType: "MacroHandler", parameters: { steps: s.macroSteps || [] } };
        }
        if (category === "modeSwitch") {
            return { actionType: "ModeSwitch", parameters: { targetMode: s.targetMode || "" } };
        }
        if (category === "audio") {
            return { actionType: "AudioAction", parameters: { filePath: (s.filePath || "").trim() } };
        }
        if (category === "tts") {
            return { actionType: "TTSAction", parameters: { text: (s.text || "").trim() } };
        }
        return { actionType: "ButtonRemapHandler", targetDeviceType: s.targetDeviceType || "vjoy",
            targetOutputId: s.targetOutputId || 1, targetButton: s.targetButton || 0 };
    }

    /// Resets and opens the popup for (devicePath_, inputName_) - restores
    /// whatever SequenceHandler is already bound there via
    /// getActionDataJson() (Fase 20.17's read-side call, the same one
    /// ActionPickerPopup.qml's own openFor() and MacroEditorPopup.qml's own
    /// openFor() already use), instead of relying on the caller to have
    /// pre-parsed anything - a plain, ignored extra argument from an older
    /// call site is harmless in QML/JS, so this stays a drop-in replacement.
    function openFor(devicePath_, inputName_) {
        root.devicePath = devicePath_;
        root.inputName = inputName_;

        let restored = [];
        const existingJsonStr = profileEditorViewModel.getActionDataJson(
            devicePath_, inputName_, profileEditorViewModel.currentMode);
        if (existingJsonStr !== "") {
            try {
                const actionData = JSON.parse(existingJsonStr);
                if (actionData.actionType === "SequenceHandler" && actionData.parameters
                        && actionData.parameters.actions) {
                    restored = actionData.parameters.actions.map(stepFromWireAction);
                }
            } catch (e) {
                restored = []; // Malformed JSON - same tolerant fallback as every other restore path.
            }
        }
        root.steps = restored.length > 0 ? restored : [defaultStep(0), defaultStep(1)];
        root.open();
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

            Text { text: qsTr("Sequence / Rotary"); color: Theme.text; font.pixelSize: 17; font.weight: Font.DemiBold }
            Text {
                text: root.inputName + " · mode: " + profileEditorViewModel.currentMode
                color: Theme.subtext0
                font.pixelSize: 12
            }
            Text {
                text: qsTr("Each press fires the next action below, then wraps back to the first.")
                color: Theme.overlay0
                font.pixelSize: 11
                font.italic: true
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }

        // --- Step list -------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            spacing: Theme.spacingXs

            Repeater {
                model: root.steps
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
                            id: stepCategoryCombo
                            Layout.preferredWidth: 150
                            model: [qsTr("vJoy Button"), qsTr("Keyboard Key"), qsTr("Mouse Action"), qsTr("Macro"),
                                    qsTr("Mode Switch"), qsTr("Play Audio"), qsTr("Text-to-Speech")]
                            currentIndex: ["device", "keyboard", "mouse", "macro", "modeSwitch", "audio", "tts"]
                                .indexOf(modelData.category || "device")
                            // Fase (bugfix, carried over from Tempo's own cascade -
                            // see ActionPickerPopup.qml's tempoShortCategoryCombo):
                            // ComboBox's own activated(int index) signal parameter
                            // shadows this delegate's Repeater-provided "index"
                            // context property unless explicitly named otherwise.
                            onActivated: (categoryIndex) => {
                                const categories = ["device", "keyboard", "mouse", "macro", "modeSwitch", "audio", "tts"];
                                const arr = root.steps.slice();
                                arr[index] = Object.assign({}, arr[index], {category: categories[categoryIndex]});
                                root.steps = arr;
                            }
                        }

                        Item { Layout.fillWidth: true }

                        ToolButton {
                            label: qsTr("×")
                            Layout.preferredWidth: 24
                            onClicked: {
                                const arr = root.steps.slice();
                                arr.splice(index, 1);
                                root.steps = arr;
                            }
                        }
                    }

                    // --- vJoy Button fields ---
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.spacingLg
                        spacing: Theme.spacingXs
                        visible: (modelData.category || "device") === "device"

                        OutputDeviceCombo {
                            id: stepDeviceCombo
                            Component.onCompleted: stepDeviceCombo.setFromTarget(modelData)
                            onChanged: {
                                const arr = root.steps.slice();
                                arr[index] = Object.assign({}, arr[index], {
                                    targetDeviceType: stepDeviceCombo.targetDeviceType,
                                    targetOutputId: stepDeviceCombo.targetOutputId
                                });
                                root.steps = arr;
                            }
                        }

                        Text { text: qsTr("Button"); color: Theme.subtext0; font.pixelSize: 11 }
                        TextField {
                            text: modelData.targetButton
                            Layout.preferredWidth: 40
                            implicitHeight: 28
                            color: Theme.text
                            font.pixelSize: 12
                            validator: IntValidator { bottom: 0; top: 127 }
                            background: Rectangle {
                                color: Theme.surface1; radius: Theme.radiusSmall
                                border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.08)
                            }
                            onEditingFinished: {
                                const arr = root.steps.slice();
                                arr[index] = Object.assign({}, arr[index], {targetButton: parseInt(text) || 0});
                                root.steps = arr;
                            }
                        }
                    }

                    // --- Keyboard Key fields ---
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.spacingLg
                        spacing: Theme.spacingXs
                        visible: (modelData.category || "device") === "keyboard"

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 28
                            radius: Theme.radiusSmall
                            color: stepKeyCaptureItem.activeFocus ? Theme.accent : Theme.surface1
                            border.width: 1
                            border.color: Qt.rgba(1, 1, 1, 0.08)
                            Behavior on color { ColorAnimation { duration: Theme.animFast } }

                            Text {
                                anchors.centerIn: parent
                                text: stepKeyCaptureItem.activeFocus
                                    ? qsTr("Press a key…")
                                    : (modelData.keyLabel ? modelData.keyLabel : qsTr("Click to set key"))
                                color: stepKeyCaptureItem.activeFocus ? Theme.crust : Theme.text
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                            }

                            Item {
                                id: stepKeyCaptureItem
                                anchors.fill: parent

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: stepKeyCaptureItem.forceActiveFocus()
                                }

                                Keys.onPressed: (event) => {
                                    const arr = root.steps.slice();
                                    const code = root.keyScanCodes[event.key];
                                    if (code !== undefined) {
                                        const lbl = (event.text && event.text.trim().length > 0)
                                            ? event.text.toUpperCase()
                                            : ("Key 0x" + event.key.toString(16));
                                        arr[index] = Object.assign({}, arr[index], {scanCode: code, keyLabel: lbl});
                                    } else {
                                        arr[index] = Object.assign({}, arr[index],
                                            {scanCode: 0, keyLabel: qsTr("Unsupported key")});
                                    }
                                    root.steps = arr;
                                    event.accepted = true;
                                }
                            }
                        }
                    }

                    // --- Mouse Action fields ---
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.spacingLg
                        spacing: Theme.spacingXs
                        visible: (modelData.category || "device") === "mouse"

                        Text { text: qsTr("Action"); color: Theme.subtext0; font.pixelSize: 11 }
                        AppComboBox {
                            id: stepMouseActionCombo
                            Layout.fillWidth: true
                            model: [qsTr("Left Click"), qsTr("Right Click"), qsTr("Middle Click"),
                                    qsTr("Scroll Up"), qsTr("Scroll Down")]
                            currentIndex: Math.max(0, ["Left", "Right", "Middle", "ScrollUp", "ScrollDown"]
                                .indexOf(modelData.mouseAction || "Left"))
                            onActivated: (comboIndex) => {
                                const actions = ["Left", "Right", "Middle", "ScrollUp", "ScrollDown"];
                                const arr = root.steps.slice();
                                arr[index] = Object.assign({}, arr[index], {mouseAction: actions[comboIndex]});
                                root.steps = arr;
                            }
                        }
                    }

                    // --- Macro fields ---
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
                                stepMacroEditorPopup.targetIndex = index;
                                stepMacroEditorPopup.openEmbedded(modelData.macroSteps || []);
                            }
                        }
                    }

                    // --- Mode Switch fields ---
                    // Same "ModeSwitch"/"parameters.targetMode" JSON shape the
                    // top-level "Mode Toggle (Sticky)" action type builds -
                    // reusing that actionType nested inside a rotation needs
                    // no C++ changes at all, same reasoning as Tempo's own
                    // Mode Switch category (see ActionPickerPopup.qml).
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.spacingLg
                        spacing: Theme.spacingXs
                        visible: (modelData.category || "device") === "modeSwitch"

                        Text { text: qsTr("Switch to Mode"); color: Theme.subtext0; font.pixelSize: 11 }
                        AppComboBox {
                            id: stepModeCombo
                            Layout.fillWidth: true
                            model: profileEditorViewModel.modes
                            onActivated: (comboIndex) => {
                                const arr = root.steps.slice();
                                arr[index] = Object.assign({}, arr[index], {targetMode: stepModeCombo.textAt(comboIndex)});
                                root.steps = arr;
                            }
                            // Bugfix (binding loop) - identical fix to Tempo's own
                            // tempoShortModeCombo: writing root.steps straight from
                            // Component.onCompleted (even for just this one row)
                            // reassigns the very array this Repeater is still
                            // mid-instantiation from, tripping Qt's binding-loop
                            // detector. Qt.callLater defers the write to the next
                            // event-loop tick, after this instantiation pass
                            // finishes.
                            Component.onCompleted: {
                                const modeIdx = profileEditorViewModel.modes.indexOf(modelData.targetMode || "");
                                stepModeCombo.currentIndex = modeIdx >= 0 ? modeIdx : 0;
                                if (modelData.category === "modeSwitch" && modeIdx < 0
                                        && profileEditorViewModel.modes.length > 0) {
                                    const rowIndex = index;
                                    const fallbackMode = stepModeCombo.textAt(0);
                                    Qt.callLater(() => {
                                        const arr = root.steps.slice();
                                        if (rowIndex < 0 || rowIndex >= arr.length) {
                                            return;
                                        }
                                        arr[rowIndex] = Object.assign({}, arr[rowIndex], {targetMode: fallbackMode});
                                        root.steps = arr;
                                    });
                                }
                            }
                        }
                    }

                    // --- Play Audio fields ---
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
                                const arr = root.steps.slice();
                                arr[index] = Object.assign({}, arr[index], {filePath: text});
                                root.steps = arr;
                            }
                        }
                        ToolButton {
                            label: qsTr("Browse…")
                            onClicked: {
                                stepAudioDialog.targetIndex = index;
                                stepAudioDialog.open();
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
                                const arr = root.steps.slice();
                                arr[index] = Object.assign({}, arr[index], {text: text});
                                root.steps = arr;
                            }
                        }
                    }
                }
            }

            ToolButton {
                label: qsTr("+ Add Action")
                Layout.alignment: Qt.AlignLeft
                onClicked: {
                    const arr = root.steps.slice();
                    arr.push(root.defaultStep(0));
                    root.steps = arr;
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
                enabled: root.steps.length >= 2
                opacity: enabled ? 1.0 : 0.5
                onClicked: {
                    const actionData = {
                        actionType: "SequenceHandler",
                        parameters: { actions: root.steps.map(root.stepToWireAction) }
                    };

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

    /// A SEPARATE MacroEditorPopup instance from the top-level "Macro"
    /// action tab's own macroEditorPopup - this one is always opened
    /// embedded (openEmbedded()/macroReady()), never bindAction()s or
    /// closes this whole SequencePopup itself. targetIndex (set right
    /// before openEmbedded()) says which row gets the recorded steps
    /// written back to it - same pattern as ActionPickerPopup's own
    /// tempoMacroEditorPopup.
    MacroEditorPopup {
        id: stepMacroEditorPopup
        property int targetIndex: -1
        onMacroReady: (actionDataJson) => {
            if (targetIndex < 0) {
                return;
            }
            const stepsJson = JSON.parse(actionDataJson).parameters.steps;
            const arr = root.steps.slice();
            arr[targetIndex] = Object.assign({}, arr[targetIndex], {macroSteps: stepsJson});
            root.steps = arr;
        }
    }

    /// targetIndex (set right before .open()) says which row gets the
    /// picked path written back to it - same pattern as ActionPickerPopup's
    /// own tempoAudioDialog.
    FileDialog {
        id: stepAudioDialog
        property int targetIndex: -1
        title: qsTr("Select Audio File")
        nameFilters: [qsTr("Audio Files (*.wav *.mp3)")]
        fileMode: FileDialog.OpenFile
        onAccepted: {
            if (targetIndex < 0) {
                return;
            }
            const path = selectedFile.toString().replace("file:///", "");
            const arr = root.steps.slice();
            arr[targetIndex] = Object.assign({}, arr[targetIndex], {filePath: path});
            root.steps = arr;
        }
    }
}
