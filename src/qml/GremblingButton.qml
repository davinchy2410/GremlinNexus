import QtQuick
import QtQuick.Controls.Basic
import GremblingNexus

// FUI / Data Terminal button (previously "Hollow/Neon" for the
// "Aeroespacial" look): dark background, strict 1px border, fills solid
// with accentColor on hover/press instead of a translucent glow - no more
// MultiEffect blur, matching the wider "end of shadows" art direction.
Button {
    id: control

    // Custom properties to allow different button colors (e.g., Theme.danger)
    property color accentColor: Theme.accent
    property color accentHoverColor: Theme.accentHover

    // Basic styling
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)
    
    padding: Theme.spacingMd
    topPadding: Theme.spacingSm
    bottomPadding: Theme.spacingSm

    scale: control.down ? 0.95 : (control.hovered ? 1.02 : 1.0)
    Behavior on scale { NumberAnimation { duration: 150; easing.type: Easing.OutBack } }

    // Content: The Text
    contentItem: Text {
        text: control.text
        font.family: control.font.family
        font.pixelSize: control.font.pixelSize
        font.weight: control.font.weight
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 1
        color: (control.down || control.hovered) ? Theme.crust : Theme.text
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight

        Behavior on color {
            ColorAnimation { duration: Theme.animFast }
        }
    }

    // Background: dark, thin border, fills solid with accentColor on hover/press.
    background: Item {
        implicitWidth: 100
        implicitHeight: 32

        Rectangle {
            id: bgRect
            anchors.fill: parent
            color: (control.down || control.hovered) ? control.accentColor : "transparent"
            border.color: control.accentColor
            border.width: 1
            radius: Theme.radiusSmall

            Behavior on color { ColorAnimation { duration: Theme.animFast } }
        }
    }
}
