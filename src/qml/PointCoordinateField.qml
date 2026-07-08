import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GremblingNexus

// One coordinate (X or Y) of curveEditorViewModel.points()[selectedIndex]
// (Phase 10 Part 6).
//
// currentPoint is a plain property, explicitly reassigned by refresh() (via
// the Connections below) rather than a live `property var currentPoint:
// curveEditorViewModel.points()[...]` expression - points() is a
// Q_INVOKABLE, not a NOTIFY-linked Q_PROPERTY, so a declarative binding
// that merely *calls* it establishes no dependency on curveEditorViewModel's
// own pointsChanged signal and would only ever refresh when selectedIndex
// happens to change, never while dragging. Explicitly listening for both
// signals and reassigning currentPoint imperatively is what makes dragging
// update these fields in real time.
//
// field.text itself is driven by a Binding element gated on
// `!field.activeFocus`, not a plain `text: expression` - the same
// binding-vs-user-input lesson as CurveHandle's dragged position (Part 3):
// an always-live `text` binding gets silently destroyed the instant the
// user types into the field (QML property-write semantics), so it would
// only ever show the very first value and then stop following anything.
// Gating on focus gets both properties at once: fully reactive (drag,
// selection change, Enter-commit, all flow straight through) while
// unfocused, and never touched while the user has a keystroke in flight.
ColumnLayout {
    id: root

    property string label: ""
    property string axis: "x" // "x" | "y"

    // null whenever nothing valid is selected - drives both the displayed
    // text (blank) and the field's enabled state below.
    property var currentPoint: null

    spacing: 2

    function refresh() {
        const idx = curveEditorViewModel.selectedIndex;
        const pts = curveEditorViewModel.points();
        root.currentPoint = (idx >= 0 && idx < pts.length) ? pts[idx] : null;
    }

    Text { text: root.label; color: Theme.overlay0; font.pixelSize: 10 }

    TextField {
        id: field
        Layout.fillWidth: true
        implicitHeight: 30
        color: Theme.text
        font.pixelSize: 12
        selectByMouse: true
        enabled: root.currentPoint !== null
        validator: DoubleValidator { bottom: -1.0; top: 1.0; decimals: 3; notation: DoubleValidator.StandardNotation }

        background: Rectangle {
            color: Theme.surface1
            radius: Theme.radiusSmall
            border.width: 1
            border.color: field.activeFocus ? Theme.accent : Qt.rgba(1, 1, 1, 0.08)
            Behavior on border.color { ColorAnimation { duration: Theme.animFast } }
        }

        Binding {
            target: field
            property: "text"
            value: root.currentPoint ? Number(root.currentPoint[root.axis]).toFixed(3) : ""
            when: !field.activeFocus
        }

        onAccepted: {
            const p = root.currentPoint;
            if (!p) {
                return;
            }
            const value = parseFloat(field.text);
            if (isNaN(value)) {
                field.focus = false; // Re-engages the Binding above, snapping back to the last-good value.
                return;
            }
            const idx = curveEditorViewModel.selectedIndex;
            if (root.axis === "x") {
                curveEditorViewModel.updatePoint(idx, value, p.y);
            } else {
                curveEditorViewModel.updatePoint(idx, p.x, value);
            }
            // Drop focus so the Binding immediately re-engages and displays
            // the committed (possibly ViewModel-clamped) value, rather than
            // leaving whatever raw string the user typed on screen.
            field.focus = false;
        }
    }

    Component.onCompleted: root.refresh()

    Connections {
        target: curveEditorViewModel
        function onSelectedIndexChanged() { root.refresh(); }
        function onPointsChanged() { root.refresh(); }
    }
}
