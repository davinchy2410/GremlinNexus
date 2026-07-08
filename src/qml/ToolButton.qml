import QtQuick
import QtQuick.Layouts
import GremblingNexus

// Small flat action button for quick tools. FUI / Data Terminal redesign:
// same solid-fill-on-hover treatment as GremblingButton, no more
// MultiEffect glow - this is the single most-reused button primitive in
// the app, so it has to match or the redesign looks half-done.
Item {
    id: root

    property string label: ""
    property color accentColor: Theme.accent
    property color accentHoverColor: Theme.accentHover
    property string iconData: ""
    property bool iconFilled: false

    signal clicked()

    implicitHeight: 34
    implicitWidth: contentRow.implicitWidth + 24

    scale: mouseArea.pressed ? 0.95 : (mouseArea.containsMouse ? 1.02 : 1.0)
    Behavior on scale { NumberAnimation { duration: 150; easing.type: Easing.OutBack } }

    Rectangle {
        id: bgRect
        anchors.fill: parent
        radius: Theme.radiusSmall
        color: (mouseArea.containsMouse || mouseArea.pressed) ? root.accentColor : "transparent"
        border.width: 1
        border.color: root.accentColor

        Behavior on color { ColorAnimation { duration: Theme.animFast } }
    }

    RowLayout {
        id: contentRow
        anchors.centerIn: parent
        spacing: 6

        SvgIcon {
            visible: root.iconData !== ""
            pathData: root.iconData
            filled: root.iconFilled
            color: labelText.color
        }

        Text {
            id: labelText
            text: root.label
            font.pixelSize: 12
            font.weight: Font.DemiBold
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 1
            color: (mouseArea.containsMouse || mouseArea.pressed) ? Theme.crust : Theme.text
            Behavior on color { ColorAnimation { duration: Theme.animFast } }
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
