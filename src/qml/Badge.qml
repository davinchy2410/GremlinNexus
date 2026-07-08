import QtQuick
import GremblingNexus

// Small pill label for a mapped binding (e.g. "CurveHandler", "Keyboard: A").
Rectangle {
    id: root
    property string text: ""

    radius: 0
    color: Theme.surface1
    border.width: 1
    border.color: Theme.accent

    implicitWidth: label.implicitWidth + Theme.spacingSm * 2
    implicitHeight: label.implicitHeight + Theme.spacingXs * 2

    // Left-aligned (not centerIn) so a caller that stretches this pill
    // wider than its own content via Layout.minimumWidth (see InputRow.qml's
    // binding pills) reads as "left-aligned label in a roomier box" instead
    // of the text drifting to the middle - identical render everywhere this
    // pill's width already equals implicitWidth (every other caller today),
    // since Theme.spacingSm padding is symmetric either way.
    Text {
        id: label
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacingSm
        anchors.verticalCenter: parent.verticalCenter
        text: root.text
        color: Theme.accent
        font.pixelSize: 11
        font.weight: Font.DemiBold
    }
}
