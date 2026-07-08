import QtQuick
import QtQuick.Layouts
import GremblingEx

// One physical input (axis/button) row inside an expanded DeviceCard.
// Clickable (Fase 10.8): opens the Action Picker for this input. Also
// exposes flash() so Quick Bind auto-detection can briefly highlight the
// row it found without the caller needing its own animation machinery.
//
// Root is a plain Item, NOT a RowLayout: the background Rectangle and
// MouseArea below need to sit *behind*/*over* the row's content without
// themselves being treated as layout cells (a RowLayout lays out every
// direct child it has, attached Layout.* properties or not) - so the
// actual row content lives in its own inner RowLayout, and the background/
// MouseArea are its siblings instead.
Item {
    id: root
    property string inputName: ""
    property string inputKind: "axis" // "axis" | "button"
    property bool hasBinding: false
    property string bindingLabel: ""

    signal clicked()

    function flash() {
        flashAnimation.restart()
    }

    implicitHeight: 28
    Layout.preferredHeight: 28

    // Hover glow (Fase 15): sits behind flashBackground so a Quick Bind
    // flash - opaque Theme.accent - fully covers it mid-animation, and it
    // simply reappears once the flash fades back to transparent.
    Rectangle {
        id: hoverBackground
        anchors.fill: parent
        anchors.margins: -4
        radius: Theme.radiusSmall
        color: Theme.surface1
        opacity: mouseArea.containsMouse ? 0.5 : 0
        Behavior on opacity {
            NumberAnimation { duration: Theme.animFast }
        }
    }

    Rectangle {
        id: flashBackground
        anchors.fill: parent
        anchors.margins: -4
        radius: Theme.radiusSmall
        color: "transparent"

        SequentialAnimation {
            id: flashAnimation
            ColorAnimation { target: flashBackground; property: "color"; to: Theme.accent; duration: Theme.animFast }
            PauseAnimation { duration: 260 }
            ColorAnimation { target: flashBackground; property: "color"; to: "transparent"; duration: Theme.animSlow }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: Theme.spacingSm

        Text {
            text: root.inputKind === "button" ? "◇" : "∿"
            color: Theme.overlay0
            font.pixelSize: 13
            Layout.preferredWidth: 16
        }

        Text {
            text: root.inputName
            color: Theme.subtext0
            font.pixelSize: 13
            Layout.fillWidth: true
        }

        Badge {
            visible: root.hasBinding
            text: root.bindingLabel
        }

        Text {
            visible: !root.hasBinding
            text: "Unbound"
            color: Theme.overlay0
            font.pixelSize: 11
            font.italic: true
        }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        anchors.margins: -4
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
