import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GremblingEx

// "Log Console" screen (Fase 14): live view of every qDebug()/qInfo()/
// qWarning()/qCritical() message the app has logged this session, backed by
// LogModel (a process-wide QStringListModel singleton installed as the Qt
// message handler in main.cpp). A plain ListView over "display" (the role
// QStringListModel exposes by default) auto-scrolls to the newest line
// unless the user has scrolled up to read older ones, the same "don't yank
// the view out from under someone reading scrollback" behavior a terminal
// gives you.
Item {
    id: root

    Rectangle {
        anchors.fill: parent
        color: Theme.base
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingMd

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMd

            ColumnLayout {
                spacing: 2
                Text {
                    text: "Log Console"
                    color: Theme.text
                    font.pixelSize: 24
                    font.weight: Font.DemiBold
                }
                Text {
                    text: "Live qDebug/qInfo/qWarning output for this session"
                    color: Theme.subtext0
                    font.pixelSize: 13
                }
            }

            Item { Layout.fillWidth: true }

            ToolButton {
                label: "Copy All"
                onClicked: {
                    hiddenCopyArea.text = logModel.getAllText()
                    hiddenCopyArea.selectAll()
                    hiddenCopyArea.copy()
                }
            }

            TextEdit {
                id: hiddenCopyArea
                visible: false
            }

            ToolButton {
                label: "Clear"
                onClicked: logModel.setStringList([])
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.radiusMedium
            color: Theme.surface0
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.06)
            clip: true

            ListView {
                id: logListView
                anchors.fill: parent
                anchors.margins: Theme.spacingSm
                model: logModel
                clip: true
                spacing: 1
                ScrollBar.vertical: ScrollBar { }

                // Auto-scroll to the newest line, unless the user has
                // manually scrolled away from the bottom to read scrollback.
                property bool stickToBottom: true
                onContentHeightChanged: if (stickToBottom) { positionViewAtEnd(); }
                onMovementEnded: stickToBottom = atYEnd

                delegate: TextEdit {
                    width: logListView.width
                    text: model.display
                    color: text.indexOf("WARN") >= 0 ? Theme.warning
                        : (text.indexOf("ERROR") >= 0 || text.indexOf("FATAL") >= 0 ? Theme.danger : Theme.subtext0)
                    font.family: "Consolas"
                    font.pixelSize: 11
                    wrapMode: TextEdit.Wrap
                    readOnly: true
                    selectByMouse: true
                    selectedTextColor: Theme.base
                    selectionColor: Theme.text
                }
            }
        }
    }
}
