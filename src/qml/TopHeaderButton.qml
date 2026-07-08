import QtQuick
import QtQuick.Layouts
import QtQuick.Effects
import GremblingNexus

// Reusable animated navigation tab for TopHeader (Fase 15.5, replaces
// SidebarButton). Same hover/press/selection Behaviors as the old sidebar
// entry - only the layout (horizontal icon+label pill) and the icon itself
// changed (SVG + MultiEffect colorization instead of a Text glyph).
Item {
    id: root

    signal clicked()
    property string label: ""
    property string iconData: ""
    property bool selected: false

    // Fase 14: tightened from Theme.spacingMd (16px/side) to spacingSm
    // (8px/side) - with 9 tabs now in TopHeader's center group, the wider
    // padding pushed this bar's combined width past the window's, which is
    // what let the Engine switch overflow underneath WindowControls (see
    // TopHeader.qml's own clip: true safety net for the hard guarantee;
    // this is what keeps that clipping from actually kicking in at the
    // default window width).
    implicitWidth: content.implicitWidth + 12
    implicitHeight: 40

    Rectangle {
        id: background
        anchors.fill: parent
        radius: Theme.radiusMedium
        color: root.selected ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.1) : (mouseArea.containsMouse ? Theme.surface0 : "transparent")

        Behavior on color {
            ColorAnimation { duration: Theme.animMedium; easing.type: Easing.OutCubic }
        }

        scale: mouseArea.pressed ? 0.97 : (mouseArea.containsMouse ? 1.02 : 1.0)
        Behavior on scale {
            NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutBack }
        }

        // Active Bottom Indicator (Neon Line)
        Rectangle {
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            width: root.selected ? parent.width * 0.6 : 0
            height: 2
            color: Theme.accent
            radius: 1
            opacity: root.selected ? 1.0 : 0.0
            Behavior on width { NumberAnimation { duration: Theme.animMedium; easing.type: Easing.OutBack } }
            Behavior on opacity { NumberAnimation { duration: Theme.animMedium } }
        }

        // Glow for the active indicator
        MultiEffect {
            source: background.children[0]
            anchors.fill: background.children[0]
            shadowEnabled: root.selected
            shadowColor: Theme.accent
            shadowBlur: 1.0
        }

        RowLayout {
            id: content
            anchors.centerIn: parent
            spacing: 4

            // Icon container (FUI redesign): mathematically-built Feather-
            // style vectors via SvgIcon (same component DeviceCard.qml's
            // ToolButtons use) instead of a rasterized Image + MultiEffect
            // mask - no source image asset to color-mask around anymore,
            // SvgIcon bakes the stroke color straight into the generated
            // SVG data URI.
            Item {
                Layout.preferredWidth: 18
                Layout.preferredHeight: 18

                SvgIcon {
                    id: vectorIcon
                    anchors.fill: parent
                    pathData: root.iconData
                    color: root.selected ? Theme.accent : (mouseArea.containsMouse ? Theme.text : Theme.overlay0)
                    Behavior on color { ColorAnimation { duration: Theme.animMedium } }
                }

                MultiEffect {
                    anchors.fill: vectorIcon
                    source: vectorIcon
                    shadowEnabled: root.selected || mouseArea.containsMouse
                    shadowColor: root.selected ? Theme.accent : Theme.text
                    shadowBlur: 1.0
                }
            }

            Text {
                text: root.label
                font.pixelSize: 11
                font.weight: Font.DemiBold
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 1
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
