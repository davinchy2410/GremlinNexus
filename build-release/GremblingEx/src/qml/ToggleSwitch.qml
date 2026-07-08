import QtQuick
import GremblingEx

// Custom pill toggle (Phase 10 Part 6) - same reasoning as ToolButton: a
// plain Rectangle + MouseArea gives full control over the Theme-driven
// look without fighting Controls.Basic's default Switch styling.
Item {
    id: root

    property bool checked: false
    signal toggled(bool checked)

    implicitWidth: 44
    implicitHeight: 24

    Rectangle {
        id: track
        anchors.fill: parent
        radius: height / 2
        color: root.checked ? Theme.accent : Theme.surface1
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.08)

        Behavior on color { ColorAnimation { duration: Theme.animFast } }
    }

    Rectangle {
        id: knob
        width: parent.height - 4
        height: parent.height - 4
        radius: width / 2
        anchors.verticalCenter: parent.verticalCenter
        x: root.checked ? parent.width - width - 2 : 2
        color: Theme.text

        Behavior on x { NumberAnimation { duration: Theme.animMedium; easing.type: Easing.OutBack } }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            root.checked = !root.checked;
            root.toggled(root.checked);
        }
    }
}
