import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GremblingNexus

// One physical input (axis/button) row inside an expanded DeviceCard.
// Clickable (Fase 10.8): opens the Action Picker for this input. Also
// exposes flash() so Quick Bind auto-detection can briefly highlight the
// row it found without the caller needing its own animation machinery.
//
// Root is a plain Item, NOT a RowLayout: the background Rectangle and
// MouseArea below need to sit *behind*/*over* the row's content without
// themselves being treated as layout cells (a RowLayout lays out every
// direct child it has, attached Layout.* properties or not) - so the
// actual row content lives in its own inner RowLayout, and the background/
// MouseArea are its siblings instead.
Item {
    id: root
    property string devicePath: ""
    property string inputName: ""
    property string inputKind: "axis" // "axis" | "button"
    property bool hasBinding: false
    property string bindingLabel: ""

    /// Sprint QoL Part 2: the binding's optional free-text note (see
    /// ProfileEditorViewModel::makeInputEntry()'s "actionNote" role) -
    /// authored via ActionPickerPopup.qml's/CurveEditorView.qml's own "Nota /
    /// Descripción" field. "" means no note (see the info icon below).
    property string actionNote: ""

    signal clicked()

    /// Sprint Final (Curves deep-linking): fired by the curve icon's own
    /// click below (axis rows only - see showCurveIcon) - DeviceCard bubbles
    /// this up the same way it already does for clicked() above, so
    /// ProfileEditorView can call profileEditorViewModel.setCurveEditorTarget()
    /// with this row's own devicePath/inputName and switch to the Curves view.
    signal curvesRequested()

    function flash() {
        flashAnimation.restart()
    }

    // Sprint 2 curve indicator, extended in Sprint Final into a clickable
    // deep-link: hasCurve() is a plain Q_INVOKABLE, not a NOTIFY-backed
    // Q_PROPERTY, so a binding that calls it directly would only ever
    // re-evaluate when devicePath/inputName themselves change - never when
    // the user binds/unbinds a curve on this exact input while this row is
    // already on screen (same class of staleness hasCopiedAxis/
    // hasCopiedButton's own NOTIFY property was added to avoid - see
    // ProfileEditorViewModel.h). curveRefreshTick is a cheap local "force
    // re-evaluate" dependency: referencing it inside hasCustomCurve's
    // binding (JS comma operator - evaluates and discards, but still
    // registers as a read) makes that binding redo the hasCurve() call
    // whenever bindingUpdated() fires for this exact (devicePath, inputName),
    // without needing any new C++ signal.
    property int curveRefreshTick: 0

    /// Sprint Final: the curve icon is now this axis' own clickable
    /// deep-link into the Curve Editor (see curvesRequested() above), so it
    /// has to show for every axis row - not just ones with an already-
    /// customized curve, the way it worked before this row had anything
    /// clickable there. hasCustomCurve (below) still gates its color/
    /// tooltip, same "is this curve actually shaped, or still the flat
    /// default" distinction as before, just no longer whether the icon
    /// exists at all.
    readonly property bool showCurveIcon: root.inputKind === "axis"

    /// UI pass (vertical alignment bugfix): curveButton's own width, named
    /// here so the alignment spacer below (button rows only, which never
    /// show curveButton) can reproduce the exact gap it leaves rather than
    /// duplicating the literal "28" in two places.
    readonly property int curveButtonWidth: 28

    /// True once hasCurve() reports this exact axis has a user-customized
    /// curve - see curveRefreshTick's own docs above for why this isn't a
    /// plain one-shot binding.
    readonly property bool hasCustomCurve: {
        curveRefreshTick;
        return root.devicePath !== "" && profileEditorViewModel.hasCurve(root.devicePath, root.inputName);
    }

    Connections {
        target: profileEditorViewModel
        function onBindingUpdated(devicePath, inputName, hasBindingNow, bindingLabelNow) {
            if (devicePath === root.devicePath && inputName === root.inputName) {
                root.curveRefreshTick++;
            }
        }
    }

    implicitHeight: 28
    Layout.preferredHeight: 28

    // Hover glow (Fase 15): sits behind flashBackground so a Quick Bind
    // flash - opaque Theme.accent - fully covers it mid-animation, and it
    // simply reappears once the flash fades back to transparent. UI pass:
    // a faint cyan wash (not the flat surface1 fill this used to be) so the
    // row-guide reads as "terminal scanline" rather than a generic hover.
    Rectangle {
        id: hoverBackground
        anchors.fill: parent
        anchors.margins: -4
        radius: Theme.radiusSmall
        color: Theme.accent
        opacity: mouseArea.containsMouse ? 0.06 : 0
        Behavior on opacity {
            NumberAnimation { duration: Theme.animFast }
        }
    }

    Rectangle {
        id: flashBackground
        anchors.fill: parent
        anchors.margins: -4
        radius: Theme.radiusSmall
        color: "transparent"

        SequentialAnimation {
            id: flashAnimation
            ColorAnimation { target: flashBackground; property: "color"; to: Theme.accent; duration: Theme.animFast }
            PauseAnimation { duration: 260 }
            ColorAnimation { target: flashBackground; property: "color"; to: "transparent"; duration: Theme.animSlow }
        }
    }

    // Whole-row "open Action Picker" MouseArea - deliberately declared
    // BEFORE the RowLayout below (Sprint Final), not after: QML stacks later
    // siblings on top for input purposes, so the curve icon's own nested
    // MouseArea (inside RowLayout) needs to be the LATER sibling to ever
    // receive its own clicks at all - otherwise this row-wide MouseArea
    // (which fills the same bounds, margins and all) would swallow every
    // click on the icon first and this row's clicked() would fire instead of
    // curvesRequested().
    MouseArea {
        id: mouseArea
        anchors.fill: parent
        anchors.margins: -4
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }

    RowLayout {
        anchors.fill: parent
        spacing: Theme.spacingSm

        // Name column (UI pass v2): fixed-width so every row's binding
        // controls start at the same x regardless of how long the input's
        // name is - previously inputName was Layout.fillWidth, which
        // stretched this Text across the whole row and shoved everything
        // after it out to the far edge of wide device cards (the "tennis
        // match" look). Widened 300 -> 420 to rebalance the row further:
        // pulls the bindings column toward center instead of hugging the
        // left edge on wide cards.
        //
        // A plain Item now, not a RowLayout - the note icon (moved in here
        // from bindingsRow below, see its own docs) needs to sit at an
        // absolute anchors.right position that never participates in
        // layout flow, so toggling actionNote's visibility can never change
        // THIS column's own width or push bindingsRow to the right. That
        // was the actual bug: the note icon used to be a sibling INSIDE
        // bindingsRow, a RowLayout - QtQuick.Layouts gives a newly-visible
        // child its own width+spacing slot, so a row that happened to have
        // a note rendered its curve icon/badge one icon-width further right
        // than an otherwise-identical row without one.
        Item {
            Layout.preferredWidth: 420
            Layout.maximumWidth: 420
            Layout.fillHeight: true

            RowLayout {
                id: nameAndTag
                anchors.left: parent.left
                anchors.right: parent.right
                // Reserved unconditionally (not just when actionNote !== "")
                // so the name text's own elide point - and therefore this
                // whole column's rendered content - never shifts depending
                // on whether a note happens to be present.
                anchors.rightMargin: 24
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacingSm

                // Monospaced terminal tag replacing the old curve/diamond
                // glyph icons - fixed width (both strings are 8 chars incl.
                // brackets, "BTN" padded to match "AXIS") so the tag column
                // itself never shifts the name text that follows it.
                Text {
                    text: root.inputKind === "button" ? "[ BTN  ]" : "[ AXIS ]"
                    font.family: "Consolas"
                    font.pixelSize: 11
                    color: root.inputKind === "button" ? Theme.subtext0 : Theme.accent
                    Layout.preferredWidth: 54
                }

                Text {
                    text: root.inputName
                    color: Theme.subtext0
                    font.pixelSize: 13
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }

            // Note indicator (Sprint QoL Part 2, relocated UI pass v2): subtle
            // "info" glyph, only shown once an actual note exists - the note
            // text itself only ever shows via the ToolTip below, never
            // inline. Deliberately NOT a button - a note is documentation,
            // not an action; clicking it does nothing, only hovering shows
            // it. Absolute-anchored to this Item's own right edge instead of
            // being a RowLayout sibling (see this column's own docs above
            // for why) - its visibility can never affect anything outside
            // this column.
            SvgIcon {
                visible: root.actionNote !== ""
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                pathData: "M12 22c5.523 0 10 -4.477 10 -10s-4.477 -10 -10 -10 -10 4.477 -10 10 4.477 10 10 10z M12 16v-4 M12 8h.01"
                color: noteIconMouseArea.containsMouse ? Theme.text : Theme.overlay0
                width: 13
                height: 13
                Behavior on color { ColorAnimation { duration: Theme.animFast } }

                ToolTip.visible: noteIconMouseArea.containsMouse
                ToolTip.text: root.actionNote
                MouseArea {
                    id: noteIconMouseArea
                    anchors.fill: parent
                    anchors.margins: -4
                    hoverEnabled: true
                }
            }
        }

        // Bindings column (bugfix: previously these were loose direct
        // children of the outer RowLayout with nothing to size them as a
        // group, so once the name column above stopped being
        // Layout.fillWidth, this stuff had no rule at all pushing it left -
        // it just drifted to wherever the layout felt like. Wrapping it in
        // its own RowLayout with NO fillWidth of its own, pinned
        // Qt.AlignLeft, gives it a single well-defined size that hugs the
        // name column; the real "push everything left" work is the spacer
        // Item below, not this wrapper.
        RowLayout {
            id: bindingsRow
            Layout.alignment: Qt.AlignLeft
            spacing: Theme.spacingSm

            // Curve-column alignment spacer (bugfix): only axis rows show
            // curveButton below (showCurveIcon gates it on inputKind) - and
            // QtQuick.Layouts excludes invisible items from the layout
            // entirely, so on button rows nothing was reserving that
            // curveButton.width + spacing gap. Net effect: the vJoy
            // binding pill/"Unbound" text landed one whole curve-button-
            // width further LEFT on button rows than on axis rows, breaking
            // the vertical rule between them. This spacer only shows on
            // button rows and exactly backfills that gap - its own width is
            // just curveButtonWidth (not +spacing too): becoming a new
            // visible RowLayout child already contributes one extra
            // `spacing` gap on its own, same as curveButton does when it's
            // the one that's visible, so adding spacing again here would
            // double-count it and overshoot by 8px.
            Item {
                visible: !root.showCurveIcon
                Layout.preferredWidth: root.curveButtonWidth
            }

            // Curve deep-link (Sprint 2 badge, extended Sprint Final into a
            // clickable icon, upgraded Sprint QoL Part 2 into a real square
            // button): every axis row gets one, opening the Curve Editor
            // pre-targeted at exactly this axis (see curvesRequested()
            // above). A real 28x28 Rectangle+SvgIcon+MouseArea (not just a
            // bare small glyph) - the previous icon-only version had a tiny,
            // fiddly click target; this one is deliberately sized to match
            // this row's own 28px height (see root's Layout.preferredHeight
            // above) so it reads as a proper button aligned with the
            // binding pill, not an afterthought floating next to it.
            // Background/border tint accent whenever hasCustomCurve is true
            // - same "is this curve actually shaped" signal the old passive
            // badge gave, just no longer gating whether the button exists
            // at all.
            Rectangle {
                id: curveButton
                visible: root.showCurveIcon
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28
                radius: Theme.radiusSmall
                border.width: 1
                border.color: root.hasCustomCurve ? Theme.accent : Qt.rgba(1, 1, 1, 0.08)
                color: root.hasCustomCurve
                    ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, curveButtonMouseArea.containsMouse ? 0.35 : 0.15)
                    : (curveButtonMouseArea.containsMouse ? Theme.surface2 : Theme.surface1)
                Behavior on color { ColorAnimation { duration: Theme.animFast } }

                SvgIcon {
                    anchors.centerIn: parent
                    width: 15
                    height: 15
                    pathData: "M4 18l6 -6 4 4 6 -6v3h2V7h-8v2h4l-5 5 -4 -4 -7 7z"
                    color: root.hasCustomCurve ? Theme.accent : (curveButtonMouseArea.containsMouse ? Theme.text : Theme.overlay0)
                    Behavior on color { ColorAnimation { duration: Theme.animFast } }
                }

                ToolTip.visible: curveButtonMouseArea.containsMouse
                ToolTip.text: root.hasCustomCurve
                    ? qsTr("Custom response curve - click to edit")
                    : qsTr("Open Curve Editor for this axis")
                MouseArea {
                    id: curveButtonMouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.curvesRequested()
                }
            }

            // Visual weight (UI pass v2): a floor, not a fixed width - short
            // labels ("vJ1 B2") no longer shrink-wrap down to a sliver next
            // to a long one ("vJ3 : Btn 128") on the row above/below it.
            // Badge.qml's own label is left-aligned (not centerIn) precisely
            // so this extra room reads as "roomier pill, text still pinned
            // left" instead of the text drifting toward the middle.
            Badge {
                visible: root.hasBinding
                text: root.bindingLabel
                Layout.minimumWidth: 140
                Layout.alignment: Qt.AlignLeft
            }

            Text {
                visible: !root.hasBinding
                text: qsTr("Unbound")
                color: Theme.overlay0
                font.pixelSize: 11
                font.italic: true
            }
        }

        // Spacer: absorbs all leftover row width so the bindings column
        // above stays hugged against the 300px name column instead of
        // drifting - this is the one item in the row allowed to
        // Layout.fillWidth.
        Item {
            Layout.fillWidth: true
        }
    }
}
