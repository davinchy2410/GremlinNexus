import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import GremblingNexus

// Custom minimize/maximize/close buttons (Fase 12) for the frameless
// ApplicationWindow (see main.qml's Qt.FramelessWindowHint) - every icon is
// drawn from plain Rectangles rather than an icon font/SVG, so there is
// nothing extra to ship and the stroke weight/color stay perfectly in sync
// with Theme. Theme has no "red" token (the prompt's own spec) - Theme.danger
// is this palette's semantic red, used for the same hover-to-red-fill affordance.
RowLayout {
    id: root
    spacing: 0

    readonly property var targetWindow: Window.window

    // --- Minimize --------------------------------------------------------
    Item {
        id: minimizeButton
        implicitWidth: 46
        implicitHeight: 32

        // Fase 14: whole-button bounce on hover/press, same feel as
        // ToolButton/GremblingButton - scaling the button as one unit
        // (rather than its background and glyph independently) keeps every
        // piece perfectly aligned through the animation.
        scale: minimizeArea.pressed ? 0.92 : (minimizeArea.containsMouse ? 1.05 : 1.0)
        Behavior on scale { NumberAnimation { duration: 150; easing.type: Easing.OutBack } }

        Rectangle {
            anchors.fill: parent
            color: Theme.surface1
            opacity: minimizeArea.containsMouse ? 1.0 : 0.0
            Behavior on opacity { NumberAnimation { duration: Theme.animFast } }
        }

        Rectangle {
            anchors.centerIn: parent
            width: 10
            height: 1.5
            color: Theme.text
        }

        MouseArea {
            id: minimizeArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.ArrowCursor
            onClicked: root.targetWindow.showMinimized()
        }
    }

    // --- Maximize / Restore -----------------------------------------------
    Item {
        id: maximizeButton
        implicitWidth: 46
        implicitHeight: 32

        readonly property bool isMaximized: (root.targetWindow && root.targetWindow.visibility === Window.Maximized) || (root.targetWindow && root.targetWindow.isPseudoMaximized)

        scale: maximizeArea.pressed ? 0.92 : (maximizeArea.containsMouse ? 1.05 : 1.0)
        Behavior on scale { NumberAnimation { duration: 150; easing.type: Easing.OutBack } }

        Rectangle {
            anchors.fill: parent
            color: Theme.surface1
            opacity: maximizeArea.containsMouse ? 1.0 : 0.0
            Behavior on opacity { NumberAnimation { duration: Theme.animFast } }
        }

        // Single square outline when windowed; a smaller square offset
        // behind it (the classic "restore" glyph) once maximized.
        Rectangle {
            visible: maximizeButton.isMaximized
            anchors.centerIn: parent
            anchors.horizontalCenterOffset: 2
            anchors.verticalCenterOffset: -2
            width: 8
            height: 8
            color: "transparent"
            border.width: 1.5
            border.color: Theme.text
        }
        Rectangle {
            anchors.centerIn: parent
            anchors.horizontalCenterOffset: maximizeButton.isMaximized ? -2 : 0
            anchors.verticalCenterOffset: maximizeButton.isMaximized ? 2 : 0
            width: 8
            height: 8
            color: maximizeButton.isMaximized ? Theme.surface1 : "transparent"
            border.width: 1.5
            border.color: Theme.text
        }

        MouseArea {
            id: maximizeArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.ArrowCursor
            onClicked: {
                root.targetWindow.toggleSmartMaximize();
            }
        }
    }

    // --- Close -------------------------------------------------------------
    Item {
        id: closeButton
        implicitWidth: 46
        implicitHeight: 32

        scale: closeArea.pressed ? 0.92 : (closeArea.containsMouse ? 1.05 : 1.0)
        Behavior on scale { NumberAnimation { duration: 150; easing.type: Easing.OutBack } }

        Rectangle {
            anchors.fill: parent
            color: Theme.danger
            opacity: closeArea.containsMouse ? 1.0 : 0.0
            Behavior on opacity { NumberAnimation { duration: Theme.animFast } }
        }

        Item {
            anchors.centerIn: parent
            width: 10
            height: 10

            Rectangle {
                anchors.centerIn: parent
                width: parent.width
                height: 1.5
                color: closeArea.containsMouse ? Theme.crust : Theme.text
                rotation: 45
            }
            Rectangle {
                anchors.centerIn: parent
                width: parent.width
                height: 1.5
                color: closeArea.containsMouse ? Theme.crust : Theme.text
                rotation: -45
            }
        }

        MouseArea {
            id: closeArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.ArrowCursor
            onClicked: root.targetWindow.close()
        }
    }
}
