import QtQuick
import QtQuick.Effects
import GremblingEx

// Small flat action button for quick tools. Phase 14 Redesign: Hollow/Neon.
Item {
    id: root

    property string label: ""
    property color accentColor: Theme.accent
    property color accentHoverColor: Theme.accentHover
    
    signal clicked()

    implicitHeight: 34
    implicitWidth: labelText.implicitWidth + 24

    scale: mouseArea.pressed ? 0.95 : (mouseArea.containsMouse ? 1.02 : 1.0)
    Behavior on scale { NumberAnimation { duration: 150; easing.type: Easing.OutBack } }

    Rectangle {
        id: bgRect
        anchors.fill: parent
        radius: Theme.radiusSmall
        color: mouseArea.pressed ? Qt.rgba(root.accentColor.r, root.accentColor.g, root.accentColor.b, 0.2) : 
               (mouseArea.containsMouse ? Qt.rgba(root.accentColor.r, root.accentColor.g, root.accentColor.b, 0.1) : "transparent")
        border.width: mouseArea.containsMouse || mouseArea.pressed ? 2 : 1
        border.color: mouseArea.containsMouse || mouseArea.pressed ? root.accentHoverColor : Theme.surface1

        Behavior on color { ColorAnimation { duration: Theme.animFast } }
        Behavior on border.color { ColorAnimation { duration: Theme.animFast } }
    }

    MultiEffect {
        source: bgRect
        anchors.fill: bgRect
        shadowEnabled: mouseArea.containsMouse
        shadowColor: root.accentColor
        shadowBlur: 1.0
        opacity: mouseArea.containsMouse ? 0.8 : 0.0
        Behavior on opacity { NumberAnimation { duration: Theme.animFast } }
    }

    Text {
        id: labelText
        anchors.centerIn: parent
        text: root.label
        color: mouseArea.containsMouse || mouseArea.pressed ? root.accentHoverColor : Theme.text
        font.pixelSize: 12
        font.weight: Font.DemiBold
        Behavior on color { ColorAnimation { duration: Theme.animFast } }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
