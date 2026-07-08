import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
// Aliased (Fase 14.1): Qt.labs.platform below (SystemTrayIcon's own Menu/
// MenuItem) also exports a type named "FileDialog" - importing both
// unqualified would make the bare name ambiguous the moment globalSaveDialog
// below tries to use it.
import QtQuick.Dialogs as QuickDialogs
import Qt.labs.platform 1.1
// Smart Maximize: Window.Maximized and the Screen attached type used by
// toggleSmartMaximize() below both come from this module - TopHeader.qml/
// WindowControls.qml already had it for their own Window.* references, but
// main.qml didn't need it until now.
import QtQuick.Window
import GremblingNexus

ApplicationWindow {
    id: window

    width: 1450
    height: 760
    minimumWidth: 1100
    minimumHeight: 600
    visible: true
    title: mainViewModel.windowTitle
    color: Theme.base

    // Smart Maximize (QoL): on triple-monitor/Nvidia-Surround setups,
    // showMaximized() stretches the window across every monitor at once -
    // unusable. Past a Screen.desktopAvailableWidth threshold, this instead
    // sizes the window to a standard 16:9 "single normal monitor" width
    // derived from the *actual* available height (2560 on a 1440p-tall
    // screen, 1920 on 1080p, ...) and centers it, tracked via
    // isPseudoMaximized/normalGeometry since showMaximized()'s own
    // visibility/showNormal() round-trip doesn't apply here.
    property bool isPseudoMaximized: false
    property rect normalGeometry: Qt.rect(0, 0, 1450, 760)

    function toggleSmartMaximize() {
        if (window.visibility === Window.Maximized || window.isPseudoMaximized) {
            window.showNormal();
            if (window.isPseudoMaximized) {
                window.x = window.normalGeometry.x;
                window.y = window.normalGeometry.y;
                window.width = window.normalGeometry.width;
                window.height = window.normalGeometry.height;
                window.isPseudoMaximized = false;
            }
        } else {
            // Pantalla de más de 3400px de ancho (típico de 3 monitores o Super Ultra-wides)
            if (Screen.desktopAvailableWidth > 3400) {
                window.normalGeometry = Qt.rect(window.x, window.y, window.width, window.height);

                // Ancho de "monitor normal" usando relación 16:9 sobre el alto real
                let targetWidth = Math.round(Screen.desktopAvailableHeight * (16 / 9));

                window.y = 0;
                window.height = Screen.desktopAvailableHeight;
                window.width = targetWidth;
                window.x = (Screen.desktopAvailableWidth - targetWidth) / 2;
                window.isPseudoMaximized = true;
            } else {
                window.showMaximized();
            }
        }
    }

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
        // qrc:/qt/qml/GremblingNexus/<path-from-CMakeLists> convention
        // engine.load() below uses) rather than a relative one - the native
        // OS tray, unlike QML's own Image/Loader, does not resolve a plain
        // relative path against this document's own base URL, so the icon
        // silently failed to appear in the system tray.
        icon.source: "qrc:/qt/qml/GremblingNexus/src/qml/grembling_icon.svg"
        tooltip: qsTr("Gremlin Nexus - Engine Active")

        onActivated: function(reason) {
            if (reason === SystemTrayIcon.Trigger || reason === SystemTrayIcon.DoubleClick) {
                window.show();
                window.raise();
                window.requestActivate();
            }
        }

        menu: Menu {
            MenuItem {
                text: qsTr("Open Gremlin Nexus")
                onTriggered: {
                    window.show();
                    window.raise();
                    window.requestActivate();
                }
            }
            MenuItem {
                text: qsTr("Quit Engine")
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

            // Fase QoL: on ultra-wide/multi-monitor-spanning setups, letting
            // this stretch to anchors.fill's full width dragged every form's
            // RowLayouts apart (buttons pinned to one screen edge, labels to
            // the other). Capping the width and centering it keeps the
            // actual content readable while contentArea's own background
            // (window.color, set above) still fills the full window behind
            // it.
            StackLayout {
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.min(parent.width, 1600)
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
    // (called from ProfileEditorView's "Save" button, relocated there from
    // TopHeader's old floating floppy-disk icon) only ever emits
    // saveDialogRequested() when it has no cached path to reuse yet, so this
    // dialog only actually opens on that first save each session.
    QuickDialogs.FileDialog {
        id: globalSaveDialog
        title: qsTr("Save Profile As...")
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
