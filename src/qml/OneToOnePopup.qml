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
        root.open();
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
                            Text {
                                text: root.targetLabel(modelData.name, modelData.kind)
                                color: Theme.accent
                                font.pixelSize: 12
                                Layout.fillWidth: true
                                elide: Text.ElideRight
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
                    if (profileEditorViewModel.create1to1Mapping(root.devicePath, targetCombo.targetOutputId)) {
                        root.close();
                    }
                }
            }
        }
    }
}
