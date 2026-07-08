import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import GremblingEx

// Dark-themed ComboBox (Phase 10 Part 6) - Controls.Basic's stock
// background/indicator/popup need full re-skinning to match the rest of
// the app, same reasoning as ValueSlider's re-skinned Slider. Works with
// either a QAbstractItemModel (set textRole) or a plain string list.
ComboBox {
    id: control

    implicitHeight: 34
    font.pixelSize: 12

    background: Item {
        Rectangle {
            id: bgRect
            anchors.fill: parent
            radius: Theme.radiusSmall
            color: control.pressed ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.2) : 
                   (control.hovered ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.1) : "transparent")
            border.width: control.hovered || control.pressed ? 2 : 1
            border.color: control.hovered || control.pressed ? Theme.accentHover : Theme.surface1
            Behavior on color { ColorAnimation { duration: Theme.animFast } }
            Behavior on border.color { ColorAnimation { duration: Theme.animFast } }
        }

        MultiEffect {
            source: bgRect
            anchors.fill: bgRect
            shadowEnabled: control.hovered || control.pressed
            shadowColor: Theme.accent
            shadowBlur: 1.0
            opacity: control.hovered || control.pressed ? 0.8 : 0.0
            Behavior on opacity { NumberAnimation { duration: Theme.animFast } }
        }
    }

    contentItem: Text {
        text: control.displayText
        color: Theme.text
        font: control.font
        verticalAlignment: Text.AlignVCenter
        leftPadding: Theme.spacingSm
        rightPadding: 24
        elide: Text.ElideRight
    }

    indicator: Text {
        x: control.width - width - Theme.spacingSm
        y: control.topPadding + (control.availableHeight - height) / 2
        text: "▾"
        color: Theme.subtext0
        font.pixelSize: 11
    }

    popup: Popup {
        y: control.height + 4
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight, 200)
        padding: 4

        background: Item {
            Rectangle {
                id: popupBg
                anchors.fill: parent
                color: Qt.rgba(Theme.surface0.r, Theme.surface0.g, Theme.surface0.b, 0.85)
                radius: Theme.radiusSmall
                border.width: 1
                border.color: Theme.accent
            }
            MultiEffect {
                source: popupBg
                anchors.fill: popupBg
                shadowEnabled: true
                shadowColor: Theme.accent
                shadowBlur: 2.0
                blurMax: 32
            }
        }

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator { }
        }
    }

    delegate: ItemDelegate {
        id: del
        width: control.width
        height: 30
        highlighted: control.highlightedIndex === index

        contentItem: Text {
            text: control.textAt(index)
            color: del.highlighted ? Theme.crust : Theme.text
            font.pixelSize: 12
            verticalAlignment: Text.AlignVCenter
            leftPadding: Theme.spacingSm
            elide: Text.ElideRight
        }
        background: Rectangle {
            color: del.highlighted ? Theme.accent : "transparent"
            radius: Theme.radiusSmall
        }
    }
}
