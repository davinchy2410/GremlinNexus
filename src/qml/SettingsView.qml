import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GremblingNexus

// "Settings" screen (Fase 10.9 mock-up, wired to SettingsViewModel in Fase
// 13): glassmorphism cards for vJoy status and app-wide preferences (run on
// Windows startup, Auto-Switch). HidHide integration was removed entirely -
// GremblingNexus no longer manages HidHide whitelisting/cloaking itself;
// users who want a device hidden from other applications whitelist this
// executable and cloak the device manually via HidHide's own GUI.
Item {
    id: root

    Rectangle {
        anchors.fill: parent
        color: Theme.base
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingLg

        ColumnLayout {
            spacing: 2
            Text {
                text: qsTr("Settings")
                color: Theme.text
                font.pixelSize: 24
                font.weight: Font.DemiBold
            }
            Text {
                text: qsTr("Application preferences")
                color: Theme.subtext0
                font.pixelSize: 13
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacingLg

            // --- Diagnostics Panel ---------------------------------------
            // Bugfix 2026-07-15: replaces a "vJoy Status" panel that showed
            // hardcoded "Driver Active"/"vJoy 2.1.9.1"/"16 (IDs 1-16)" text
            // unconditionally, regardless of whether vJoy was actually
            // installed - misleading in exactly the same way the old
            // seedMockDevices() placeholders were (see ProfileEditorViewModel's
            // own docs). Each row here reads real, live state: vJoy/ViGEmBus
            // via settingsViewModel's registry-backed detection, HidHide via
            // the hidHideController context property already wired up
            // elsewhere in the app.
            Item {
                id: diagnosticsPanel
                Layout.fillWidth: true
                Layout.fillHeight: true

                function statusColor(state) {
                    if (state === "active") return Theme.success;
                    if (state === "inactive") return Theme.warning;
                    if (state === "unknown") return Theme.overlay0;
                    return Theme.danger;
                }

                function refreshAll() {
                    settingsViewModel.refreshDiagnostics();
                    // queryCloakStateOnly() is a read-only IOCTL query (see
                    // its own docs) - unlike deactivateCloak()/
                    // reactivateCloak(), it never toggles the actual cloak
                    // state, just resolves cloakState() from Unknown to
                    // whatever's really going on right now. Needed because
                    // cloakState() is otherwise only ever populated once, by
                    // main.cpp's startup dance - which is skipped entirely
                    // when hidHideAutoCloakEnabled is off, leaving this row
                    // stuck at "Checking..." for the rest of the session
                    // with nothing else to resolve it.
                    hidHideController.queryCloakStateOnly();
                }

                Component.onCompleted: refreshAll()

                GlassPanel {
                    anchors.fill: parent
                    cornerRadius: Theme.radiusLarge
                    bgOpacity: 0.7
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingLg
                    spacing: Theme.spacingMd

                    Text { text: qsTr("Diagnostics"); color: Theme.text; font.pixelSize: 17; font.weight: Font.DemiBold }

                    ColumnLayout {
                        spacing: Theme.spacingSm
                        Layout.topMargin: Theme.spacingXs

                        // vJoy
                        RowLayout {
                            spacing: Theme.spacingSm
                            Rectangle {
                                width: 8; height: 8; radius: 4
                                color: diagnosticsPanel.statusColor(settingsViewModel.vjoyDetected ? "active" : "missing")
                            }
                            Text { text: qsTr("vJoy"); color: Theme.text; font.pixelSize: 13; Layout.preferredWidth: 90 }
                            Text {
                                text: settingsViewModel.vjoyDetected ? qsTr("Detected") : qsTr("Not Detected")
                                color: Theme.subtext0
                                font.pixelSize: 12
                            }
                        }

                        // HidHide - three real states (Active/Inactive/
                        // Unavailable), not just a boolean, since HidHideController
                        // already distinguishes "installed but cloak off" from
                        // "not installed at all".
                        RowLayout {
                            spacing: Theme.spacingSm
                            Rectangle {
                                width: 8; height: 8; radius: 4
                                color: diagnosticsPanel.statusColor(
                                    hidHideController.cloakState === HidHideController.Active ? "active" :
                                    hidHideController.cloakState === HidHideController.Inactive ? "inactive" :
                                    hidHideController.cloakState === HidHideController.Unavailable ? "missing" : "unknown")
                            }
                            Text { text: qsTr("HidHide"); color: Theme.text; font.pixelSize: 13; Layout.preferredWidth: 90 }
                            Text {
                                text: hidHideController.cloakState === HidHideController.Active ? qsTr("Active") :
                                      hidHideController.cloakState === HidHideController.Inactive ? qsTr("Installed (Cloak Off)") :
                                      hidHideController.cloakState === HidHideController.Unavailable ? qsTr("Not Detected") :
                                      qsTr("Checking...")
                                color: Theme.subtext0
                                font.pixelSize: 12
                            }
                        }

                        // ViGEmBus
                        RowLayout {
                            spacing: Theme.spacingSm
                            Rectangle {
                                width: 8; height: 8; radius: 4
                                color: diagnosticsPanel.statusColor(settingsViewModel.vigemBusDetected ? "active" : "missing")
                            }
                            Text { text: qsTr("ViGEmBus"); color: Theme.text; font.pixelSize: 13; Layout.preferredWidth: 90 }
                            Text {
                                text: settingsViewModel.vigemBusDetected ? qsTr("Detected") : qsTr("Not Detected")
                                color: Theme.subtext0
                                font.pixelSize: 12
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }

                    RowLayout {
                        spacing: Theme.spacingMd
                        GremblingButton {
                            text: qsTr("Refresh")
                            Layout.alignment: Qt.AlignLeft
                            onClicked: diagnosticsPanel.refreshAll()
                        }
                        GremblingButton {
                            text: qsTr("Reset vJoy")
                            accentColor: Theme.danger
                            accentHoverColor: Theme.danger
                            Layout.alignment: Qt.AlignLeft
                            onClicked: settingsViewModel.resetVJoy()
                        }
                    }
                }
            }

            // --- Application Preferences Panel --------------------------
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                GlassPanel {
                    anchors.fill: parent
                    cornerRadius: Theme.radiusLarge
                    bgOpacity: 0.7
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingLg
                    spacing: Theme.spacingMd

                    Text { text: qsTr("Application Preferences"); color: Theme.text; font.pixelSize: 17; font.weight: Font.DemiBold }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.spacingXs
                        spacing: Theme.spacingMd

                        Text { text: qsTr("Run on Windows Startup"); color: Theme.text; font.pixelSize: 13; Layout.fillWidth: true }
                        ToggleSwitch {
                            checked: settingsViewModel.runOnStartup
                            onToggled: (v) => settingsViewModel.runOnStartup = v
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingMd

                        Text { text: qsTr("Auto-Switch Profiles"); color: Theme.text; font.pixelSize: 13; Layout.fillWidth: true }
                        ToggleSwitch {
                            checked: settingsViewModel.autoSwitchEnabled
                            onToggled: (v) => settingsViewModel.autoSwitchEnabled = v
                        }
                    }

                    // Language (i18n): index 0 = English (no translator
                    // installed, qsTr() falls back to source text), index 1
                    // = Español (installs grembling_es.qm via I18nManager -
                    // see its own docs for why "en" is handled as "no
                    // translator" rather than a real .qm). currentIndex
                    // tracks I18nManager.currentLanguage (a real NOTIFY-
                    // backed property, restored from QSettings at startup)
                    // instead of a hardcoded 0, so reopening this screen -
                    // or a fresh launch that already restored a saved
                    // Español choice - shows the language actually active,
                    // not always "English" regardless of reality.
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingMd

                        Text { text: qsTr("Language"); color: Theme.text; font.pixelSize: 13; Layout.fillWidth: true }
                        AppComboBox {
                            id: languageCombo
                            Layout.preferredWidth: 140
                            // Autonyms (each language's own name for
                            // itself) are deliberately NOT wrapped in
                            // qsTr() - a language name should always read
                            // in its own language regardless of which UI
                            // language is currently active, so a user lost
                            // in an unfamiliar language can still recognize
                            // their own.
                            model: ["English", "Español"]
                            currentIndex: I18nManager.currentLanguage === "es" ? 1 : 0
                            onActivated: (index) => I18nManager.setLanguage(index === 1 ? "es" : "en")
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingMd

                        Text { text: qsTr("Voice Announcements"); color: Theme.text; font.pixelSize: 13; Layout.fillWidth: true }
                        ToggleSwitch {
                            checked: VoiceFeedbackManager.enabled
                            onToggled: (v) => VoiceFeedbackManager.enabled = v
                        }
                    }

                    // Debug Session Log (QoL): off by default for every
                    // install - only worth turning on while actively
                    // diagnosing a bug with someone, since it writes every
                    // qDebug()/qInfo()/qWarning() line to a growing
                    // Logs/*.txt file next to the executable for as long as
                    // it stays on (see AsyncLogSink's own docs for why this
                    // used to be always-on and why that was a problem).
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingMd

                            Text { text: qsTr("Debug Session Log"); color: Theme.text; font.pixelSize: 13; Layout.fillWidth: true }
                            ToggleSwitch {
                                checked: settingsViewModel.debugLoggingEnabled
                                onToggled: (v) => settingsViewModel.debugLoggingEnabled = v
                            }
                        }
                        Text {
                            text: qsTr("Writes every log line to a file under Logs/ - only enable while diagnosing an issue")
                            color: Theme.subtext0
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }

                    // HidHide Auto-Cloak Management (QoL - faster startup):
                    // off by default this toggle would silently reintroduce
                    // the zombie-lock bug for a HidHide user (see
                    // HidHideController's own docs / Memory.md), so it
                    // defaults ON (settingsViewModel.hidHideAutoCloakEnabled
                    // itself defaults to true when unset) - this is purely
                    // an opt-OUT for someone who doesn't use HidHide's cloak
                    // feature at all and wants to skip the ~2s of startup
                    // work that dance costs every launch either way.
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingMd

                            Text { text: qsTr("HidHide Auto-Cloak Management"); color: Theme.text; font.pixelSize: 13; Layout.fillWidth: true }
                            ToggleSwitch {
                                checked: settingsViewModel.hidHideAutoCloakEnabled
                                onToggled: (v) => settingsViewModel.hidHideAutoCloakEnabled = v
                            }
                        }
                        Text {
                            text: qsTr("Uncloaks devices briefly on startup so HidHide's whitelist works reliably - skipped automatically if HidHide's own \"Inverse application cloak\" is on. Turn off only if you don't use HidHide, for a faster launch. Takes effect on next restart.")
                            color: Theme.subtext0
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }

                    // Scripts / Script Bridge (Fase 19, Beta): the Python
                    // module is deliberately NOT part of this installer -
                    // same reasoning as vJoy/HidHide/ViGEmBus staying
                    // external downloads (see installer.iss's own comment) -
                    // so this toggle only makes sense once
                    // settingsViewModel.scriptsModuleDetected finds the
                    // separately-downloaded ScriptsModule/ folder next to the
                    // executable. Disabled (not hidden) otherwise, so a user
                    // who hasn't downloaded it yet still sees the feature
                    // exists and what to do about it.
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingMd

                            Text { text: qsTr("Scripts (Beta)"); color: Theme.text; font.pixelSize: 13; Layout.fillWidth: true }
                            ToggleSwitch {
                                enabled: settingsViewModel.scriptsModuleDetected
                                checked: settingsViewModel.scriptsEnabled
                                onToggled: (v) => settingsViewModel.scriptsEnabled = v
                            }
                        }
                        Text {
                            text: settingsViewModel.scriptsModuleDetected
                                ? qsTr("Lets external Python scripts read/drive inputs through Nexus.")
                                : qsTr("Scripts module not detected - download it from the Releases page and extract it next to GremlinNexus.exe as \"ScriptsModule\" to enable this.")
                            color: Theme.subtext0
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }

                    Item { Layout.fillHeight: true }
                }
            }
        }
    }
}
