import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Effects
import GremblingEx

// Remote Control pairing modal (Fase 16 Part 2, HTTP serving added Part 3):
// shown from TopHeader's "Web Dashboard" entry. serverIp/serverPort/
// securityToken all come straight off the already-running PwaServer (see
// main.cpp's "pwaServer" context property), so the PIN/URL shown here are
// always the real, current values - never stale copies. Closes itself the
// instant pairing succeeds (Fase 16.5 - see the Connections block below),
// so the user never has to remember to dismiss it by hand.
Popup {
    id: root

    modal: true
    focus: true
    x: Overlay.overlay ? (Overlay.overlay.width - width) / 2 : 0
    y: Overlay.overlay ? (Overlay.overlay.height - height) / 2 : 0
    width: 340
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    // Fixed at 8081 (Part 3's future HTTP server port) regardless of
    // pwaServer.serverPort (the WebSocket port, 8080) - see the file-level
    // comment above.
    readonly property string pairingUrl:
        "http://" + pwaServer.serverIp + ":8081/?token=" + pwaServer.securityToken

    // Fase 16.5: PwaServer only emits clientConnected once a client's PIN
    // is actually accepted (see PwaServer::handleAuthMessage()), so this
    // never fires for e.g. a phone that merely opened the page without
    // finishing the WebSocket handshake.
    Connections {
        target: pwaServer
        function onClientConnected(deviceId) {
            root.close();
        }
    }

    background: Item {
        Rectangle {
            id: backgroundRect
            anchors.fill: parent
            color: Qt.rgba(Theme.surface0.r, Theme.surface0.g, Theme.surface0.b, 0.85)
            radius: Theme.radiusMedium
            border.width: 1
            border.color: Theme.accent
        }

        MultiEffect {
            source: backgroundRect
            anchors.fill: backgroundRect
            shadowEnabled: true
            shadowColor: Theme.accent
            shadowBlur: 2.0
            shadowVerticalOffset: 0
            blurMax: 64
        }
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacingMd

        Item { Layout.preferredHeight: Theme.spacingMd } // top padding

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            spacing: 2

            Text { text: "Connect Tablet"; color: Theme.text; font.pixelSize: 17; font.weight: Font.DemiBold }
            Text {
                text: "Scan this code from the PWA to pair a remote device."
                color: Theme.subtext0
                font.pixelSize: 12
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 200
            Layout.preferredHeight: 200
            radius: Theme.radiusSmall
            color: "white"
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.08)

            QrCodeItem {
                anchors.fill: parent
                anchors.margins: Theme.spacingSm
                text: root.pairingUrl
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignHCenter
            spacing: 2

            Text {
                Layout.alignment: Qt.AlignHCenter
                text: "PIN: " + pwaServer.securityToken
                color: Theme.text
                font.pixelSize: 22
                font.weight: Font.Bold
                font.letterSpacing: 2
            }
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: pwaServer.serverIp !== "" ? ("Server: " + pwaServer.serverIp) : "Server not running"
                color: Theme.subtext0
                font.pixelSize: 12
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

            Item { Layout.fillWidth: true }

            ToolButton {
                label: "Close"
                onClicked: root.close()
            }
        }
    }
}
