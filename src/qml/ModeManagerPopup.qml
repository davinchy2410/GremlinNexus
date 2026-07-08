import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GremblingNexus

// Sprint 5 (Familias de Modos): replaces the old flat "+"/pencil/"-" mode
// controls with a single management surface for the whole mode-inheritance
// tree. "Inherits from" here is not cosmetic - it edits the same
// EventRouter::m_modeParents map resolveHandlerWithFallback() actually
// climbs at dispatch time (see EventRouter.cpp): an input with no binding in
// its own mode falls through to whatever its parent (then its parent's
// parent, ...) has bound, all the way up to Global. Self-contained like
// OneToOnePopup/ConfirmationPopup: ProfileEditorView.qml just instantiates
// this once and opens it - creating, renaming, re-parenting and deleting
// modes all happen here now, calling straight into
// ProfileEditorViewModel::addMode()/renameMode()/setModeParent()/
// removeMode() (all of which already existed except setModeParent(), a
// thin Sprint 5 wrapper around the router).
Popup {
    id: root

    modal: true
    focus: true
    parent: Overlay.overlay
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    width: 460
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    /// Name of the mode whose row is currently showing its rename TextField
    /// instead of its plain Text label - "" means no row is being renamed.
    /// Double-clicking a (non-Global) mode's name enters this state; Enter/
    /// Escape/losing focus all leave it (see the delegate below).
    property string renamingMode: ""

    /// Capped like OneToOnePopup's own preview ScrollView (see Memory.md's
    /// "ScrollView dentro de un Popup no encoge" entry) - capping the
    /// ScrollView's Layout.preferredHeight against its inner ColumnLayout's
    /// real implicitHeight is what makes the scrollbar appear once the mode
    /// list overflows; capping the Popup itself does not reliably work.
    readonly property int maxListHeight: Overlay.overlay
        ? Math.max(200, Math.round(Overlay.overlay.height * 0.5))
        : 320

    onOpened: root.renamingMode = ""
    onClosed: root.renamingMode = ""

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
            spacing: 2

            Text { text: qsTr("Manage Modes"); color: Theme.text; font.pixelSize: 17; font.weight: Font.DemiBold }
            Text {
                text: qsTr("A mode falls back to its parent's bindings when it has none of its own for an input - cascading all the way up to Global.")
                color: Theme.subtext0
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            height: 1
            color: Qt.rgba(1, 1, 1, 0.1)
        }

        ScrollView {
            id: modesScroll
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            Layout.preferredHeight: Math.min(modesContent.implicitHeight, root.maxListHeight)
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            contentWidth: availableWidth

            ColumnLayout {
                id: modesContent
                width: modesScroll.availableWidth
                spacing: Theme.spacingXs

                Repeater {
                    model: profileEditorViewModel.modes
                    delegate: Rectangle {
                        id: modeRow

                        // Fase (Sprint 5): the Repeater's model is a plain
                        // QStringList (profileEditorViewModel.modes), so
                        // each delegate gets a "modelData" context property
                        // of that mode's name - named here for readability,
                        // same convention OneToOnePopup's own Repeater uses.
                        property string modeName: modelData
                        // "Global" mirrors EventRouter::kGlobalMode's own
                        // hardcoded literal value - EventRouter itself isn't
                        // exposed to QML, so (matching the rest of this
                        // file's existing convention, e.g. the old +/-/
                        // pencil buttons this popup replaced) the string is
                        // just spelled out here too.
                        readonly property bool isGlobal: modeName === "Global"

                        Layout.fillWidth: true
                        implicitHeight: rowLayout.implicitHeight + Theme.spacingSm * 2
                        radius: Theme.radiusSmall
                        color: Theme.surface1
                        border.width: 1
                        border.color: Qt.rgba(1, 1, 1, 0.06)

                        RowLayout {
                            id: rowLayout
                            anchors.fill: parent
                            anchors.margins: Theme.spacingSm
                            spacing: Theme.spacingSm

                            Text {
                                visible: root.renamingMode !== modeRow.modeName
                                text: modeRow.modeName
                                color: Theme.text
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                                Layout.preferredWidth: 110
                                elide: Text.ElideRight

                                // Global's name is EventRouter::kGlobalMode's
                                // own hardcoded literal - renaming it would
                                // silently break every "Global" mode binding,
                                // so (like the old renameModePopup before it)
                                // it stays unrenamable.
                                MouseArea {
                                    anchors.fill: parent
                                    enabled: !modeRow.isGlobal
                                    cursorShape: !modeRow.isGlobal ? Qt.PointingHandCursor : Qt.ArrowCursor
                                    onDoubleClicked: {
                                        root.renamingMode = modeRow.modeName;
                                        renameField.text = modeRow.modeName;
                                        renameField.selectAll();
                                        renameField.forceActiveFocus();
                                    }
                                }
                            }
                            TextField {
                                id: renameField
                                visible: root.renamingMode === modeRow.modeName
                                Layout.preferredWidth: 110
                                color: Theme.text
                                font.pixelSize: 13
                                background: Rectangle {
                                    color: Theme.surface0
                                    radius: Theme.radiusSmall
                                    border.width: 1
                                    border.color: Theme.accent
                                }
                                Keys.onReturnPressed: {
                                    const newName = text.trim();
                                    if (newName.length > 0) {
                                        profileEditorViewModel.renameMode(modeRow.modeName, newName);
                                    }
                                    root.renamingMode = "";
                                }
                                Keys.onEscapePressed: root.renamingMode = ""
                                onActiveFocusChanged: if (!activeFocus) root.renamingMode = ""
                            }

                            Item { Layout.fillWidth: true }

                            Text {
                                visible: modeRow.isGlobal
                                text: qsTr("Root mode")
                                color: Theme.overlay0
                                font.pixelSize: 11
                                font.italic: true
                            }

                            ColumnLayout {
                                visible: !modeRow.isGlobal
                                spacing: 1
                                Text { text: qsTr("Inherits from"); color: Theme.subtext0; font.pixelSize: 10 }
                                AppComboBox {
                                    id: parentCombo
                                    Layout.preferredWidth: 130
                                    // Every other mode is a valid parent -
                                    // excluding modeRow's own name here keeps
                                    // it from ever offering a direct
                                    // self-reference (EventRouter::
                                    // setModeParent() would just reject it
                                    // anyway, treating it as "no parent").
                                    model: profileEditorViewModel.modes.filter((m) => m !== modeRow.modeName)
                                    Component.onCompleted: {
                                        const currentParent = profileEditorViewModel.getModeParent(modeRow.modeName);
                                        const idx = model.indexOf(currentParent);
                                        currentIndex = idx >= 0 ? idx : model.indexOf("Global");
                                    }
                                    onActivated: (idx) => {
                                        profileEditorViewModel.setModeParent(modeRow.modeName, model[idx]);
                                    }
                                }
                            }

                            ToolButton {
                                visible: !modeRow.isGlobal
                                label: qsTr("Delete")
                                accentColor: Theme.danger
                                Layout.preferredWidth: 76
                                onClicked: profileEditorViewModel.removeMode(modeRow.modeName)
                            }
                        }
                    }
                }
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

            TextField {
                id: newModeField
                Layout.fillWidth: true
                placeholderText: qsTr("New mode name")
                color: Theme.text
                font.pixelSize: 13
                background: Rectangle {
                    color: Theme.surface1
                    radius: Theme.radiusSmall
                    border.width: 1
                    border.color: Qt.rgba(1, 1, 1, 0.08)
                }
                Keys.onReturnPressed: addModeButton.clicked()
            }
            ToolButton {
                id: addModeButton
                label: qsTr("Add New Mode")
                onClicked: {
                    const name = newModeField.text.trim();
                    if (name.length > 0) {
                        profileEditorViewModel.addMode(name);
                        newModeField.text = "";
                    }
                }
            }
            ToolButton {
                label: qsTr("Close")
                onClicked: root.close()
            }
        }
    }
}
