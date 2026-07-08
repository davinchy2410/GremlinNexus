import QtQuick
import GremblingEx

// Small pill label for a mapped binding (e.g. "CurveHandler", "Keyboard: A").
Rectangle {
    id: root
    property string text: ""

    radius: Theme.radiusSmall
    color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.18)
    border.width: 1
    border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.5)

    implicitWidth: label.implicitWidth + Theme.spacingSm * 2
    implicitHeight: label.implicitHeight + Theme.spacingXs * 2

    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        color: Theme.accent
        font.pixelSize: 11
        font.weight: Font.DemiBold
    }
}
