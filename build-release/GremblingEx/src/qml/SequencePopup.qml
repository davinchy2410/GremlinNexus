import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Effects
import GremblingEx

// Sequence / Rotary (Fase 13): configures a SequenceHandler - an ordered
// list of vJoy button remaps that rotate on each physical press (press 1
// fires the first remap, press 2 the second, ..., wrapping back to the
// first after the last). Opened from ActionPickerPopup's "Sequence / Rotary"
// tab (button-kind inputs only). Self-contained like AxisSplitterPopup:
// calls profileEditorViewModel.bindAction() and closes itself directly.
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

    /// Draft step list - plain JS objects {targetOutputId, targetButton}.
    property var steps: []

    readonly property var vjoyDeviceNames: Array.from({length: 16}, (_, i) => "vJoy Device " + (i + 1))

    signal applied()

    function openFor(devicePath_, inputName_, existingSteps) {
        root.devicePath = devicePath_;
        root.inputName = inputName_;
        // Fase 20.21: restore whatever steps ActionPickerPopup already read
        // back from this input's existing binding, instead of always
        // overwriting them with these two hardcoded defaults - previously
        // every re-open of an already-configured sequence silently reset it.
        if (existingSteps && existingSteps.length > 0) {
            root.steps = existingSteps;
        } else {
            root.steps = [{targetOutputId: 1, targetButton: 0}, {targetOutputId: 1, targetButton: 1}];
        }
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

            Text { text: "Sequence / Rotary"; color: Theme.text; font.pixelSize: 17; font.weight: Font.DemiBold }
            Text {
                text: root.inputName + " · mode: " + profileEditorViewModel.currentMode
                color: Theme.subtext0
                font.pixelSize: 12
            }
            Text {
                text: "Each press fires the next remap below, then wraps back to the first."
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
                delegate: RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingXs

                    Text { text: (index + 1) + "."; color: Theme.subtext0; font.pixelSize: 11 }

                    Text { text: "vJoy Device"; color: Theme.subtext0; font.pixelSize: 11 }
                    AppComboBox {
                        Layout.preferredWidth: 130
                        model: root.vjoyDeviceNames
                        currentIndex: modelData.targetOutputId - 1
                        onActivated: (idx) => {
                            const arr = root.steps.slice();
                            arr[index] = Object.assign({}, arr[index], {targetOutputId: idx + 1});
                            root.steps = arr;
                        }
                    }

                    Text { text: "Button"; color: Theme.subtext0; font.pixelSize: 11 }
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

                    Item { Layout.fillWidth: true }

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

            ToolButton {
                label: "+ Add Action"
                Layout.alignment: Qt.AlignLeft
                onClicked: {
                    const arr = root.steps.slice();
                    arr.push({targetOutputId: 1, targetButton: 0});
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
                label: "Cancel"
                onClicked: root.close()
            }
            ToolButton {
                label: "Apply"
                enabled: root.steps.length >= 2
                opacity: enabled ? 1.0 : 0.5
                onClicked: {
                    const jsonActions = root.steps.map((s) => {
                        return { actionType: "ButtonRemapHandler", targetOutputId: s.targetOutputId,
                                 targetButton: s.targetButton };
                    });
                    const actionData = {
                        actionType: "SequenceHandler",
                        parameters: { actions: jsonActions }
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
}
