import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GremblingNexus

// Sprint 2: generic reusable "are you sure?" modal - prevents an accidental
// single click on a destructive action (e.g. DeviceCard's "Clear All") from
// silently wiping data with no way back. Deliberately dumb/stateless: it
// knows nothing about what it's confirming - the caller sets titleText/
// messageText, opens it, and reacts to accepted()/rejected() itself (see
// DeviceCard.qml's clearConfirmPopup for the reference wiring). Structurally
// mirrors OneToOnePopup/AxisSplitterPopup (same centered-Popup-over-Overlay
// pattern) so it looks native to the rest of this modal family.
Popup {
    id: root

    modal: true
    focus: true
    parent: Overlay.overlay // Fase 20.15: escape the opening popup's own coordinate system
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    width: 380
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    property string titleText: ""
    property string messageText: ""

    /// Confirm button's accent color - defaults to Theme.danger (a real
    /// "are you sure you want to destroy something" tone) so every existing
    /// caller (e.g. DeviceCard's "Clear All") keeps looking exactly the same.
    /// A caller confirming a non-destructive structural decision (e.g.
    /// ProfileEditorView.qml's merge-root-mode-into-Global prompt) can
    /// override this to Theme.accent instead, so the button doesn't visually
    /// imply data loss for an action that isn't one.
    property color confirmAccentColor: Theme.danger

    /// Fired when the user picks "Confirm" - the caller (not this popup)
    /// owns whatever destructive action that implies.
    signal accepted()

    /// Fired when the user picks "Cancel" (or dismisses the popup any other
    /// way it might close in the future) - most callers have nothing to do
    /// here, but it's exposed for symmetry with accepted().
    signal rejected()

    background: Rectangle {
        color: Theme.surface0
        radius: Theme.radiusMedium
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.08)
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacingMd

        Item { Layout.preferredHeight: Theme.spacingMd }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            spacing: Theme.spacingSm

            Text {
                text: root.titleText
                color: Theme.text
                font.pixelSize: 17
                font.weight: Font.DemiBold
            }
            Text {
                text: root.messageText
                color: Theme.subtext0
                font.pixelSize: 12
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            Layout.topMargin: Theme.spacingXs
            height: 1
            color: Qt.rgba(1, 1, 1, 0.1)
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            Layout.bottomMargin: Theme.spacingMd
            spacing: Theme.spacingSm

            Item { Layout.fillWidth: true }

            GremblingButton {
                text: qsTr("Cancel")
                onClicked: {
                    root.rejected();
                    root.close();
                }
            }
            GremblingButton {
                text: qsTr("Confirm")
                accentColor: root.confirmAccentColor
                accentHoverColor: root.confirmAccentColor
                onClicked: {
                    root.accepted();
                    root.close();
                }
            }
        }
    }
}
