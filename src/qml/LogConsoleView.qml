import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GremblingNexus

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
                    text: qsTr("Log Console")
                    color: Theme.text
                    font.pixelSize: 24
                    font.weight: Font.DemiBold
                }
                Text {
                    text: qsTr("Live qDebug/qInfo/qWarning output for this session")
                    color: Theme.subtext0
                    font.pixelSize: 13
                }
            }

            Item { Layout.fillWidth: true }

            ToolButton {
                label: qsTr("Copy All")
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
                label: qsTr("Clear")
                onClicked: logModel.clearLogs()
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

            ScrollView {
                id: scrollView
                anchors.fill: parent
                anchors.margins: Theme.spacingSm
                clip: true

                property bool stickToBottom: true
                ScrollBar.vertical.onPositionChanged: {
                    if (ScrollBar.vertical.pressed) {
                        stickToBottom = (ScrollBar.vertical.position >= 1.0 - ScrollBar.vertical.size - 0.01)
                    }
                }

                TextArea {
                    id: logArea
                    text: logModel.htmlText
                    textFormat: TextEdit.RichText
                    font.family: "Consolas"
                    font.pixelSize: 11
                    wrapMode: TextEdit.Wrap
                    readOnly: true
                    selectByMouse: true
                    selectedTextColor: Theme.base
                    selectionColor: Theme.text
                    background: Item {}

                    onTextChanged: {
                        if (scrollView.stickToBottom) {
                            // Defer scroll to bottom to ensure layout has updated
                            Qt.callLater(() => {
                                scrollView.ScrollBar.vertical.position = Math.max(0, 1.0 - scrollView.ScrollBar.vertical.size)
                            })
                        }
                    }
                }
            }
        }
    }
}
