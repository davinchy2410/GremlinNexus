import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Effects
import GremblingEx

// Macro Editor (Fase 10.9): the "real" editor superseding the Action
// Picker's Fase 10.8 quick vJoy timed-press shorthand (still available
// separately - see ActionPickerPopup's Macro tab). Record live keyboard
// input via the C++ macroRecorder (MacroRecorderViewModel installs an
// application-wide event filter while recording - see its class docs for
// why a plain QML Keys.onPressed isn't enough here), edit delays inline,
// delete steps, then Apply to bind it via
// profileEditorViewModel.bindAction(). Self-contained: opened by
// ActionPickerPopup, but calls bindAction() and closes itself directly
// rather than handing JSON back to its opener.
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

    function openFor(devicePath_, inputName_) {
        root.devicePath = devicePath_;
        root.inputName = inputName_;
        root.steps = [];
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

            Text { text: "Macro Editor"; color: Theme.text; font.pixelSize: 17; font.weight: Font.DemiBold }
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
                        text: macroRecorder.recording ? "Stop" : "Record"
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
                    ? "Recording… press and release keys now"
                    : "Click Record, then press the keys for this macro"
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
                text: "No steps recorded yet"
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
                            text: (modelData.type === "PressKey" ? "Key Down: " : "Key Up: ") + modelData.label
                            color: Theme.text
                            font.pixelSize: 12
                            Layout.fillWidth: true
                        }

                        RowLayout {
                            visible: modelData.type === "Wait"
                            Layout.fillWidth: true
                            spacing: 4

                            Text { text: "Delay:"; color: Theme.subtext0; font.pixelSize: 12 }
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
                            Text { text: "ms"; color: Theme.subtext0; font.pixelSize: 12 }
                        }

                        ToolButton {
                            label: "×"
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
                label: "Cancel"
                onClicked: root.close()
            }
            ToolButton {
                label: "Apply"
                enabled: root.steps.length > 0
                opacity: enabled ? 1.0 : 0.5
                onClicked: {
                    const jsonSteps = root.steps.map((s) => {
                        return s.type === "Wait"
                            ? {type: "Wait", waitMs: s.waitMs}
                            : {type: s.type, scanCode: s.scanCode};
                    });
                    const actionData = {actionType: "MacroHandler", parameters: {steps: jsonSteps}};

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
