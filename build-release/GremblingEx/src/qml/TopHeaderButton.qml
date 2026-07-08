import QtQuick
import QtQuick.Layouts
import QtQuick.Effects
import GremblingEx

// Reusable animated navigation tab for TopHeader (Fase 15.5, replaces
// SidebarButton). Same hover/press/selection Behaviors as the old sidebar
// entry - only the layout (horizontal icon+label pill) and the icon itself
// changed (SVG + MultiEffect colorization instead of a Text glyph).
Item {
    id: root

    signal clicked()
    property string label: ""
    property string iconSource: ""
    property bool selected: false

    // Fase 14: tightened from Theme.spacingMd (16px/side) to spacingSm
    // (8px/side) - with 9 tabs now in TopHeader's center group, the wider
    // padding pushed this bar's combined width past the window's, which is
    // what let the Engine switch overflow underneath WindowControls (see
    // TopHeader.qml's own clip: true safety net for the hard guarantee;
    // this is what keeps that clipping from actually kicking in at the
    // default window width).
    implicitWidth: content.implicitWidth + Theme.spacingSm * 2
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
            spacing: Theme.spacingSm

            // Icon container (Fase 14): MultiEffect's "colorization" only
            // shifts hue/saturation while preserving the source's own
            // luminance - a source SVG whose opaque pixels are near-black
            // (luminance ~0) therefore stays visually black no matter what
            // colorizationColor is set to, which was this tab bar's actual
            // contrast bug. Masking instead: iconColorRect is a flat,
            // fully-opaque Rectangle in the exact color we want, and the
            // MultiEffect repaints it through iconMaskSource's alpha shape
            // (maskEnabled/maskSource - Qt6 QuickEffects' native icon-tinting
            // mechanism), so the result is that solid color everywhere the
            // SVG had ink, regardless of the SVG's own pixel values. Both
            // inputs are visible: false - MultiEffect still captures their
            // rendered texture via its internal ShaderEffectSource, but
            // neither the raw black glyph nor the solid color square ever
            // paints directly to screen on its own.
            Item {
                Layout.preferredWidth: 18
                Layout.preferredHeight: 18

                Image {
                    id: iconMaskSource
                    anchors.fill: parent
                    source: root.iconSource
                    sourceSize: Qt.size(18, 18)
                    smooth: true
                    visible: false
                }

                Rectangle {
                    id: iconColorRect
                    anchors.fill: parent
                    visible: false
                    color: root.selected ? Theme.accent : (mouseArea.containsMouse ? Theme.text : Theme.overlay0)
                    Behavior on color { ColorAnimation { duration: Theme.animMedium } }
                }

                MultiEffect {
                    anchors.fill: parent
                    source: iconColorRect
                    maskEnabled: true
                    maskSource: iconMaskSource

                    // Neon Glow on the icon
                    shadowEnabled: root.selected || mouseArea.containsMouse
                    shadowColor: root.selected ? Theme.accent : Theme.text
                    shadowBlur: 1.0
                }
            }

            Text {
                text: root.label
                font.pixelSize: 13
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
