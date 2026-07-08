import QtQuick
import GremblingNexus

// One draggable multi-point-spline control point (Phase 10 Part 3, extended
// in Part 6 with selection/delete).
//
// Position is a single, permanently-active binding to pointX/pointY
// (bipolar [-1,1], sourced from CurveEditorViewModel.points) - there is
// no drag.target and nothing ever writes to root.x/y directly. Dragging
// works by mapping the mouse's position into the graph's own coordinate
// space (mapToItem) and calling updatePoint() with the resulting
// normalized (x, y); the ViewModel clamps it and the position binding
// above picks up the (possibly-clamped) result on its own. An earlier
// version used QtQuick's built-in drag.target together with a
// conditionally-disabled Binding, which raced drag.target's internal
// drag-start latching against the moment the Binding's `when` flips - that
// combination was observed to occasionally snap a point to a wildly wrong
// position on press. Never writing to x/y directly sidesteps the whole
// class of bug.
Item {
    id: root

    property int pointIndex: 0
    property real pointX: 0.0
    property real pointY: 0.0
    property real graphWidth: 1
    property real graphHeight: 1
    property bool dragging: false
    property bool selected: false

    readonly property real radius: 7

    // pointX lives in the *post-deadzone* domain CurveHandler actually
    // evaluates the spline over (see CurveEditorViewModel's own class
    // docs) - the drawn curve in CurveEditorView.qml's Canvas is sampled
    // in the *raw* input domain and pushed through deadzoneAdjust() before
    // evaluating that same spline, so for deadzone > 0 a point drawn at
    // its own raw pointX would sit visibly off the curve. physicalX
    // reverses deadzoneAdjust() - stretching pointX back out by the
    // deadzone gap - so the handle always renders exactly on top of the
    // curve, at the raw stick position that actually produces this point.
    property real dz: curveEditorViewModel.deadzone
    property real physicalX: pointX > 0.0
        ? pointX * (1.0 - dz) + dz
        : (pointX < 0.0 ? pointX * (1.0 - dz) - dz : 0.0)

    width: radius * 2
    height: radius * 2
    // Maps bipolar [-1,1] physicalX/pointY into [0, graphWidth/Height]
    // pixel space - x=-1 sits at the left edge, x=1 at the right, x=0
    // dead-center (and mirrored, flipped, for y, since pixel Y grows
    // downward while pointY grows upward).
    x: ((physicalX + 1.0) / 2.0) * graphWidth - radius
    y: (1.0 - pointY) * 0.5 * graphHeight - radius

    Rectangle {
        id: dot
        anchors.fill: parent
        radius: root.radius
        color: mouseArea.containsMouse || root.dragging ? Theme.accentHover : Theme.accent
        border.width: root.selected ? 3 : 2
        border.color: root.selected ? Theme.text : Theme.crust
        scale: mouseArea.containsMouse || root.dragging ? 1.35 : 1.0

        Behavior on color { ColorAnimation { duration: Theme.animFast } }
        Behavior on scale { NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutBack } }
        Behavior on border.width { NumberAnimation { duration: Theme.animFast } }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        anchors.margins: -6 // Generous hit target beyond the small visible dot.
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        acceptedButtons: Qt.LeftButton | Qt.RightButton

        onPressed: (mouse) => {
            curveEditorViewModel.selectedIndex = root.pointIndex;
            if (mouse.button === Qt.LeftButton) {
                root.dragging = true;
            }
        }
        onReleased: {
            root.dragging = false;
        }

        // Two equivalent ways to delete a point, per the UX spec: a
        // right-click, or a left double-click.
        onClicked: (mouse) => {
            if (mouse.button === Qt.RightButton) {
                curveEditorViewModel.removePoint(root.pointIndex);
            }
        }
        onDoubleClicked: (mouse) => {
            if (mouse.button === Qt.LeftButton) {
                curveEditorViewModel.removePoint(root.pointIndex);
            }
        }

        onPositionChanged: (mouse) => {
            if (!root.dragging || root.graphWidth <= 0 || root.graphHeight <= 0) {
                return;
            }
            // root.parent is graphInner, the same coordinate space
            // curveCanvas (and therefore graphWidth/graphHeight) is
            // measured in - mapping through it gives graph-space pixels
            // regardless of where root itself currently sits.
            const graphPos = mouseArea.mapToItem(root.parent, mouse.x, mouse.y);
            const nx = (graphPos.x / root.graphWidth) * 2.0 - 1.0;
            const ny = (1.0 - graphPos.y / root.graphHeight) * 2.0 - 1.0;

            // Reverse physicalX's own math: the dragged pixel gives the raw
            // stick position (nx), but m_points/updatePoint() store the
            // post-deadzone spline-domain x - the same compression
            // CurveHandler::processAxis() and the canvas's own
            // deadzoneAdjust() apply, so dragging the dot back over the
            // deadzone's flat region collapses it to exactly 0 instead of
            // creeping past the endpoint clamp toward whatever's nearest.
            let adjX = 0.0;
            if (nx > root.dz) {
                adjX = (nx - root.dz) / (1.0 - root.dz);
            } else if (nx < -root.dz) {
                adjX = (nx + root.dz) / (1.0 - root.dz);
            }

            curveEditorViewModel.updatePoint(root.pointIndex, adjX, ny);
        }
    }
}
