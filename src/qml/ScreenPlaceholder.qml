import QtQuick
import QtQuick.Effects
import GremblingNexus

// Elegant "not built yet" placeholder for a Phase 10 section, standing in
// for Profiles/Curves/Control Center/Settings until each gets its own real
// screen in a later Phase 10 part. A soft-shadowed glass-ish card rather
// than bare centered text, so the skeleton itself still looks intentional.
Item {
    id: root
    property string title: ""
    property string subtitle: "Coming soon"

    Rectangle {
        anchors.fill: parent
        color: Theme.base
    }

    Rectangle {
        id: card
        anchors.centerIn: parent
        width: 380
        height: 190
        radius: Theme.radiusLarge
        color: Theme.surface0
        opacity: 0.6
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.06)
    }

    MultiEffect {
        source: card
        anchors.fill: card
        shadowEnabled: true
        shadowColor: Qt.rgba(0, 0, 0, 0.5)
        shadowBlur: 0.8
        shadowVerticalOffset: 10
        shadowOpacity: 0.6
        blurMax: 32
    }

    Column {
        anchors.centerIn: card
        spacing: Theme.spacingSm

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.title
            color: Theme.text
            font.pixelSize: 24
            font.weight: Font.DemiBold
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.subtitle
            color: Theme.subtext0
            font.pixelSize: 13
        }
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 36
            height: 3
            radius: 2
            color: Theme.accent
        }
    }
}
