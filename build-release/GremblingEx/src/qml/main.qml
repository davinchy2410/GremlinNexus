import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
// Aliased (Fase 14.1): Qt.labs.platform below (SystemTrayIcon's own Menu/
// MenuItem) also exports a type named "FileDialog" - importing both
// unqualified would make the bare name ambiguous the moment globalSaveDialog
// below tries to use it.
import QtQuick.Dialogs as QuickDialogs
import Qt.labs.platform 1.1
import GremblingEx

ApplicationWindow {
    id: window

    width: 1450
    height: 760
    minimumWidth: 1100
    minimumHeight: 600
    visible: true
    title: mainViewModel.windowTitle
    color: Theme.crust

    // Fase 12: drops Windows' own title bar so TopHeader.qml can supply a
    // fully custom one (drag region + WindowControls) that actually matches
    // the Catppuccin Mocha design system instead of the native chrome.
    flags: Qt.Window | Qt.FramelessWindowHint

    // Fase 17: System Tray
    onClosing: function(close_event) {
        close_event.accepted = false;
        window.hide();
    }

    SystemTrayIcon {
        id: trayIcon
        visible: true
        // Fase 14: an absolute qrc:/ path (matching the same
        // qrc:/qt/qml/GremblingEx/<path-from-CMakeLists> convention
        // engine.load() below uses) rather than a relative one - the native
        // OS tray, unlike QML's own Image/Loader, does not resolve a plain
        // relative path against this document's own base URL, so the icon
        // silently failed to appear in the system tray.
        icon.source: "qrc:/qt/qml/GremblingEx/src/qml/grembling_icon.svg"
        tooltip: "GremblingEx - Engine Active"

        onActivated: function(reason) {
            if (reason === SystemTrayIcon.Trigger || reason === SystemTrayIcon.DoubleClick) {
                window.show();
                window.raise();
                window.requestActivate();
            }
        }

        menu: Menu {
            MenuItem {
                text: "Open GremblingEx"
                onTriggered: {
                    window.show();
                    window.raise();
                    window.requestActivate();
                }
            }
            MenuItem {
                text: "Quit Engine"
                onTriggered: Qt.exit(0)
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        TopHeader {
            id: topHeader
            Layout.fillWidth: true
            onRemoteControlRequested: remoteControlPopup.open()
            onAutoSwitchRequested: autoSwitchPopup.open()
        }

        // Content area: cross-fades on section change (see the Connections
        // block below) so switching header tabs feels like a deliberate
        // transition rather than an instant, jarring swap.
        Item {
            id: contentArea
            Layout.fillWidth: true
            Layout.fillHeight: true

            opacity: 1.0
            Behavior on opacity {
                NumberAnimation { duration: Theme.animMedium; easing.type: Easing.OutCubic }
            }

            StackLayout {
                anchors.fill: parent
                currentIndex: {
                    switch (mainViewModel.currentView) {
                    case "Profiles": return 0;
                    case "Curves": return 1;
                    case "DeviceTester": return 2;
                    case "Settings": return 3;
                    case "LogConsole": return 4;
                    case "StarCitizen": return 5;
                    default: return 0;
                    }
                }

                ProfileEditorView { }
                CurveEditorView { }
                DeviceTesterView { }
                SettingsView { }
                LogConsoleView { }
                StarCitizenView { }
            }

            Connections {
                target: mainViewModel
                function onCurrentViewChanged() {
                    contentArea.opacity = 0.0;
                    fadeInTimer.restart();
                }
            }
            Timer {
                id: fadeInTimer
                interval: 1
                onTriggered: contentArea.opacity = 1.0
            }
        }
    }

    RemoteControlPopup {
        id: remoteControlPopup
    }
    
    AutoSwitchPopup {
        id: autoSwitchPopup
    }

    // Fase 14.1: global Quick Save - profileEditorViewModel.quickSave()
    // (called from TopHeader's "Quick Save" button) only ever emits
    // saveDialogRequested() when it has no cached path to reuse yet, so this
    // dialog only actually opens on that first save each session.
    QuickDialogs.FileDialog {
        id: globalSaveDialog
        title: "Save Profile As..."
        fileMode: QuickDialogs.FileDialog.SaveFile
        nameFilters: ["JSON Profiles (*.json)", "All Files (*)"]
        onAccepted: profileEditorViewModel.saveProfileToPath(selectedFile)
    }

    Connections {
        target: profileEditorViewModel
        function onSaveDialogRequested() {
            globalSaveDialog.open();
        }
    }
}
