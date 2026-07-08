import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GremblingNexus

// vJoy Auditor: read-only diagnostic overlay showing which of one vJoy
// device's 8 axes and 128 buttons are already targeted by a live binding in
// the current profile, so the user can tell at a glance which slots are
// still free before wiring up a new one - self-contained the same way
// ModeManagerPopup/CalibrationWizardPopup are: ProfileEditorView.qml just
// instantiates this once and calls open().
//
// Occupancy is read straight from ProfileEditorViewModel::vjoyOccupancy(),
// which walks EventRouter's *live* routing table on every call (see that
// method's own C++ docs) - not a cached snapshot - so refresh() always
// reflects whatever's bound as of the moment it's called, including a bind
// added earlier this same session, without this popup needing its own
// change-notification wiring.
Popup {
    id: root

    modal: true
    focus: true
    parent: Overlay.overlay
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    width: 760
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    /// vJoy's own fixed axis order (see VJoyDevice::setAxis()/
    /// ProfileManager::VJoyOccupancy's own docs) - index-aligned with the
    /// "axes" QVariantList vjoyOccupancy() returns.
    readonly property var axisNames: ["X", "Y", "Z", "Rx", "Ry", "Rz", "Slider", "Dial"]

    /// Which vJoy device (1-16, vJoy's own valid id range) this popup is
    /// currently auditing.
    property int targetOutputId: 1

    /// Last snapshot from ProfileEditorViewModel::vjoyOccupancy() - {
    /// "axes": [bool x8], "buttons": [bool x128] }. Empty arrays (both
    /// sections render as if nothing were bound) until refresh() first runs.
    property var occupancy: ({ axes: [], buttons: [] })

    /// Capped the same way ModeManagerPopup's own button-list ScrollView is
    /// (see Memory.md - a ScrollView inside a Popup doesn't reliably shrink
    /// to its content otherwise): the Popup's own height stays bounded by
    /// the overlay even with all 128 buttons and 16 possible devices.
    readonly property int maxListHeight: Overlay.overlay
        ? Math.max(220, Math.round(Overlay.overlay.height * 0.5))
        : 360

    function refresh() {
        root.occupancy = profileEditorViewModel.vjoyOccupancy(root.targetOutputId);
    }

    function isAxisOccupied(index) {
        return !!(root.occupancy.axes && root.occupancy.axes[index]);
    }

    function isButtonOccupied(index) {
        return !!(root.occupancy.buttons && root.occupancy.buttons[index]);
    }

    onOpened: root.refresh()

    // Rebinding new profiles/bindings only ever happens while this modal
    // popup is closed (it blocks the rest of the UI while open), so the
    // Connections handler below - not a live re-scan on a timer - is enough
    // to guarantee the NEXT open always reflects the latest session state;
    // this just also catches the (rarer) case of a profile being loaded out
    // from under an already-open Auditor via Quick Bind/Auto-Switch.
    Connections {
        target: profileEditorViewModel
        function onBindingUpdated() {
            if (root.visible) {
                root.refresh();
            }
        }
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

        // --- Header: title + vJoy device picker -----------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            spacing: Theme.spacingMd

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: qsTr("vJoy Mapping Status"); color: Theme.text; font.pixelSize: 17; font.weight: Font.DemiBold }
                Text {
                    text: qsTr("Every axis/button already targeted by a binding in the current profile, for the vJoy device selected below.")
                    color: Theme.subtext0
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }

            ColumnLayout {
                spacing: 2
                Text { text: qsTr("vJoy Device"); color: Theme.subtext0; font.pixelSize: 11 }
                AppComboBox {
                    id: deviceCombo
                    Layout.preferredWidth: 150
                    model: {
                        const names = [];
                        for (let i = 1; i <= 16; i++) {
                            names.push(qsTr("vJoy Device %1").arg(i));
                        }
                        return names;
                    }
                    currentIndex: root.targetOutputId - 1
                    onActivated: (idx) => {
                        root.targetOutputId = idx + 1;
                        root.refresh();
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            height: 1
            color: Qt.rgba(1, 1, 1, 0.1)
        }

        // --- Axes section -----------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            spacing: Theme.spacingSm

            Text {
                text: qsTr("AXES")
                color: Theme.subtext0
                font.pixelSize: 11
                font.weight: Font.DemiBold
                font.letterSpacing: 1
            }

            RowLayout {
                spacing: Theme.spacingSm

                Repeater {
                    // Plain JS string array model (root.axisNames) - each
                    // delegate gets implicit "modelData"/"index" context
                    // properties, same convention ModeManagerPopup.qml's own
                    // Repeater (over a QStringList) already uses.
                    model: root.axisNames
                    delegate: Rectangle {
                        readonly property string axisName: modelData
                        readonly property bool occupied: root.isAxisOccupied(index)

                        Layout.preferredWidth: 64
                        Layout.preferredHeight: 40
                        radius: Theme.radiusSmall
                        color: occupied ? Theme.accent : Theme.surface1
                        border.width: 1
                        border.color: occupied ? Theme.accent : Qt.rgba(1, 1, 1, 0.08)

                        Text {
                            anchors.centerIn: parent
                            text: parent.axisName
                            font.family: "Consolas"
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                            color: parent.occupied ? Theme.crust : Theme.subtext0
                        }
                    }
                }

                Item { Layout.fillWidth: true }
            }
        }

        // --- Buttons section ----------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            spacing: Theme.spacingSm

            Text {
                text: qsTr("BUTTONS (1-128)")
                color: Theme.subtext0
                font.pixelSize: 11
                font.weight: Font.DemiBold
                font.letterSpacing: 1
            }

            ScrollView {
                id: buttonsScroll
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(buttonsFlow.implicitHeight, root.maxListHeight)
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                contentWidth: availableWidth

                Flow {
                    id: buttonsFlow
                    width: buttonsScroll.availableWidth
                    spacing: 4

                    Repeater {
                        // Plain int model - each delegate gets an implicit
                        // "index" context property (0..127), no separate
                        // "modelData" since there's no underlying data, just
                        // a count.
                        model: 128
                        delegate: Rectangle {
                            readonly property int buttonIndex: index
                            readonly property bool occupied: root.isButtonOccupied(buttonIndex)

                            width: 30
                            height: 30
                            radius: Theme.radiusSmall
                            color: occupied ? Theme.accent : Theme.surface1
                            border.width: 1
                            border.color: occupied ? Theme.accent : Qt.rgba(1, 1, 1, 0.08)

                            ToolTip.visible: buttonMouseArea.containsMouse
                            ToolTip.text: qsTr("Button %1 - %2").arg(buttonIndex + 1).arg(occupied ? qsTr("Occupied") : qsTr("Free"))

                            MouseArea {
                                id: buttonMouseArea
                                anchors.fill: parent
                                hoverEnabled: true
                            }

                            Text {
                                anchors.centerIn: parent
                                text: parent.buttonIndex + 1
                                font.family: "Consolas"
                                font.pixelSize: 10
                                color: parent.occupied ? Theme.crust : Theme.subtext0
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
            height: 1
            color: Qt.rgba(1, 1, 1, 0.1)
        }

        // --- Legend + Close -----------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            Layout.bottomMargin: Theme.spacingMd
            spacing: Theme.spacingLg

            RowLayout {
                spacing: Theme.spacingSm
                Rectangle { width: 14; height: 14; radius: Theme.radiusSmall; color: Theme.surface1; border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.08) }
                Text { text: qsTr("Free"); color: Theme.subtext0; font.pixelSize: 12 }
            }
            RowLayout {
                spacing: Theme.spacingSm
                Rectangle { width: 14; height: 14; radius: Theme.radiusSmall; color: Theme.accent }
                Text { text: qsTr("Occupied"); color: Theme.subtext0; font.pixelSize: 12 }
            }

            Item { Layout.fillWidth: true }

            ToolButton {
                label: qsTr("Close")
                onClicked: root.close()
            }
        }
    }
}
