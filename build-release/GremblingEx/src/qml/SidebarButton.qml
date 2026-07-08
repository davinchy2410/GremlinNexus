import QtQuick
import GremblingEx

// Reusable animated navigation entry for the sidebar (Phase 10). Every
// state change - hover, press, selection - is driven by a Behavior, so
// there is no state where a color/scale jumps instantly; the ~120-200ms
// eased transitions are what make the sidebar feel "alive" rather than a
// static list of buttons.
Item {
    id: root

    signal clicked()
    property string label: ""
    property string iconGlyph: "●"
    property bool selected: false

    implicitHeight: 48

    Rectangle {
        id: background
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingSm
        anchors.rightMargin: Theme.spacingSm
        radius: Theme.radiusMedium
        color: root.selected ? Theme.surface1 : (mouseArea.containsMouse ? Theme.surface0 : "transparent")

        Behavior on color {
            ColorAnimation { duration: Theme.animMedium; easing.type: Easing.OutCubic }
        }

        scale: mouseArea.pressed ? 0.97 : (mouseArea.containsMouse ? 1.02 : 1.0)
        Behavior on scale {
            NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutBack }
        }

        // Left accent bar, only visible (and only "grown") once selected -
        // the classic "active tab" indicator, animated in/out rather than toggled.
        Rectangle {
            width: 3
            height: parent.height * (root.selected ? 0.6 : 0.0)
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: -Theme.spacingSm + 1
            radius: 2
            color: Theme.accent

            Behavior on height {
                NumberAnimation { duration: Theme.animMedium; easing.type: Easing.OutCubic }
            }
        }

        Row {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingMd
            spacing: Theme.spacingSm

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.iconGlyph
                font.pixelSize: 16
                color: root.selected ? Theme.accent : Theme.subtext0
                Behavior on color { ColorAnimation { duration: Theme.animMedium } }
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.label
                font.pixelSize: 14
                font.weight: root.selected ? Font.DemiBold : Font.Normal
                color: root.selected ? Theme.text : Theme.subtext0
                Behavior on color { ColorAnimation { duration: Theme.animMedium } }
            }
        }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
