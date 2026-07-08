import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Effects
import GremblingEx

// "Device Tester" screen (Fase 10.5, radar replaces the oscilloscope in
// Fase 16.6): a Virpil/VKB-style live hardware monitor. Header picks which
// connected device to watch; left is a 2D Cartesian radar - a live dot
// driven directly off axes 0 (X) and 1 (Y), the pair every flight stick
// treats as its primary movement plane, plus simple bipolar bars for
// whatever other axes the device has - right is a 128-button matrix that
// lights up on press. All live data comes from deviceTesterViewModel, which
// mirrors DeviceManager::axisMoved/buttonPressed for whichever device is
// selected; unlike the old oscilloscope (which kept its own sampled-history
// buffer), the radar/bars bind reactively straight to
// deviceTesterViewModel.axisValues - a calibration tool should show the
// current raw value with no added lag, not a smoothed trend.
Item {
    id: root

    // Semantic axis names (Fase 16.6), short form for this screen's compact
    // bars/labels - see ProfileEditorViewModel::makeDeviceEntry() for the
    // longer "Axis X"-style names used on the Profiles screen itself.
    readonly property var axisNames: ["X", "Y", "Z", "Rx", "Ry", "Rz", "Slider", "Dial"]

    // Bipolar [-1, 1] position of axisIndex, normalized against *this
    // device's own* HID logical range (deviceTesterViewModel.axisLogicalMin/
    // Max - see DeviceManager::registerHidDevice()) rather than a hardcoded
    // [0, 65535]: a HID device's native range varies (this project has seen
    // both a 16-bit vJoy device and a 12-bit VKBsim stick), so normalizing
    // against a fixed constant put every device's rest position somewhere
    // other than dead-center.
    function bipolarFor(axisIndex) {
        const values = deviceTesterViewModel.axisValues;
        const mins = deviceTesterViewModel.axisLogicalMin;
        const maxs = deviceTesterViewModel.axisLogicalMax;
        const raw = axisIndex < values.length ? values[axisIndex] : 0;
        const lo = axisIndex < mins.length ? mins[axisIndex] : 0;
        const hi = axisIndex < maxs.length ? maxs[axisIndex] : 65535;
        const range = hi - lo;
        return range > 0 ? ((raw - lo) / range) * 2 - 1 : 0;
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.base
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingLg

        // --- Header: device picker --------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            radius: Theme.radiusMedium
            color: Theme.surface0
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.05)

            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacingMd
                spacing: Theme.spacingLg

                ColumnLayout {
                    spacing: 2
                    Text { text: "Device"; color: Theme.subtext0; font.pixelSize: 11 }
                    AppComboBox {
                        id: deviceCombo
                        Layout.preferredWidth: 260
                        model: profileEditorViewModel
                        textRole: "deviceName"
                        valueRole: "systemPath"
                        onCurrentValueChanged: {
                            deviceTesterViewModel.currentSystemPath = currentValue || "";
                        }
                        Component.onCompleted: {
                            deviceTesterViewModel.currentSystemPath = currentValue || "";
                        }
                    }
                }

                Item { Layout.fillWidth: true }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacingLg

            // --- Left: 2D Cartesian radar (X/Y) + bipolar bars for the rest ---
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: Theme.radiusMedium
                color: Theme.surface0
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.05)

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingMd
                    spacing: Theme.spacingSm

                    Text { text: "Axes"; color: Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: Theme.spacingLg

                        // --- X/Y radar: the primary stick plane -------------
                        ColumnLayout {
                            Layout.fillHeight: true
                            spacing: Theme.spacingSm

                            Item {
                                id: radar
                                Layout.fillHeight: true
                                Layout.preferredWidth: height

                                readonly property real rawX: {
                                    const values = deviceTesterViewModel.axisValues;
                                    return values.length > 0 ? values[0] : 0;
                                }
                                readonly property real rawY: {
                                    const values = deviceTesterViewModel.axisValues;
                                    return values.length > 1 ? values[1] : 0;
                                }
                                // Bipolar [-1, 1], normalized against this
                                // device's own HID logical range - see
                                // root.bipolarFor()'s own docs above.
                                readonly property real bipolarX: root.bipolarFor(0)
                                readonly property real bipolarY: root.bipolarFor(1)

                                Rectangle {
                                    anchors.fill: parent
                                    radius: Theme.radiusMedium
                                    color: Theme.mantle
                                    border.width: 1
                                    border.color: Qt.rgba(1, 1, 1, 0.08)
                                }

                                // Purely decorative reference rings - the
                                // "radar" framing.
                                Repeater {
                                    model: 3
                                    delegate: Rectangle {
                                        anchors.centerIn: parent
                                        width: radar.width * (0.32 * (index + 1))
                                        height: width
                                        radius: width / 2
                                        color: "transparent"
                                        border.width: 1
                                        border.color: Qt.rgba(1, 1, 1, 0.06)
                                    }
                                }

                                // Fixed crosshair marking dead-center (bipolar 0, 0).
                                Rectangle {
                                    anchors.centerIn: parent
                                    width: parent.width - Theme.spacingMd
                                    height: 1
                                    color: Qt.rgba(1, 1, 1, 0.12)
                                }
                                Rectangle {
                                    anchors.centerIn: parent
                                    width: 1
                                    height: parent.height - Theme.spacingMd
                                    color: Qt.rgba(1, 1, 1, 0.12)
                                }

                                // Live position dot, bound directly to axes
                                // 0/1 - no smoothing or history buffer, since
                                // a calibration tool should show the current
                                // raw value with no added lag.
                                //
                                // Y is deliberately NOT flipped relative to
                                // bipolarY: screen space has y=0 at the top,
                                // so a direct (bipolarY+1)/2*height mapping
                                // already puts a negative bipolarY near the
                                // top (small y) - "raising" the dot exactly
                                // as a negative axis value should. Flipping
                                // this (the usual reflex when converting
                                // math-Cartesian to screen coordinates) would
                                // be the actual bug here: it would raise the
                                // dot for a *positive* value instead.
                                Rectangle {
                                    id: dot
                                    width: 16
                                    height: 16
                                    radius: 8
                                    color: Theme.danger
                                    x: (radar.bipolarX + 1) / 2 * (radar.width - width)
                                    y: (radar.bipolarY + 1) / 2 * (radar.height - height)
                                }
                                MultiEffect {
                                    source: dot
                                    anchors.fill: dot
                                    shadowEnabled: true
                                    shadowColor: Theme.danger
                                    shadowBlur: 1.0
                                    blurMax: 24
                                }
                            }

                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                text: "X: " + radar.rawX.toFixed(0) + "   Y: " + radar.rawY.toFixed(0)
                                color: Theme.subtext0
                                font.pixelSize: 11
                            }
                        }

                        // --- Remaining axes: bipolar bars -------------------
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignTop
                            spacing: Theme.spacingSm

                            Repeater {
                                model: 6 // axes 2..7: Z, Rx, Ry, Rz, Slider, Dial.
                                delegate: RowLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.spacingSm

                                    readonly property int axisIndex: index + 2
                                    readonly property real raw: {
                                        const values = deviceTesterViewModel.axisValues;
                                        return axisIndex < values.length ? values[axisIndex] : 0;
                                    }
                                    readonly property real bipolar: root.bipolarFor(axisIndex)

                                    Text {
                                        text: axisIndex < root.axisNames.length ? root.axisNames[axisIndex] : ("Axis " + axisIndex)
                                        color: Theme.subtext0
                                        font.pixelSize: 11
                                        Layout.preferredWidth: 44
                                    }

                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 14
                                        radius: Theme.radiusSmall
                                        color: Theme.mantle
                                        border.width: 1
                                        border.color: Qt.rgba(1, 1, 1, 0.06)
                                        clip: true

                                        Rectangle {
                                            anchors.left: parent.left
                                            anchors.leftMargin: 2
                                            anchors.verticalCenter: parent.verticalCenter
                                            height: parent.height - 4
                                            radius: Theme.radiusSmall
                                            color: Theme.accent
                                            width: Math.max(0, ((bipolar + 1) / 2) * (parent.width - 4))
                                        }
                                    }

                                    Text {
                                        text: Math.round(((bipolar + 1) / 2) * 100) + "%"
                                        color: Theme.overlay0
                                        font.pixelSize: 10
                                        Layout.preferredWidth: 36
                                        horizontalAlignment: Text.AlignRight
                                    }
                                }
                            }

                            Item { Layout.fillHeight: true }
                        }
                    }
                }
            }

            // --- Right: button matrix -------------------------------------
            Rectangle {
                Layout.preferredWidth: 320
                Layout.fillHeight: true
                radius: Theme.radiusMedium
                color: Theme.surface0
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.05)

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingMd
                    spacing: Theme.spacingSm

                    Text { text: "Buttons"; color: Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }

                    GridView {
                        id: buttonGrid
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        cellWidth: 34
                        cellHeight: 34
                        // Excludes the synthetic per-hat button entries
                        // (Fase 16.7/16.8) - those get their own 3x3 D-pad
                        // section below instead of showing up as loose
                        // buttons at the end of this grid. 256 is a safety
                        // cap; on a small device it settles to the real
                        // physical button count.
                        model: Math.min(256, deviceTesterViewModel.currentDeviceNumButtons - deviceTesterViewModel.currentDeviceNumHats * 4)

                        // Marks which buttons were pressed at least once
                        // since the current device was selected ("button
                        // trails"), keyed by index. touchedButtonsVersion is
                        // a plain counter bumped alongside every in-place
                        // mutation of touchedButtons so the "touched"
                        // property below - a QML dictionary key isn't
                        // itself observable, so this stands in as the
                        // rebinding trigger.
                        property var touchedButtons: ({})
                        property int touchedButtonsVersion: 0

                        Connections {
                            target: deviceTesterViewModel
                            function onCurrentSystemPathChanged() {
                                buttonGrid.touchedButtons = {};
                                buttonGrid.touchedButtonsVersion++;
                            }
                        }

                        delegate: Rectangle {
                            width: 30
                            height: 30
                            radius: Theme.radiusSmall
                            border.width: 1
                            border.color: Qt.rgba(1, 1, 1, 0.08)

                            property bool pressed: {
                                const states = deviceTesterViewModel.buttonStates;
                                return index < states.length && states[index] === true;
                            }
                            property bool touched: {
                                buttonGrid.touchedButtonsVersion;
                                return buttonGrid.touchedButtons[index] === true;
                            }
                            onPressedChanged: {
                                if (pressed) {
                                    buttonGrid.touchedButtons[index] = true;
                                    buttonGrid.touchedButtonsVersion++;
                                }
                            }
                            color: pressed ? Theme.accent : (touched ? Theme.accentSecondary : Theme.surface1)
                            Behavior on color { ColorAnimation { duration: Theme.animFast } }

                            Text {
                                anchors.centerIn: parent
                                text: index + 1
                                color: (pressed || touched) ? Theme.crust : Theme.subtext0
                                font.pixelSize: 10
                            }
                        }

                        ScrollBar.vertical: ScrollBar { }
                    }

                    // --- POV hats: dedicated 3x3 D-pad matrices ------------
                    Repeater {
                        model: deviceTesterViewModel.currentDeviceNumHats

                        delegate: ColumnLayout {
                            id: hatDelegate
                            Layout.fillWidth: true
                            spacing: Theme.spacingSm

                            readonly property int hatIndex: index
                            // Where this hat's 4 synthetic Up/Right/Down/Left
                            // button entries start (Fase 16.7 - see
                            // DeviceInfo::numButtons's docs).
                            readonly property int base: deviceTesterViewModel.currentDeviceNumButtons - deviceTesterViewModel.currentDeviceNumHats * 4 + (hatIndex * 4)

                            readonly property bool hatUp: {
                                const states = deviceTesterViewModel.buttonStates;
                                return base + 0 < states.length && states[base + 0] === true;
                            }
                            readonly property bool hatRight: {
                                const states = deviceTesterViewModel.buttonStates;
                                return base + 1 < states.length && states[base + 1] === true;
                            }
                            readonly property bool hatDown: {
                                const states = deviceTesterViewModel.buttonStates;
                                return base + 2 < states.length && states[base + 2] === true;
                            }
                            readonly property bool hatLeft: {
                                const states = deviceTesterViewModel.buttonStates;
                                return base + 3 < states.length && states[base + 3] === true;
                            }

                            Text {
                                text: "Hat " + (hatIndex + 1)
                                color: Theme.subtext0
                                font.pixelSize: 11
                            }

                            GridLayout {
                                columns: 3
                                rows: 3
                                columnSpacing: 2
                                rowSpacing: 2

                                Repeater {
                                    model: [
                                        { lit: hatDelegate.hatUp && hatDelegate.hatLeft, label: "" },
                                        { lit: hatDelegate.hatUp, label: "U" },
                                        { lit: hatDelegate.hatUp && hatDelegate.hatRight, label: "" },
                                        { lit: hatDelegate.hatLeft, label: "L" },
                                        { lit: !hatDelegate.hatUp && !hatDelegate.hatRight && !hatDelegate.hatDown && !hatDelegate.hatLeft, label: "C" },
                                        { lit: hatDelegate.hatRight, label: "R" },
                                        { lit: hatDelegate.hatDown && hatDelegate.hatLeft, label: "" },
                                        { lit: hatDelegate.hatDown, label: "D" },
                                        { lit: hatDelegate.hatDown && hatDelegate.hatRight, label: "" }
                                    ]

                                    delegate: Rectangle {
                                        Layout.preferredWidth: 30
                                        Layout.preferredHeight: 30
                                        radius: Theme.radiusSmall
                                        border.width: 1
                                        border.color: Qt.rgba(1, 1, 1, 0.08)
                                        color: modelData.lit ? Theme.accent : Theme.surface1
                                        Behavior on color { ColorAnimation { duration: Theme.animFast } }

                                        Text {
                                            anchors.centerIn: parent
                                            text: modelData.label
                                            color: modelData.lit ? Theme.crust : Theme.subtext0
                                            font.pixelSize: 10
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
