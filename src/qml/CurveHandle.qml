import QtQuick
import GremblingNexus

// One draggable multi-point-spline control point (Phase 10 Part 3, extended
// in Part 6 with selection/delete).
//
// Position is a single, permanently-active binding to pointX/pointY
// (normalized [0,1], sourced from CurveEditorViewModel.points) - there is
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

    width: radius * 2
    height: radius * 2
    x: pointX * graphWidth - radius
    y: (1.0 - pointY) * graphHeight - radius

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
            const nx = graphPos.x / root.graphWidth;
            const ny = 1.0 - graphPos.y / root.graphHeight;
            curveEditorViewModel.updatePoint(root.pointIndex, nx, ny);
        }
    }
}
