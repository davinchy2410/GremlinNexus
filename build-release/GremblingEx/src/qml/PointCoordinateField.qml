import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GremblingEx

// One coordinate (X or Y) of curveEditorViewModel's selected control point
// (Phase 10 Part 6).
//
// `field.text` is deliberately a plain property, resynced explicitly, not a
// live `text: expression` binding - the same binding-vs-user-input lesson
// as CurveHandle's dragged position (Part 3): a bound `text` gets silently
// destroyed the moment the user types into the field (QML property-write
// semantics), so a live binding would only ever populate it once and then
// silently stop following selection changes. Resyncing explicitly, only on
// the events that should actually override in-progress typing (a new
// point gets selected, or the model changes shape after Enter is pressed),
// avoids that trap entirely.
ColumnLayout {
    id: root

    property string label: ""
    property string axis: "x" // "x" | "y"

    spacing: 2

    function currentPoint() {
        const idx = curveEditorViewModel.selectedIndex;
        const pts = curveEditorViewModel.points();
        return (idx >= 0 && idx < pts.length) ? pts[idx] : null;
    }

    function sync() {
        const p = currentPoint();
        field.text = p ? Number(p[root.axis]).toFixed(3) : "";
    }

    Text { text: root.label; color: Theme.overlay0; font.pixelSize: 10 }

    TextField {
        id: field
        Layout.fillWidth: true
        implicitHeight: 30
        color: Theme.text
        font.pixelSize: 12
        selectByMouse: true
        enabled: root.currentPoint() !== null
        validator: DoubleValidator { bottom: 0.0; top: 1.0; decimals: 3 }

        background: Rectangle {
            color: Theme.surface1
            radius: Theme.radiusSmall
            border.width: 1
            border.color: field.activeFocus ? Theme.accent : Qt.rgba(1, 1, 1, 0.08)
            Behavior on border.color { ColorAnimation { duration: Theme.animFast } }
        }

        onAccepted: {
            const p = root.currentPoint();
            if (!p) {
                return;
            }
            const value = parseFloat(field.text);
            if (isNaN(value)) {
                root.sync();
                return;
            }
            const idx = curveEditorViewModel.selectedIndex;
            if (root.axis === "x") {
                curveEditorViewModel.updatePoint(idx, value, p.y);
            } else {
                curveEditorViewModel.updatePoint(idx, p.x, value);
            }
        }
    }

    Component.onCompleted: root.sync()

    Connections {
        target: curveEditorViewModel
        function onSelectedIndexChanged() { root.sync(); }
        function onPointsChanged() { root.sync(); }
    }
}
