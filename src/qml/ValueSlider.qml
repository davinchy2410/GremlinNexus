import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Effects
import GremblingNexus

// Labeled slider row. Phase 14: Neon glow effect added to active track.
// Deliberately re-skins Controls.Basic's Slider background/handle instead of
// leaving the stock gray look - QQuickStyle::setStyle("Basic") in main.cpp
// exists precisely so nothing here has any native-OS chrome to fight.
ColumnLayout {
    id: root

    property string label: ""
    property real value: 0.0
    property real from: 0.0
    property real to: 1.0
    property int decimals: 2

    signal moved(real value)

    spacing: 4

    RowLayout {
        Layout.fillWidth: true
        Text { text: root.label; color: Theme.text; font.pixelSize: 13; Layout.fillWidth: true }
        Text {
            text: root.value.toFixed(root.decimals)
            color: Theme.accent
            font.pixelSize: 13
            font.weight: Font.DemiBold
        }
    }

    Slider {
        id: slider
        Layout.fillWidth: true
        from: root.from
        to: root.to
        value: root.value
        onMoved: root.moved(value)

        background: Rectangle {
            x: slider.leftPadding
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: slider.availableWidth
            height: 4
            radius: 2
            color: Theme.surface1

            Rectangle {
                id: activeTrack
                width: slider.visualPosition * parent.width
                height: parent.height
                radius: 2
                color: Theme.accent
            }

            MultiEffect {
                source: activeTrack
                anchors.fill: activeTrack
                shadowEnabled: true
                shadowColor: Theme.accent
                shadowBlur: 1.0
                opacity: 0.6
            }
        }

        handle: Rectangle {
            x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: 16
            height: 16
            radius: 8
            color: slider.pressed ? Theme.accentHover : Theme.accent
            border.width: 2
            border.color: Theme.crust
            scale: slider.pressed ? 1.15 : 1.0

            Behavior on scale { NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutBack } }
            Behavior on color { ColorAnimation { duration: Theme.animFast } }
        }
    }
}
