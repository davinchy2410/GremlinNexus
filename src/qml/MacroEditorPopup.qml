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
// delete steps, then Apply.
//
// Two ways this gets used (Sprint 6 added the second):
//  - Standalone (openFor(), embeddedMode false): Apply calls
//    profileEditorViewModel.bindAction() directly and closes itself - the
//    original Fase 10.9 flow, still what ActionPickerPopup's top-level
//    "Macro" tab uses.
//  - Embedded (openEmbedded(), embeddedMode true): there is no single
//    physical input to bind here - this popup is being used to record/edit
//    one step inside a TempoHandler's own shortActions/longActions cascade
//    (see ActionPickerPopup's "Macro" category on a Tempo row). Apply
//    instead emits macroReady(json) with the built actionType JSON and
//    closes; the opener (a Tempo row) is responsible for storing it.
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

    /// Draft step list - plain JS objects {type, scanCode, waitMs, label},
    /// same shape macroRecorder.stepRecorded() reports and what
    /// buildActionData() below turns into the final actionType JSON.
    property var steps: []

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

    function openFor(devicePath_, inputName_) {
        root.devicePath = devicePath_;
        root.inputName = inputName_;
        root.embeddedMode = false;
        root.steps = [];
        macroRecorder.stopRecording();
        root.open();
    }

    /// Sprint 6: opened from a "Macro" category row inside ActionPickerPopup's
    /// Tempo (Short/Long Press) cascade editor instead of the top-level
    /// "Macro" action tab. existingSteps is that row's own previously-saved
    /// macroSteps (the same {type, scanCode}/{type:"Wait", waitMs} shape
    /// MacroHandler::toJson() writes - see extractTempoActions() - not the
    /// live-recording {type, scanCode, waitMs, label} shape), so re-opening
    /// an already-recorded row shows what's there instead of starting blank;
    /// a missing "label" (every step restored this way has none - only a
    /// step just recorded live carries one) is synthesized from scanCode the
    /// same way ActionPickerPopup's own KeyboardHandler restore does for a
    /// plain top-level Keyboard binding.
    function openEmbedded(existingSteps) {
        root.devicePath = "";
        root.inputName = qsTr("Tempo cascade step");
        root.embeddedMode = true;
        root.steps = (existingSteps || []).map((s) => {
            if (s.type === "Wait") {
                return { type: "Wait", scanCode: 0, waitMs: s.waitMs || 0, label: "" };
            }
            return { type: s.type, scanCode: s.scanCode, waitMs: 0,
                label: "Key 0x" + (s.scanCode || 0).toString(16) };
        });
        macroRecorder.stopRecording();
        root.open();
    }

    onClosed: macroRecorder.stopRecording()

    Connections {
        target: macroRecorder
        function onStepRecorded(kind, scanCode, waitMs, label) {
            const arr = root.steps.slice();
            arr.push({type: kind, scanCode: scanCode, waitMs: waitMs, label: label});
            root.steps = arr;
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
                text: macroRecorder.recording
                    ? qsTr("Recording… press and release keys now")
                    : qsTr("Click Record, then press the keys for this macro")
                color: macroRecorder.recording ? Theme.success : Theme.subtext0
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
                visible: root.steps.length === 0
                text: qsTr("No steps recorded yet")
                color: Theme.overlay0
                font.pixelSize: 12
                font.italic: true
            }

            ListView {
                anchors.fill: parent
                anchors.margins: Theme.spacingXs
                clip: true
                spacing: 2
                model: root.steps

                delegate: Rectangle {
                    width: ListView.view.width
                    height: 30
                    radius: Theme.radiusSmall
                    color: "transparent"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacingSm
                        anchors.rightMargin: Theme.spacingXs
                        spacing: Theme.spacingSm

                        Text {
                            text: index + 1 + "."
                            color: Theme.overlay0
                            font.pixelSize: 11
                            Layout.preferredWidth: 18
                        }

                        Text {
                            visible: modelData.type !== "Wait"
                            text: (modelData.type === "PressKey" ? qsTr("Key Down: ") : qsTr("Key Up: ")) + modelData.label
                            color: Theme.text
                            font.pixelSize: 12
                            Layout.fillWidth: true
                        }

                        RowLayout {
                            visible: modelData.type === "Wait"
                            Layout.fillWidth: true
                            spacing: 4

                            Text { text: qsTr("Delay:"); color: Theme.subtext0; font.pixelSize: 12 }
                            TextField {
                                text: modelData.waitMs
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
                                onEditingFinished: {
                                    const arr = root.steps.slice();
                                    arr[index] = Object.assign({}, arr[index], {waitMs: parseInt(text) || 0});
                                    root.steps = arr;
                                }
                            }
                            Text { text: qsTr("ms"); color: Theme.subtext0; font.pixelSize: 12 }
                        }

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
                enabled: root.steps.length > 0
                opacity: enabled ? 1.0 : 0.5
                onClicked: {
                    const jsonSteps = root.steps.map((s) => {
                        return s.type === "Wait"
                            ? {type: "Wait", waitMs: s.waitMs}
                            : {type: s.type, scanCode: s.scanCode};
                    });
                    const actionData = {actionType: "MacroHandler", parameters: {steps: jsonSteps}};

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
}
