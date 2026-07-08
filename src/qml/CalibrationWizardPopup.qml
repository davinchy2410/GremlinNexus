import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GremblingNexus

// Axis Calibration Wizard (Fase 10.9 mock-up, wired to CalibrationViewModel
// in Fase 11): walks the user through moving every axis on devicePath
// through its full range while calibrationViewModel (see its own class docs)
// tracks the observed raw min/max per axis, then commits them into
// ProfileManager on Save.
Popup {
    id: root

    modal: true
    focus: true
    x: Overlay.overlay ? (Overlay.overlay.width - width) / 2 : 0
    y: Overlay.overlay ? (Overlay.overlay.height - height) / 2 : 0
    width: 420
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    property string devicePath: ""

    /// "idle" -> "calibrating" -> back to "idle" (and closed) after Save.
    property string wizardState: "idle"

    function openFor(devicePath_) {
        root.devicePath = devicePath_;
        root.wizardState = "idle";
        root.open();
    }

    // Escape/click-outside while calibrating must not leave
    // calibrationViewModel stuck thinking it's still capturing samples for
    // a popup that no longer exists on screen.
    onClosed: {
        if (root.wizardState === "calibrating") {
            calibrationViewModel.cancelCalibration();
            root.wizardState = "idle";
        }
    }

    Overlay.modal: Rectangle {
        color: Qt.rgba(0, 0, 0, 0.55)
    }

    // Fase 14: standardized Glassmorphism (see GlassPanel's own docs).
    background: GlassPanel {
        bgOpacity: 0.9
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacingMd

        Item { Layout.preferredHeight: Theme.spacingMd }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            spacing: 2

            Text { text: qsTr("Axis Calibration"); color: Theme.text; font.pixelSize: 17; font.weight: Font.DemiBold }
            Text {
                text: root.devicePath
                color: Theme.subtext0
                font.pixelSize: 11
                elide: Text.ElideMiddle
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

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            Layout.topMargin: Theme.spacingSm
            Layout.bottomMargin: Theme.spacingSm
            spacing: Theme.spacingMd

            // Decorative "move in circles" indicator - spins while calibrating.
            Item {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 96
                Layout.preferredHeight: 96

                Rectangle {
                    anchors.fill: parent
                    radius: width / 2
                    color: "transparent"
                    border.width: 3
                    border.color: root.wizardState === "calibrating" ? Theme.accent : Theme.surface2
                }

                Rectangle {
                    id: sweepDot
                    width: 12
                    height: 12
                    radius: 6
                    color: Theme.accent
                    visible: root.wizardState === "calibrating"

                    property real angle: 0
                    x: parent.width / 2 + (parent.width / 2 - 6) * Math.cos(angle) - width / 2
                    y: parent.height / 2 + (parent.height / 2 - 6) * Math.sin(angle) - height / 2

                    NumberAnimation on angle {
                        running: root.wizardState === "calibrating"
                        from: 0
                        to: Math.PI * 2
                        duration: 1400
                        loops: Animation.Infinite
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: root.wizardState === "idle"
                    text: qsTr("🕹")
                    font.pixelSize: 32
                }
            }

            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: Theme.text
                font.pixelSize: 13
                text: root.wizardState === "calibrating"
                    ? qsTr("Move axes in full circles...")
                    : qsTr("Click Start, then move every stick/axis on this device through its full range of motion (full circles work best for gimbaled sticks).")
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
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
                label: root.wizardState === "calibrating" ? qsTr("Save Calibration") : qsTr("Start Calibration")
                onClicked: {
                    if (root.wizardState === "idle") {
                        calibrationViewModel.startCalibration(root.devicePath);
                        root.wizardState = "calibrating";
                    } else {
                        calibrationViewModel.commitCalibration();
                        root.wizardState = "idle";
                        root.close();
                    }
                }
            }
        }
    }
}
