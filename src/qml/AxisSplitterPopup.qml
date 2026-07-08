import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GremblingNexus

// Axis Splitter (Fase 10.9): configures an AxisSplitterHandler - one or
// more independent [min%, max%] zones on a physical axis' travel, each
// firing its own vJoy button on entry/exit (e.g. the last 10% of a
// throttle triggering the afterburner detent). Opened from
// ActionPickerPopup's "Axis-to-Button" tab (axis-kind inputs only - see
// that file). Self-contained like MacroEditorPopup: calls
// profileEditorViewModel.bindAction() and closes itself directly.
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

    /// Draft zone list - plain JS objects {min, max, targetButton}, min/max
    /// as fractions [0, 1] of the axis' full travel (matching
    /// AxisSplitterHandler's own Zone::minFraction/maxFraction).
    property var zones: []

    signal applied()

    function openFor(devicePath_, inputName_) {
        root.devicePath = devicePath_;
        root.inputName = inputName_;
        root.zones = [{min: 0.9, max: 1.0, targetButton: 0}];
        vjoyDeviceCombo.setFromTarget(null);
        root.open();
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

            Text { text: qsTr("Axis-to-Button Zones"); color: Theme.text; font.pixelSize: 17; font.weight: Font.DemiBold }
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

            Text { text: qsTr("Target Output Device"); color: Theme.subtext0; font.pixelSize: 11 }
            OutputDeviceCombo { id: vjoyDeviceCombo }
        }

        // --- Visual travel bar: 0% (left) to 100% (right), each zone drawn
        // as a colored segment at its own [min, max] position. -----------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            spacing: 2

            Text { text: qsTr("Travel"); color: Theme.subtext0; font.pixelSize: 11 }

            Rectangle {
                id: track
                Layout.fillWidth: true
                implicitHeight: 28
                radius: Theme.radiusSmall
                color: Theme.surface1
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.08)
                clip: true

                Repeater {
                    model: root.zones
                    delegate: Rectangle {
                        x: modelData.min * track.width
                        width: Math.max(2, (modelData.max - modelData.min) * track.width)
                        height: track.height
                        color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.55)
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Text { text: qsTr("0%"); color: Theme.overlay0; font.pixelSize: 10 }
                Item { Layout.fillWidth: true }
                Text { text: qsTr("100%"); color: Theme.overlay0; font.pixelSize: 10 }
            }
        }

        // --- Zone list -----------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            spacing: Theme.spacingXs

            Repeater {
                model: root.zones
                delegate: RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingXs

                    Text { text: qsTr("Min %"); color: Theme.subtext0; font.pixelSize: 11 }
                    TextField {
                        text: (modelData.min * 100).toFixed(0)
                        Layout.preferredWidth: 48
                        implicitHeight: 28
                        color: Theme.text
                        font.pixelSize: 12
                        validator: DoubleValidator { bottom: 0; top: 100 }
                        background: Rectangle {
                            color: Theme.surface1; radius: Theme.radiusSmall
                            border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.08)
                        }
                        onEditingFinished: {
                            const arr = root.zones.slice();
                            const v = Math.max(0, Math.min(100, parseFloat(text) || 0)) / 100.0;
                            arr[index] = Object.assign({}, arr[index], {min: v});
                            root.zones = arr;
                        }
                    }

                    Text { text: qsTr("Max %"); color: Theme.subtext0; font.pixelSize: 11 }
                    TextField {
                        text: (modelData.max * 100).toFixed(0)
                        Layout.preferredWidth: 48
                        implicitHeight: 28
                        color: Theme.text
                        font.pixelSize: 12
                        validator: DoubleValidator { bottom: 0; top: 100 }
                        background: Rectangle {
                            color: Theme.surface1; radius: Theme.radiusSmall
                            border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.08)
                        }
                        onEditingFinished: {
                            const arr = root.zones.slice();
                            const v = Math.max(0, Math.min(100, parseFloat(text) || 0)) / 100.0;
                            arr[index] = Object.assign({}, arr[index], {max: v});
                            root.zones = arr;
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
                            const arr = root.zones.slice();
                            arr[index] = Object.assign({}, arr[index], {targetButton: parseInt(text) || 0});
                            root.zones = arr;
                        }
                    }

                    Item { Layout.fillWidth: true }

                    ToolButton {
                        label: qsTr("×")
                        Layout.preferredWidth: 24
                        onClicked: {
                            const arr = root.zones.slice();
                            arr.splice(index, 1);
                            root.zones = arr;
                        }
                    }
                }
            }

            ToolButton {
                label: qsTr("+ Add Zone")
                Layout.alignment: Qt.AlignLeft
                onClicked: {
                    const arr = root.zones.slice();
                    arr.push({min: 0.0, max: 0.1, targetButton: 0});
                    root.zones = arr;
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
                enabled: root.zones.length > 0
                opacity: enabled ? 1.0 : 0.5
                onClicked: {
                    const jsonZones = root.zones.map((z) => {
                        return {min: z.min, max: z.max, targetButton: z.targetButton};
                    });
                    const actionData = {
                        actionType: "AxisSplitterHandler",
                        targetDeviceType: vjoyDeviceCombo.targetDeviceType,
                        targetOutputId: vjoyDeviceCombo.targetOutputId,
                        parameters: {zones: jsonZones}
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
