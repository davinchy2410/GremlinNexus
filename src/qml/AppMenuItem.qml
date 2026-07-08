import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GremblingNexus

// Dark-themed MenuItem re-skin, shared by every dropdown Menu in the app
// (Profiles header's "Options" menu, each DeviceCard's terminal menu, ...).
// Extracted out of ProfileEditorView.qml's own inline StyledMenuItem once a
// second file (DeviceCard.qml) needed the identical look - same reasoning
// as ToolButton.qml/GremblingButton.qml existing as their own files instead
// of being duplicated per screen.
//
// iconData (not Qt's own icon.source): this app doesn't ship individual
// rasterized/file-based icon assets for its buttons - every icon everywhere
// (ToolButton, TopHeaderButton, DeviceCard's old per-device buttons, ...) is
// an inline SVG path `d` string rendered through SvgIcon.qml. iconData
// matches that same house convention so a caller can pass the exact same
// path string it would give any other button in the app, instead of this
// one control alone expecting a real qrc:/ asset that doesn't exist.
MenuItem {
    id: root

    property string iconData: ""

    implicitWidth: 220
    implicitHeight: 34

    contentItem: RowLayout {
        spacing: Theme.spacingSm

        SvgIcon {
            visible: root.iconData !== ""
            Layout.preferredWidth: 16
            Layout.preferredHeight: 16
            Layout.leftMargin: Theme.spacingSm
            pathData: root.iconData
            color: root.highlighted ? Theme.crust : Theme.subtext0
        }

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: root.iconData === "" ? Theme.spacingSm : 0
            text: root.text
            color: root.highlighted ? Theme.crust : Theme.text
            font.pixelSize: 12
            font.weight: Font.DemiBold
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 1
            verticalAlignment: Text.AlignVCenter
        }
    }

    background: Rectangle {
        radius: Theme.radiusSmall
        color: root.highlighted ? Theme.accent : "transparent"
    }
}
