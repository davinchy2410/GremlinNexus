import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Effects
import GremblingEx

// Animated accordion card for one device: clicking the header expands/
// collapses its physical-input list. cardBackground's height is the only
// thing driving the whole card's size (see implicitHeight/height below) -
// the Behavior on cardBackground.height is what makes both the card itself
// AND every sibling below it in the outer ColumnLayout reflow smoothly.
Item {
    id: root

    property string deviceName: ""
    property string vendorProduct: ""
    property string systemPath: ""
    property var inputs: []
    property bool expanded: false

    // HidHide cloaking (Fase 11): whether this device is currently hidden
    // from non-whitelisted applications. Re-read explicitly after every
    // toggle (see cloakButton.onClicked below) rather than kept live via a
    // changed-signal - HidHideManager's own state can also change outside
    // this process (its GUI, another app), so isDeviceCloaked() is a fresh
    // query every time anyway; a signal here could only ever cover the one
    // path this button itself drives.
    property bool hidHideInstalled: profileEditorViewModel.isHidHideInstalled()
    property bool cloaked: hidHideInstalled && profileEditorViewModel.isDeviceCloaked(root.systemPath)

    /// Emitted when one of this device's InputRows is clicked - bubbles up
    /// enough context (systemPath + the clicked input's own name/kind) for
    /// ProfileEditorView's shared Action Picker popup to open pre-targeted
    /// at the right input.
    signal inputClicked(string systemPath, string inputName, string inputKind, bool hasBinding)

    /// Fase 10.9: bubbles up to ProfileEditorView's shared
    /// CalibrationWizardPopup, same "just carry the systemPath up" pattern
    /// as inputClicked above.
    signal calibrateRequested(string systemPath)

    /// Quick Bind support (Fase 10.8): expands this card if needed and
    /// flashes the InputRow named name, so ProfileEditorView's
    /// hardwareInputDetected handler can point the user at whichever
    /// physical input it just saw move/press. Returns the InputRow item
    /// (so the caller can also scroll it into view) or null if this device
    /// has no input by that name. Works even while collapsed - a Repeater's
    /// delegates exist regardless of the wrapping ColumnLayout's own
    /// visibility, only their on-screen layout footprint collapses to zero.
    function highlightInput(name) {
        root.expanded = true;
        for (let i = 0; i < inputRepeater.count; i++) {
            const item = inputRepeater.itemAt(i);
            if (item && item.inputName === name) {
                item.flash();
                return item;
            }
        }
        return null;
    }

    /// Fase 20.5: patches one already-existing InputRow delegate's binding
    /// state in place, instead of the Repeater rebuilding every delegate the
    /// way reassigning root.inputs wholesale would - that rebuild is what
    /// broke scrollToItem's geometry calculation in Fase 20.4.
    function updateBinding(name, hasBinding, label) {
        for (let i = 0; i < inputRepeater.count; i++) {
            const item = inputRepeater.itemAt(i);
            if (item && item.inputName === name) {
                item.hasBinding = hasBinding;
                item.bindingLabel = label;

                // Keeps the underlying JS array in sync too, so collapsing
                // and re-expanding the card (which rebuilds the Repeater
                // from root.inputs) doesn't revert this delegate's binding.
                if (root.inputs[i]) {
                    root.inputs[i].hasBinding = hasBinding;
                    root.inputs[i].bindingLabel = label;
                }
                return;
            }
        }
    }

    implicitHeight: cardBackground.height
    height: implicitHeight

    Rectangle {
        id: cardBackground
        width: root.width
        radius: Theme.radiusMedium
        color: Theme.surface0
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.05)
        clip: true

        height: contentColumn.implicitHeight + Theme.spacingMd * 2
        Behavior on height {
            NumberAnimation { duration: Theme.animMedium; easing.type: Easing.OutBack; easing.overshoot: 2 }
        }

        ColumnLayout {
            id: contentColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.spacingMd
            spacing: Theme.spacingSm

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm

                Item {
                    id: headerArea
                    Layout.fillWidth: true
                    implicitHeight: headerRowLayout.implicitHeight

                    // Subtle glow on hover (Fase 15): signals that the whole
                    // header - not just the chevron - is clickable to expand/
                    // collapse. Sits behind headerRowLayout and the click
                    // MouseArea below, so it never intercepts input.
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -6
                        radius: Theme.radiusSmall
                        color: Theme.surface1
                        opacity: headerMouseArea.containsMouse ? 0.5 : 0
                        Behavior on opacity {
                            NumberAnimation { duration: Theme.animFast }
                        }
                    }

                    RowLayout {
                        id: headerRowLayout
                        anchors.left: parent.left
                        anchors.right: parent.right
                        spacing: Theme.spacingSm

                        Text {
                            text: "▸"
                            color: Theme.subtext0
                            font.pixelSize: 14
                            rotation: root.expanded ? 90 : 0
                            Behavior on rotation {
                                NumberAnimation { duration: Theme.animMedium; easing.type: Easing.OutCubic }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            Text {
                                text: root.deviceName
                                color: Theme.text
                                font.pixelSize: 15
                                font.weight: Font.DemiBold
                            }
                            Text {
                                text: root.vendorProduct
                                color: Theme.overlay0
                                font.pixelSize: 11
                            }
                        }

                        Text {
                            text: root.inputs.length + " inputs"
                            color: Theme.subtext0
                            font.pixelSize: 11
                        }
                    }

                    MouseArea {
                        id: headerMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.expanded = !root.expanded
                    }
                }

                // 1:1 Map (Fase 14): binds every axis/hat/button on this
                // device straight through to the same index on the chosen
                // vJoy device - see ProfileEditorViewModel::create1to1Mapping().
                // Kept as a sibling of headerArea (not nested inside it) so
                // its own click targets never fall under headerArea's
                // expand/collapse MouseArea, which would otherwise swallow
                // every click here first.
                AppComboBox {
                    id: oneToOneTargetCombo
                    Layout.preferredWidth: 100
                    model: Array.from({length: 16}, (_, i) => "vJoy " + (i + 1))
                }
                ToolButton {
                    label: "1:1 Map"
                    onClicked: profileEditorViewModel.create1to1Mapping(root.systemPath, oneToOneTargetCombo.currentIndex + 1)
                }

                ToolButton {
                    label: "⚙ Calibrate"
                    onClicked: root.calibrateRequested(root.systemPath)
                }

                // Fase SC-7.12: starts a Smart Swap (see swapToPopup below)
                // with this card as the source - replaces the old global
                // "Swap Devices" button/popup in ProfileEditorView.qml, which
                // made the user hunt for both the source and destination in
                // two ComboBoxes instead of just starting from the device
                // card they're already looking at.
                ToolButton {
                    label: "🔄 Swap To..."
                    onClicked: swapToPopup.open()
                }

                // Fase SC-7.5: clears every binding on this device outright -
                // for a "[Legacy]" imported device this also makes the whole
                // row vanish (it has nothing left to show once its bindings
                // are gone), the same self-cleanup swapDevices() already
                // does; for a real device it just leaves every input Unbound.
                ToolButton {
                    label: "🗑 Clear All"
                    onClicked: profileEditorViewModel.clearDeviceBindings(root.systemPath)
                }

                // HidHide cloak/uncloak (Fase 11): hidden entirely (not just
                // disabled) when HidHide isn't installed - there is nothing
                // useful the user can do with this control in that case, and
                // a permanently-disabled button would just raise "why is
                // this greyed out?" without an obvious answer on-screen.
                Rectangle {
                    id: cloakButton
                    visible: root.hidHideInstalled
                    implicitHeight: 34
                    implicitWidth: cloakLabel.implicitWidth + 24
                    radius: Theme.radiusSmall
                    color: root.cloaked
                        ? (cloakMouseArea.pressed ? Qt.darker(Theme.danger, 1.2) : Theme.danger)
                        : (cloakMouseArea.pressed ? Theme.accentHover : (cloakMouseArea.containsMouse ? Theme.surface2 : Theme.surface1))
                    border.width: 1
                    border.color: Qt.rgba(1, 1, 1, 0.08)
                    Behavior on color { ColorAnimation { duration: Theme.animFast } }

                    Text {
                        id: cloakLabel
                        anchors.centerIn: parent
                        text: root.cloaked ? "🛡 Cloaked" : "👁 Visible"
                        color: root.cloaked ? Theme.crust : Theme.text
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }

                    MouseArea {
                        id: cloakMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            const newState = !root.cloaked;
                            profileEditorViewModel.setDeviceCloaked(root.systemPath, newState);
                            root.cloaked = profileEditorViewModel.isDeviceCloaked(root.systemPath);
                        }

                        ToolTip.visible: containsMouse
                        ToolTip.text: root.cloaked
                            ? "Hidden from other applications - click to reveal"
                            : "Visible to other applications - click to hide"
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: root.expanded ? Theme.spacingXs : 0
                spacing: 4
                visible: root.expanded

                Repeater {
                    id: inputRepeater
                    model: root.inputs
                    delegate: InputRow {
                        Layout.fillWidth: true

                        // Fase SC-7.11: a "[Legacy]"/orphan-device row is now
                        // a lost-mappings tray (Fase SC-7.9's partial Smart
                        // Swap) rather than a full device, so its 8 axes/128
                        // buttons worth of Unbound ghost entries just bury
                        // the handful that actually still need attention -
                        // hide anything unbound there. A real device is
                        // untouched (root.vendorProduct won't match), so
                        // every one of its physical inputs still always shows.
                        //
                        // Fase SC-7.13: reads this delegate's own hasBinding
                        // property (set two lines below, and already wired
                        // up to update live as C++ pushes changes) instead of
                        // the raw modelData.hasBinding - QML has no way to
                        // know a plain modelData property changed underneath
                        // it unless the model itself emits a change signal
                        // for that role, so the old binding froze at
                        // whatever hasBinding was the moment this ghost row
                        // was first created (false), hiding the entire
                        // device - including rows that later did get a
                        // rescued/leftover binding - rather than just the
                        // ones still genuinely unbound.
                        visible: root.vendorProduct !== "Offline / Imported Device" || hasBinding

                        inputName: modelData.name
                        inputKind: modelData.kind
                        hasBinding: modelData.hasBinding
                        bindingLabel: modelData.bindingLabel
                        onClicked: root.inputClicked(root.systemPath, inputName, inputKind, hasBinding)
                    }
                }
            }
        }
    }

    MultiEffect {
        source: cardBackground
        anchors.fill: cardBackground
        shadowEnabled: true
        shadowColor: Theme.shadowColor
        shadowBlur: 0.6
        shadowVerticalOffset: 6
        blurMax: 24
    }

    // Fase SC-7.12: "Swap To..." destination picker - a checkbox-like list
    // (highlight + accent border on the selected row) rather than a
    // ComboBox, per the user's own request to make an accidental wrong-
    // device click harder than a dropdown makes it. swapDevices() itself is
    // still the Smart Swap from Fase SC-7.9 - only bindings that fit the
    // destination's real input count move; anything else stays behind here.
    Popup {
        id: swapToPopup
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: 400
        modal: true
        focus: true
        padding: Theme.spacingLg

        property int selectedIndex: -1
        property string selectedSystemPath: ""

        onClosed: {
            selectedIndex = -1;
            selectedSystemPath = "";
        }

        background: Rectangle {
            color: Theme.surface0
            radius: Theme.radiusLarge
            border.color: Theme.surface1
            border.width: 1
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.spacingMd

            Text {
                text: "Swap To..."
                color: Theme.text
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }
            Text {
                text: "Select the destination device. Only compatible bindings will be moved."
                color: Theme.subtext0
                font.pixelSize: 12
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Theme.surface1
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingXs

                Repeater {
                    model: profileEditorViewModel
                    delegate: Rectangle {
                        visible: model.systemPath !== root.systemPath
                        Layout.fillWidth: true
                        Layout.preferredHeight: 40
                        color: swapToPopup.selectedIndex === index ? Theme.surface1 : "transparent"
                        radius: Theme.radiusMedium
                        border.color: swapToPopup.selectedIndex === index ? Theme.accent : "transparent"

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: Theme.spacingSm
                            Text {
                                text: model.deviceName
                                color: Theme.text
                                font.pixelSize: 14
                                Layout.fillWidth: true
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                swapToPopup.selectedIndex = index;
                                swapToPopup.selectedSystemPath = model.systemPath;
                            }
                        }
                    }
                }
            }

            Item { Layout.fillHeight: true } // spacer

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingMd
                Item { Layout.fillWidth: true }
                ToolButton {
                    label: "Cancel"
                    onClicked: swapToPopup.close()
                }
                ToolButton {
                    label: "Apply Swap"
                    enabled: swapToPopup.selectedIndex !== -1
                    opacity: enabled ? 1.0 : 0.5
                    onClicked: {
                        const fromRow = profileEditorViewModel.deviceRowForSystemPath(root.systemPath);
                        const toRow = profileEditorViewModel.deviceRowForSystemPath(swapToPopup.selectedSystemPath);
                        profileEditorViewModel.swapDevices(fromRow, toRow);
                        swapToPopup.close();
                    }
                }
            }
        }
    }
}
