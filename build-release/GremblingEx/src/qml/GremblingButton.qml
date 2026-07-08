import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import GremblingEx

// A premium, Hollow/Neon styled button for the "Aeroespacial" look.
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
        font: control.font
        color: control.down || control.hovered ? control.accentHoverColor : Theme.text
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight

        Behavior on color {
            ColorAnimation { duration: Theme.animFast }
        }
    }

    // Background: Hollow with glowing neon border on hover
    background: Item {
        implicitWidth: 100
        implicitHeight: 32

        // The actual border box
        Rectangle {
            id: bgRect
            anchors.fill: parent
            color: control.down ? Qt.rgba(control.accentColor.r, control.accentColor.g, control.accentColor.b, 0.2) : 
                   (control.hovered ? Qt.rgba(control.accentColor.r, control.accentColor.g, control.accentColor.b, 0.1) : "transparent")
            border.color: control.down || control.hovered ? control.accentHoverColor : Theme.surface1
            border.width: control.hovered || control.down ? 2 : 1
            radius: Theme.radiusSmall

            Behavior on color { ColorAnimation { duration: Theme.animFast } }
            Behavior on border.color { ColorAnimation { duration: Theme.animFast } }
        }

        // The glow effect (Glassmorphism / Neon)
        MultiEffect {
            source: bgRect
            anchors.fill: bgRect
            shadowEnabled: control.hovered
            shadowColor: control.accentColor
            shadowBlur: 1.0
            shadowHorizontalOffset: 0
            shadowVerticalOffset: 0
            opacity: control.hovered ? 0.8 : 0.0

            Behavior on opacity {
                NumberAnimation { duration: Theme.animFast }
            }
        }
    }
}
